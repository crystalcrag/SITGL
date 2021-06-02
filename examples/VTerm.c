/*
 * VTerm.c: console widget to dump VT100 byte stream. Text will wordwrapped, and can be selected using
 *          mouse or keyboard. See doc/doc.html for more information on how to use this code.
 *
 * written by T.Pierron, May 2021.
 */

#define VT_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "SIT.h"
#include "extra.h"

static Bool VTMarkSelection(VirtualTerm vt);
static int VTRawKey(SIT_Widget w, APTR cd, APTR ud);

static uint8_t VTDefPalette[] = {
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0xAA, 0xff,
	0x00, 0xAA, 0x00, 0xff,
	0x00, 0xAA, 0xAA, 0xff,
	0xAA, 0x00, 0x00, 0xff,
	0xAA, 0x00, 0xAA, 0xff,
	0xFF, 0xAA, 0x00, 0xff,
	0xAA, 0xAA, 0xAA, 0xff,
	0x55, 0x55, 0x55, 0xff,
	0x55, 0x55, 0xFF, 0xff,
	0x55, 0xFF, 0x55, 0xff,
	0x55, 0xFF, 0xFF, 0xff,
	0xFF, 0x55, 0x55, 0xff,
	0xFF, 0x55, 0xFF, 0xff,
	0xFF, 0xFF, 0x55, 0xff,
	0x00, 0x00, 0x00, 0xff,
};

static uint8_t VTUtf8Next[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4};
static uint8_t VTCharClass[128];

enum /* VTCharClass entries */
{
	CHR, SEP, SPC = ' '
};

#define NEXTCHAR(str)              (str + VTUtf8Next[*(str) >> 4])


/* hide or show scrollbar according to amount of text */
static void VTAdjustScroll(VirtualTerm vt)
{
	int height = vt->lineHeight * vt->totalLines;

	if (height > vt->height)
	{
		if (! vt->hasScroll)
		{
			SIT_SetValues(vt->scroll, SIT_Visible, 1, NULL);
			vt->hasScroll = 1;
		}
		SIT_SetValues(vt->scroll,
			SIT_ScrollPos,  vt->topLine * vt->lineHeight - vt->topOffset,
			SIT_MaxValue,   height,
			SIT_PageSize,   vt->height,
			SIT_LineHeight, vt->lineHeight,
			NULL
		);
	}
	else if (vt->hasScroll)
	{
		SIT_SetValues(vt->scroll, SIT_Visible, 0, NULL);
		vt->topLine   = 0;
		vt->scrollPad = 0;
		vt->hasScroll = 0;
	}
}

/* iterators over ring buffer */
static void VTInitIter(VirtualTerm vt, VTIter_t * iter, int start, int end)
{
	DATA8 line = vt->buffer + vt->lines[start].offset;
	DATA8 next = vt->buffer + vt->lines[start+1].offset;
	iter->line  = line;
	iter->split = 0;
	iter->endLine = end;
	iter->endChr  = 0;
	iter->curLine = start;
	iter->curChr  = 0;
	if (next <= line)
		iter->size = next + vt->bufMax - line, iter->split = 1;
	else
		iter->size = next - line;
}

static Bool VTIterNext(VirtualTerm vt, VTIter_t * iter)
{
	int cur = iter->curLine + 1;
	if (cur > iter->endLine) return False;
	DATA8 line = vt->buffer + vt->lines[cur].offset;
	DATA8 next = vt->buffer + vt->lines[cur+1].offset;
	iter->line = line;
	iter->curLine = cur;
	if (next < line)
		iter->size = next + vt->bufMax - line, iter->split = 1;
	else
		iter->size = next - line, iter->split = 0;
	return True;
}

/* suppose that iter->line has been pre-allocated before calling this function */
static void VTIterUnsplit(VirtualTerm vt, VTIter_t * iter)
{
	DATA8 line = vt->buffer + vt->lines[iter->curLine].offset;
	DATA8 dest = iter->line;
	int   size = vt->buffer + vt->bufMax - line;
	memcpy(dest, line, size); dest += size;
	memcpy(dest, vt->buffer, iter->size - size);
}

/* cannot be an inline function due to alloca() */
#define VT_CHECKIFSPLIT(iter)   { \
	if (iter.split) { \
		iter.line = alloca(iter.size); \
		VTIterUnsplit(vt, &iter); \
	} \
}

/* take care of all the special characters within the content buffer */
static float nvgTextBoundsWithTabs(VirtualTerm vt, NVGcontext * vg, float x, DATA8 start, DATA8 end, DATA16 attrs)
{
	while (start < end)
	{
		DATA8 next;
		for (next = start; next < end && *next >= 32; next ++);
		if (next > start)
			x += nvgTextBounds(vg, x, 0, start, next, NULL);
		switch (*next) {
		case 27:
			if (attrs) attrs[0] = (next[1] << 8) | next[2];
			nvgFontFaceId(vg, next[1] & (VT_ATTRBOLD >> 8) ? vt->fontBoldId : vt->fontId);
			next += 3;
			break;
		case 28: /* variable width space */
			x += (next[1] << 8) | next[2];
			next += 3;
			break;
		case 29: /* indent */
			if (attrs) attrs[1] = (next[1] << 8) | next[2];
			next += 3;
			break;
		case '\n':
			x += vt->spaceLen;
			next ++;
			break;
		case '\t':
			x += vt->tabSizePx - ((int) x - vt->startx) % vt->tabSizePx;
		default: /* should not happen, but will prevent an infinite loop if it does */
			next ++;
		}
		start = next;
	}
	return x;
}

/* need to take tabs and styles into account */
static int VTTextFit(VirtualTerm vt, NVGcontext * vg, float max, DATA8 start, DATA8 end, DATA16 attr, Bool round)
{
	DATA8 next, eol, eof;
	float x;
	int   fit, len;

	nvgFontFaceId(vg, *attr & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
	eof = vt->buffer + vt->bufMax;

	/* try to fit as many characters into <max> width */
	for (x = start != vt->buffer + vt->bufStart && start[start == vt->buffer ? vt->bufMax-1 : -1] != '\n' ? attr[1] : 0, len = 0, next = start; next != end; )
	{
		/* note: cannot use iterators at this point, since we are in the process of building line ptr (which are needed by iterators) */
		DATA8   frag;
		float   width;
		uint8_t split;

		/* get to end of token first */
		split = 0;
		fit   = 0;
		switch (*next) {
		case 27:
		case 28:
		case 29: eol = next + 3; fit = 3; break; /* style attribute: always 3 bytes */
		case '\n': goto break_all;
		case '\t': eol = next + 1; fit = 1; break;
		default:
			/* stop at the next EOL/tab/attr */
			for (eol = next, fit = 0; eol != end && fit < 1024 && *eol >= 32; fit ++)
			{
				eol ++;
				if (eol == end) { fit ++; break; }
				if (eol == eof) eol = vt->buffer, split = 1;
			}
		}
		if (eol > eof) eol -= vt->bufMax, split = 1;

		if (split)
		{
			/* unsplit for easier parsing */
			frag = alloca(fit);
			int remain = eof - next;
			memcpy(frag, next, remain);
			memcpy(frag + remain, vt->buffer, fit - remain);
		}
		else frag = next;

		/* parse fragment */
		switch (*frag) {
		case 27: /* style attribute: always 3 bytes */
			attr[0] = fit = (frag[1] << 8) | frag[2];
			nvgFontFaceId(vg, fit & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
			width = 0;
			len += 3;
			break;
		case 28: /* custom alignment */
			width = (frag[1] << 8) | frag[2];
			if (x + (round ? width * 0.5 : width) > max) goto break_all;
			len += 3;
			break;
		case 29: /* indent */
			attr[1] = (frag[1] << 8) | frag[2];
			width = 0;
			len += 3;
			break;
		case '\t': /* tab characters need special processing */
			width = vt->tabSizePx - (int) x % vt->tabSizePx;
			if (x + (round ? width * 0.5 : width) > max) goto break_all;
			len ++;
			break;
		default:
			fit = nvgTextFit(vg, frag, next = frag + fit, max - x, &width);
			frag += fit;
			len  += fit;
			if (frag != next)
			{
				if (round)
				{
					next = NEXTCHAR(frag);
					x += width;
					width = *frag == '\t' ? vt->tabSizePx - (int) x % vt->tabSizePx : nvgTextBounds(vg, 0, 0, frag, next, NULL);
					if (x + width * 0.5 < max) len += next - frag;
				}
				goto break_all;
			}
		}
		next = eol;
		x += width;
	}
	break_all:
	next = start + len;
	if (next > eof) next -= vt->bufMax;
	if (! round && next != end && (*next == '\n' || next == start))
	{
		/* wrap at least one character to avoid infinite loop */
		next ++;
	}
	return next - start;
}

static inline int VTLineInBetween(DATA8 min, DATA8 max, DATA8 cur, int bufMax)
{
	if (max < min) max += bufMax;
	if (cur < min) cur += bufMax;
	if (min <= cur && cur <= max)
		return cur - min;
	return -1;
}

/* width of terminal has changed: reformat all lines */
static void VTReformat(VirtualTerm vt, NVGcontext * vg)
{
	VTLine   line;
	uint16_t attrs[2];
	DATA8    start, eof, end, next;
	DATA8    selStart, selEnd;
	float    max = vt->width - vt->scrollPad;
	uint8_t  hasSel = vt->hasSelect;
	int      total;

	if (vt->buffer == NULL)
		return;

	if (hasSel)
	{
		selStart = vt->buffer + vt->lines[vt->selStart.line].offset + vt->selStart.chr;
		selEnd   = vt->buffer + vt->lines[vt->selEnd.line].offset   + vt->selEnd.chr;
	}
	else selStart = selEnd = NULL;

	total = vt->bufStart + vt->bufUsage; if (total > vt->bufMax) total -= vt->bufMax;
	eof   = vt->buffer + total;
	end   = vt->buffer + vt->bufMax;
	total = vt->reformat;
	if (end == eof) end = NULL;
	if (total >= 0 && total < vt->totalLines)
	{
		/* character added at the end: don't reformat the whole content buffer */
		VTLine line = vt->lines + total;
		memcpy(attrs, &line->styles, 4);
		attrs[0] &= ~VT_PRIMARY;
		start = next = vt->buffer + line->offset;
		total = vt->totalLines-1;
		if (total < 0) total = 0;
	}
	else total = 0, start = next = vt->buffer + vt->bufStart, memset(attrs, 0, 4);

	while (next != eof)
	{
		uint16_t curAttrs[2] = {attrs[0], attrs[1]};
		next = start + VTTextFit(vt, vg, max, start, eof, attrs, False);

		if (vt->wordWrap && next != start && next < eof && next[-1] != '\n')
		{
			/* only keep full words */
			DATA8 prev, sol = vt->buffer;
			for (prev = next; prev != start && VTCharClass[*prev] != SPC; prev = prev == sol ? vt->buffer + vt->bufMax - 1 : prev - 1);
			if (prev != start) next = prev + 1;
		}

		if (hasSel)
		{
			int chr;
			if ((chr = VTLineInBetween(start, next-1, selStart, vt->bufMax)) >= 0)
			{
				vt->selStart.line = total;
				vt->selStart.chr  = chr;
			}
			if ((chr = VTLineInBetween(start, next-1, selEnd, vt->bufMax)) >= 0)
			{
				vt->selEnd.line = total;
				vt->selEnd.chr  = chr;
			}
		}
		if (total >= vt->allocLines)
		{
			int max = (total + VT_LINES) & ~(VT_LINES-1);

			line = realloc(vt->lines, max * sizeof *line);
			if (!line) return;
			vt->lines = line;
			vt->allocLines = max;
		}

		line = vt->lines + total;
		line->offset = start - vt->buffer;
		line->styles = curAttrs[0];
		line->indent = curAttrs[1];
		if (line->offset == vt->bufStart || vt->buffer[line->offset > 0 ? line->offset-1 : vt->bufMax-1] == '\n')
			line->styles |= VT_PRIMARY;
		total ++;
		if (next == end)
			next = vt->buffer;
		start = next;
	}
	line = vt->lines + total;
	line->offset = start - vt->buffer;
	line->indent = 0;
	line->styles = 0;
	vt->totalLines = total;
	vt->reformat = -1;

	if (hasSel)
	{
		vt->selCur = vt->selStart;
		VTMarkSelection(vt);
	}
}

enum
{
	VT_USECURFG = 1,
	VT_USECURBG = 2,
	VT_NOCUSTBG = 4,
	VT_SELTEXT  = 7
};

/* curFgBg: &1: use current Fg, &2: use current bg, &4: don't draw bg */
static float nvgTextWithTabs(VirtualTerm vt, NVGcontext * vg, float x, float y, DATA8 start, DATA8 end, int useCurFgBg)
{
	uint16_t attrs = vt->lineAttr[0];
	float    uline = attrs & VT_ATTRLINE ? x : -1;
	float    dx = 0, dy = 0, h = vt->lineHeight - vt->linePadding;

	/* selected text don't get shifted */
	if (useCurFgBg != VT_SELTEXT)
		dx = vt->dx, dy = vt->dy;

	nvgFontFaceId(vg, attrs & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
	if ((useCurFgBg & VT_USECURFG) == 0)
		nvgFillColorRGBA8(vg, vt->palette + (attrs & 15) * 4);

	while (start < end)
	{
		DATA8   next;
		uint8_t chr;
		float   ox;
		for (next = start, chr = 0; next < end && (chr = *next) >= 32; next ++);

		if (next > start)
		{
			ox = x;
			if ((useCurFgBg & VT_NOCUSTBG) == 0 && (attrs & 0xf0))
			{
				/* paint a custom background */
				nvgSave(vg);
				nvgBeginPath(vg);
				nvgRect(vg, x, y, nvgTextBounds(vg, 0, 0, start, next, NULL), h);
				if ((useCurFgBg & VT_USECURBG) == 0)
					nvgFillColorRGBA8(vg, vt->palette + ((attrs & 0xf0) >> 2));
				nvgFill(vg);
				nvgRestore(vg);
			}
			x = nvgText(vg, x + dx, y + dy + vt->lineBorder, start, next) - dx;
			/* user didn't provide a bold font face: fake it by overwriting text 1px to the right */
			if (vt->fakeBold && (attrs & VT_ATTRBOLD))
				/* 1px might not be enough for big font size, but this is already a hack */
				nvgText(vg, ox+1 + dx, y + dy + vt->lineBorder, start, next);

			/* handle underline style */
			if (uline >= 0)
			{
				NVGcolor fg;
				float    thick, liney;
				underline:
				thick = vt->fontSize * (1/12.);
				liney = y + vt->fontSize - thick * 0.75;
				nvgGetCurTextColor(vg, &fg);
				nvgStrokeWidth(vg, thick);
				nvgStrokeColor(vg, fg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, uline, liney);
				nvgLineTo(vg, x,     liney);
				nvgStroke(vg);
			}
		}
		switch (chr) {
		case 27:
			/* change current attribute */
			if ((useCurFgBg & VT_USECURFG) == 0)
				nvgFillColorRGBA8(vg, vt->palette + 4 * (next[2] & 15));
			vt->lineAttr[0] = attrs = (next[1] << 8) | next[2];
			nvgFontFaceId(vg, attrs & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
			next += 3;
			uline = attrs & VT_ATTRLINE ? x : -1;
			break;
			if ((useCurFgBg & VT_NOCUSTBG) == 0 && (attrs & 0xf0))
			{
				nvgSave(vg);
				nvgBeginPath(vg);
				nvgRect(vg, x, y, ox, h);
				if ((useCurFgBg & VT_USECURBG) == 0)
					nvgFillColorRGBA8(vg, vt->palette + ((attrs & 0xf0) >> 2));
				nvgFill(vg);
				nvgRestore(vg);
			}
			x += ox;
			next += 3;
			break;
		case '\n':
			return x;
		case 29:
			vt->lineAttr[1] = (next[1] << 8) | next[2];
			next += 3;
			break;
		case 28:
			ox = (next[1] << 8) | next[2];
			next += 3;
			goto case_common;
		case '\t':
			/* skip tab */
			ox = vt->tabSizePx - ((int) x - vt->startx) % vt->tabSizePx;
			next ++;
		case_common:
			if ((useCurFgBg & VT_NOCUSTBG) == 0 && (attrs & 0xf0))
			{
				nvgSave(vg);
				nvgBeginPath(vg);
				nvgRect(vg, x, y, ox, h);
				if ((useCurFgBg & VT_USECURBG) == 0)
					nvgFillColorRGBA8(vg, vt->palette + ((attrs & 0xf0) >> 2));
				nvgFill(vg);
				nvgRestore(vg);
			}
			x += ox;
			if (uline >= 0) { chr = 0; goto underline; }
		}
		start = next;
	}
	return x;
}

/* try to set the top visible line as close as possible to <top> */
static void VTAdjustTop(VirtualTerm vt, int top)
{
	int maxVisible = (vt->height + vt->lineHeight - 1) / vt->lineHeight;
	int offset = 0;

	if (top + maxVisible > vt->totalLines)
		top = vt->totalLines - maxVisible, offset = vt->height - maxVisible * vt->lineHeight;
	if (top < 0) top = offset = 0;

	vt->topLine = top;
	vt->topOffset = offset;
}

static int VTPaint(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	VirtualTerm   vt    = ud;
	NVGcontext *  vg    = paint->nvg;
	VTIter_t      start;

	if (vt->formatWidth != vt->width - vt->scrollPad)
	{
		if (vt->waitConf)
		{
			/* only need to do this once */
			TEXT fontBold[64];
			float lineHeight;
			SIT_GetCSSValue(w, "line-height", &lineHeight);
			SIT_GetCSSValue(w, "selection",   &vt->selColors);
			CopyString(fontBold, nvgGetFontName(vg, paint->fontId), sizeof fontBold);
			StrCat(fontBold, sizeof fontBold, 0, "-bold");
			vt->fontSize = paint->fontSize;
			vt->lineHeight = vt->fontSize + vt->linePadding;
			vt->fontId = paint->fontId;
			vt->spaceLen = nvgTextBounds(vg, 0, 0, " ", NULL, NULL);
			vt->tabSizePx = vt->spaceLen * vt->tabSize;
			vt->waitConf = False;
			vt->fontBoldId = nvgFindFont(vg, fontBold);
			if (vt->fontBoldId < 0)
				vt->fontBoldId = vt->fontId, vt->fakeBold = 1;
			int m = roundf(lineHeight);
			if (m > vt->fontSize+1)
			{
				m = (m - vt->fontSize) >> 1;
				if (m > 127) m = 127;
				vt->lineHeight += 2 * m;
				vt->lineBorder = m;
			}
		}
		/* redo line wrapping */
		vt->formatWidth = vt->width - vt->scrollPad;
		VTReformat(vt, vg);
		if (vt->topLine != vt->topTarget)
			VTAdjustTop(vt, vt->topTarget);
		VTAdjustScroll(vt);
	}
	else if (vt->reformat >= 0)
	{
		/* text has been added */
		VTReformat(vt, vg);
		if (vt->topLine != vt->topTarget)
			VTAdjustTop(vt, vt->topTarget);
		VTAdjustScroll(vt);
	}

	float  x = paint->x;
	float  y = paint->y;
	float  b = y + paint->h;
	int    i, max;
	int8_t shadow, drawFgBg;

	if (vt->totalLines == 0) return 1;

	nvgIntersectScissor(vg, x, y, paint->w, paint->h);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

	y += vt->topOffset;
	i = vt->topLine;
	vt->startx = x;

	VTInitIter(vt, &start, i, vt->totalLines-1);

	/* render shadows layer per layer */
	SITTSH textShadow = paint->shadow;
	for (shadow = paint->shadowCount, drawFgBg = 0; shadow >= 0; shadow --, textShadow ++, drawFgBg |= VT_NOCUSTBG)
	{
		VTIter_t iter = start;
		VTLine   line = vt->lines + i;
		float    startx = x;
		float    starty = y;
		if (shadow > 0)
		{
			drawFgBg |= VT_USECURFG;
			nvgFillColorRGBA8(vg, textShadow->color);
			nvgFontBlur(vg, textShadow->blur);
			/* note: those values will be rounded by SITGL */
			vt->dx = textShadow->dx;
			vt->dy = textShadow->dy;
		}
		else /* final text layer */
		{
			drawFgBg &= VT_NOCUSTBG;
			nvgFontBlur(vg, 0);
			textShadow = NULL;
			vt->dx = vt->dy = 0;
		}
		memcpy(vt->lineAttr, line, 4);

		for (max = vt->totalLines - vt->topLine; max > 0 && y < b; i ++, max --, line ++)
		{
			uint16_t flags = line->styles;
			uint8_t  hasNL = 0;
			DATA8    start, end;

			/* indent (soft lines only) */
			x = startx;
			if ((flags & VT_PRIMARY) == 0)
				x += line->indent;

			VT_CHECKIFSPLIT(iter);
			start = iter.line;
			end   = start + iter.size;
			if (end > start && end[-1] == '\n') end --, hasNL = 1;

			if (flags & VT_SELWHOLE)
			{
				/* line partially or entirely selected */
				uint16_t s, e;
				switch (flags & VT_SELWHOLE) {
				case VT_SELSTART: s = vt->colStart; e = end - start + hasNL; break;
				case VT_SELEND:   s = 0; e = vt->colEnd; break;
				case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
				case VT_SELWHOLE: s = 0; e = end - start + hasNL;
				}
				float x2 = x;
				if (s > e) { uint16_t tmp = s; s = e; e = tmp; }
				if (s > 0)
					x2 = nvgTextWithTabs(vt, vg, x, y, start, start + s, drawFgBg), start += s, e -= s, s = 0;
				if (e > 0)
				{
					NVGcolor fg;
					nvgGetCurTextColor(vg, &fg);
					if ((drawFgBg & VT_NOCUSTBG) == 0)
					{
						nvgSave(vg);
						float w = nvgTextBoundsWithTabs(vt, vg, x2, start, start + e, NULL) - x2;
						nvgRestore(vg);
						nvgFillColorRGBA8(vg, vt->selColors + 4);
						nvgBeginPath(vg);
						nvgRect(vg, x2, y, w, vt->lineHeight - vt->linePadding);
						nvgFill(vg);
						nvgFillColorRGBA8(vg, vt->selColors);
						/* if someone uses a god awful theme, selection will be able to override this crap */
						x2 = nvgTextWithTabs(vt, vg, x2, y, start, start + e, VT_SELTEXT);
					}
					else x2 = nvgTextBoundsWithTabs(vt, vg, x2, start, start + e, vt->lineAttr);
					nvgFillColor(vg, fg);
					start += e;
				}
				if (start < end)
					nvgTextWithTabs(vt, vg, x2, y, start, end, drawFgBg);
			}
			else nvgTextWithTabs(vt, vg, x, y, start, end, drawFgBg);
			y += vt->lineHeight;
			if (! VTIterNext(vt, &iter))
				break;
		}
		x = startx;
		y = starty;
		i = vt->topLine;
	}

	return 1;
}

static uint8_t ctrlCodes[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0
};

/* compute length of text as it will be stored in buffer */
static int VTStrLen(DATA8 text)
{
	DATA8 p;
	int   ret;
	for (p = text, ret = 0; *p; p ++)
	{
		uint8_t chr = *p;
		if (chr < 32)
		{
			if (chr == 27)
			{
				if (p[1] == '[')
				{
					/* skip styles */
					while (*p && ! isalpha(*p)) p ++;
					ret += 3;
				}
				continue;
			}
			ret += ctrlCodes[chr];
		}
		else ret ++;
	}
	return ret;
}

static int VTParseAttribute(DATA8 text, DATA8 out, DATA16 curAttr)
{
	int attrs = *curAttr;
	/* these are the VT100 escape sequence */
	while (*text != 'm')
	{
		int attr = strtoul(text, (char **) &text, 10);
		if (30 <= attr && attr <= 37)
		{
			/* set foreground color: dark tones */
			attrs &= ~0x0f;
			attrs |= attr - 30;
		}
		else if (40 <= attr && attr <= 47)
		{
			/* set background color: dark tones */
			attrs &= ~0xf0;
			attrs |= (attr - 40) << 4;
		}
		else if (90 <= attr && attr <= 97)
		{
			/* foreground: bright tones */
			attrs &= ~0x0f;
			attrs |= attr - 90 + 8;
		}
		else if (100 <= attr && attr <= 107)
		{
			/* background: bright tones */
			attrs &= ~0xf0;
			attrs |= (attr - 100 + 8) << 4;
		}
		else switch (attr) {
		case  0: attrs = 0; break;
		case  1: attrs |=  VT_ATTRBOLD; break;
		case 21: // no break;
		case  2: attrs &= ~VT_ATTRBOLD; break;
		case  4: attrs |=  VT_ATTRLINE; break;
		case 24: attrs &= ~VT_ATTRLINE; break;
		}
		if (*text == ';') text ++;
	}
	out[0] = 27;
	out[1] = attrs >> 8;
	out[2] = attrs & 0xff;
	*curAttr = attrs;
	return 3;
}

/* these are extension, not part of VT100 */
static int VTParseSpaceWidth(DATA8 text, DATA8 out, int code, DATA16 curAttr)
{
	uint32_t fixedSize = strtoul(text, (char **) &text, 10);

	if (0 < fixedSize && fixedSize < 65536)
	{
		out[0] = code;
		out[1] = fixedSize >> 8;
		out[2] = fixedSize & 0xff;
		if (code == 29)
			curAttr[1] = fixedSize;
		return 3;
	}
	/* numbers will overflow: discard style entirely */
	return 0;
}


static void VTAddText(VirtualTerm vt, DATA8 text)
{
	int len = VTStrLen(text);

	if (vt->bufUsage + len > vt->bufMax)
	{
		int max = (vt->bufUsage + len + (VT_CHUNK-1)) & ~(VT_CHUNK-1);
		int check = 0;

		if (max > vt->totalBytes)
			max = vt->totalBytes, check = 1;

		if (max != vt->bufMax)
		{
			DATA8 buffer = realloc(vt->buffer, max);
			if (! buffer) return;
			vt->buffer = buffer;
			vt->bufMax = max;
		}

		if (check)
		{
			/* buffer has reached max capacity: discard earlier content */
			int discard = vt->bufUsage + len - vt->bufMax;
			if (discard >= vt->totalBytes)
			{
				/* need to discard everything */
				vt->bufStart = 0;
				vt->totalLines = 0;
				vt->bufUsage = 0;
				vt->topLine = vt->topOffset = 0;
				vt->reformat = 0;
				if (len > vt->totalBytes)
				{
					/* even the user supplied buffer is too big :-/ */
					discard = len - vt->totalBytes;
					// XXX check if middle of an escape seq ?
					len  -= discard;
					text += discard;
				}
			}
			else
			{
				/* discard some content */
				max = vt->bufStart + discard;
				if (max >= vt->bufMax) max -= vt->bufMax;
				DATA8 content = vt->buffer + max;

				if (vt->totalLines > 0)
				{
					/* check if we can use line info */
					VTIter_t iter;
					VTInitIter(vt, &iter, 0, vt->totalLines-1);
					for (;;)
					{
						DATA8 line = iter.line;
						if (! VTIterNext(vt, &iter)) break;
						/* try to discard only complete line: will prevent from redoing word wrapping */
						if (VTLineInBetween(line, iter.line, content, vt->bufMax) >= 0)
						{
							/* need to discard all previous line */
							memmove(vt->lines, vt->lines + iter.curLine, (vt->totalLines - iter.curLine + 1) * sizeof *vt->lines);
							if (vt->hasSelect)
							{
								vt->selStart.line -= iter.curLine; if (vt->selStart.line < 0) memset(&vt->selStart, 0, sizeof vt->selStart);
								vt->selEnd.line   -= iter.curLine; if (vt->selEnd.line   < 0) memset(&vt->selEnd,   0, sizeof vt->selEnd);
								if (memcmp(&vt->selStart, &vt->selEnd, sizeof vt->selEnd) == 0)
									vt->hasSelect = 0;
							}
							discard = iter.line - (vt->buffer + vt->bufStart);
							if (discard < 0) discard += vt->bufMax;
							vt->bufUsage -= discard;
							vt->bufStart = iter.line - vt->buffer;
							vt->totalLines -= iter.curLine;
							check = 2;
							break;
						}
					}
				}
				if (check == 1)
				{
					/* line info incomplete: discard bytes then, since word wrapping will have to be redone anyway */
					vt->bufStart = content - vt->buffer;
					vt->totalLines = 0;
					vt->bufUsage = vt->bufMax - len;
				}
			}
		}
	}

	len = vt->bufStart + vt->bufUsage;
	if (len >= vt->bufMax) len -= vt->bufMax;
	DATA8 dest = vt->buffer + len;
	DATA8 eof  = vt->buffer + vt->bufMax;
	DATA8 end;
	while (*text)
	{
		uint8_t buffer[4];
		uint8_t chr = *text++;
		switch (chr < 32 ? ctrlCodes[chr] : 1) {
		default: break; /* discard */
		case 1:
			*dest++ = chr;
			vt->bufUsage ++;
			if (dest == eof) dest = vt->buffer;
			break;
		case 2: /* special: attributes */
			if (text[0] != '[') break;
			for (end = text += 1; *end && ! isalpha(*end); end ++);
			switch (*end) {
			case 'm': chr = VTParseAttribute(text, buffer, vt->curAttr); break;
			case 's': chr = VTParseSpaceWidth(text, buffer, 28, vt->curAttr); break; /* ext */
			case 'i': chr = VTParseSpaceWidth(text, buffer, 29, vt->curAttr); break; /* ext */
			default:  chr = 0;
			}
			text = end + 1;
			/* store attributes in buffer as fixed size block */
			vt->bufUsage += chr;
			for (end = buffer; chr > 0; end ++, chr --)
			{
				*dest++ = *end;
				if (dest == eof) dest = vt->buffer;
			}
		}
	}
	//fprintf(stderr, "usage = %d, start = %d  \n", vt->bufUsage, vt->bufStart);
	if (vt->reformat < 0)
	{
		len = vt->totalLines - 1;
		if (len < 0) len = 0;
		vt->reformat = len;
	}
	SIT_ForceRefresh();
}

/* clear everything: restart from scratch */
static void VTClearAll(VirtualTerm vt)
{
	free(vt->lines);
	free(vt->buffer);
	memset(vt->curAttr, 0, sizeof vt->curAttr);
	vt->lines = NULL;
	vt->buffer = NULL;
	vt->hasSelect = 0;
	vt->wordSelect = 0;
	vt->autoScrollDir = 0;
	vt->allocLines = 0;
	vt->bufUsage = vt->bufMax = vt->bufStart = 0;
	vt->topLine = vt->topOffset = 0;
	vt->totalLines = 0;
	if (vt->autoScroll)
		SIT_ActionReschedule(vt->autoScroll, -1, -1), vt->autoScroll = NULL;
	memset(&vt->selStart, 0, 3 * sizeof vt->selStart);
}

#define VTLINE_BUF(vt, coord)    (vt->buffer + vt->lines[coord.line].offset + coord.chr)

/* calc selection length in bytes */
static int VTSelLength(VirtualTerm vt)
{
	VTIter_t iter;
	int      length = 1;

	if (vt->selStart.line <= vt->selEnd.line)
		VTInitIter(vt, &iter, vt->selStart.line, vt->selEnd.line);
	else
		VTInitIter(vt, &iter, vt->selEnd.line, vt->selStart.line);

	do {
		uint16_t flags = vt->lines[iter.curLine].styles;
		uint16_t s, e;

		VT_CHECKIFSPLIT(iter);

		switch (flags & VT_SELWHOLE) {
		case VT_SELSTART: s = vt->colStart; e = iter.size; break;
		case VT_SELEND:   s = 0; e = vt->colEnd; break;
		case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
		case VT_SELWHOLE: s = 0; e = iter.size;
		}

		DATA8 start = iter.line + s;
		DATA8 eol   = iter.line + e;
		while (start < eol)
		{
			switch (*start) {
			case 27:
			case 28:
			case 29: start += 3; break;
			default: start ++; length ++;
			}
		}
	} while (VTIterNext(vt, &iter));

	return length;
}

/* copy selection into user supplied bufffer (length limited) */
static void VTSelCopy(VirtualTerm vt, DATA8 out)
{
	VTIter_t iter;
	int      max = vt->reformat;

	if (max == 0) return;
	if (vt->selStart.line <= vt->selEnd.line)
		VTInitIter(vt, &iter, vt->selStart.line, vt->selEnd.line);
	else
		VTInitIter(vt, &iter, vt->selEnd.line, vt->selStart.line);

	do {
		uint16_t flags = vt->lines[iter.curLine].styles;
		uint16_t s, e;

		VT_CHECKIFSPLIT(iter);

		switch (flags & VT_SELWHOLE) {
		case VT_SELSTART: s = vt->colStart; e = iter.size; break;
		case VT_SELEND:   s = 0; e = vt->colEnd; break;
		case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
		case VT_SELWHOLE: s = 0; e = iter.size;
		}

		DATA8 start = iter.line + s;
		DATA8 eol   = iter.line + e;
		while (start < eol)
		{
			switch (*start) {
			case 27:
			case 28:
			case 29: start += 3; break;
			default:
				if (max == 1)
				{
					*out = 0;
					return;
				}
				else *out++ = *start, max --;
				start ++;
			}
		}
	} while (VTIterNext(vt, &iter));

	*out = 0;
}

static int VTSetOrGet(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnVal * val = cd;
	VirtualTerm vt  = ud;
	int         arg;

	switch (val->stage) {
	case SITV_Get:
		switch (val->tag) {
		case VT_MaxBuffer:   SIT_SET(cd, vt->totalBytes, int); break;
		case VT_Palette:     SIT_SET(cd, vt->palette, DATA8); break;
		case VT_TabSize:     SIT_SET(cd, vt->tabSize, int); break;
		case VT_TopLine:     SIT_SET(cd, vt->topLine, int); break;
		case VT_DefFgBg:     SIT_SET(cd, vt->defAttr, int); break;
		case VT_WordWrap:    SIT_SET(cd, vt->wordWrap, int); break;
		case VT_SelLength:   SIT_SET(cd, VTSelLength(vt), int); break;
		case VT_LinePadding: SIT_SET(cd, vt->linePadding, int); break;
		case VT_TotalLines:  SIT_SET(cd, vt->totalLines, int); break;
		case VTX_Private:    vt->reformat = (int) val->ptr; break;
		case VTX_Private2:   VTSelCopy(vt, val->ptr); break;
		}
		break;
	case SITV_Set:
		switch (val->tag) {
		case VT_AddText:
			VTAddText(vt, SIT_GET(val, DATA8));
			break;
		case VT_TabSize:
			arg = SIT_GET(val, int);
			if (arg < 2) arg = 2;
			if (arg > 100) arg = 100;
			vt->tabSize = arg;
			vt->formatWidth = 0;
			SIT_ForceRefresh();
			break;
		case VT_MaxBuffer:
			arg = (SIT_GET(val, int) + VT_CHUNK - 1) & ~(VT_CHUNK-1);
			if (arg <= 0) arg = VT_CHUNK;
			vt->totalBytes = arg;
			break;
		case VT_WordWrap:
			arg = SIT_GET(val, int);
			if (arg != vt->wordWrap)
			{
				vt->wordWrap = arg;
				vt->formatWidth = 0;
				SIT_ForceRefresh();
			}
			break;
		case VT_LinePadding:
			arg = SIT_EmToReal(w, SIT_GET(val, int));
			if (arg > 255) arg = 255;
			if (arg < 0)   arg = 0;
			vt->linePadding = arg;
			vt->lineHeight = vt->fontSize + vt->linePadding;
			SIT_ForceRefresh();
		}
		break;
	case SITV_PostProcess:
		break;
	}

	return 1;
}

static Bool VTGetCoord(VirtualTerm vt, VTCoord_t * coord, int x, int y)
{
	NVGcontext * vg;
	VTIter_t     iter;
	uint16_t     attrs[2];
	int          line = vt->topLine + (y - vt->topOffset) / vt->lineHeight;

	if (vt->totalLines == 0)
		return False;
	if (line >= vt->totalLines)
		line  = vt->totalLines - 1;
	if (line < 0)
		line = 0;

	VTInitIter(vt, &iter, line, line);
	VT_CHECKIFSPLIT(iter);
	memcpy(attrs, vt->lines + line, 4);
	coord->line = line;

	SIT_GetValues(vt->canvas, SIT_NVGcontext, &vg, NULL);
	nvgFontSize(vg, vt->fontSize);
	nvgTextAlign(vg, NVG_ALIGN_TOP | NVG_ALIGN_LEFT);

	coord->chr = VTTextFit(vt, vg, x, iter.line, iter.line+iter.size, attrs, True);
	return True;
}

/* quick and dirty character classifier */
static inline char VTCharType(uint8_t chr)
{
	return chr < 128 ? VTCharClass[chr] : CHR;
}

static int VTAdjustCoordPrevWord(VirtualTerm vt, VTCoord_t * coord)
{
	VTIter_t iter;
	uint8_t  type;
	DATA8    min, word;

	VTInitIter(vt, &iter, coord->line, coord->line);
	VT_CHECKIFSPLIT(iter);
	word = iter.line + coord->chr;
	type = VTCharType(*word);

	for (min = iter.line; *min == 27; min += 3);
	if (word > min)
	{
		for (word --; word > min && VTCharType(*word) == type; word --);
		if (word > min) word = NEXTCHAR(word);
	}
	return word - iter.line;
}

static int VTAdjustCoordNextWord(VirtualTerm vt, VTCoord_t * coord)
{
	VTIter_t iter;
	uint8_t  type;
	DATA8    end, word;

	VTInitIter(vt, &iter, coord->line, coord->line);
	VT_CHECKIFSPLIT(iter);
	word = iter.line + coord->chr;
	end  = iter.line + iter.size;
	type = VTCharType(*word);
	if (end[-1] == '\n') end --;

	if (word < end)
	{
		for (word ++; word < end && VTCharType(*word) == type; word = NEXTCHAR(word));
	}
	return word - iter.line;
}

/* update selection flags on lines from selCur to selEnd */
static Bool VTMarkSelection(VirtualTerm vt)
{
	if (vt->wordSelect != 2) /* initial point for word selection */
	{
		/* move mouse over same character */
		if (memcmp(&vt->selEnd, &vt->selCur, sizeof vt->selEnd) == 0)
			return False;
	}

	VTIter_t iter;
	VTLine   line;
	int      start = vt->selStart.line;

	if (vt->selCur.line < vt->selEnd.line)
	{
		VTInitIter(vt, &iter, vt->selCur.line, vt->selEnd.line);
		do {
			line = vt->lines + iter.curLine;
			if (iter.curLine <= start) line->styles &= VT_SELCLEAR;
			else                       line->styles |= VT_SELWHOLE;
		} while (VTIterNext(vt, &iter));
	}
	else
	{
		VTInitIter(vt, &iter, vt->selEnd.line, vt->selCur.line);
		do {
			line = vt->lines + iter.curLine;
			if (iter.curLine >= start) line->styles &= VT_SELCLEAR;
			else                       line->styles |= VT_SELWHOLE;
		} while (VTIterNext(vt, &iter));
	}

	line = vt->lines + vt->selEnd.line;
	line->styles = (line->styles & VT_SELCLEAR) | (vt->selEnd.line < vt->selStart.line ? VT_SELSTART : VT_SELEND);

	if (vt->selStart.line < vt->selEnd.line || (vt->selStart.line == vt->selEnd.line && vt->selStart.chr <= vt->selEnd.chr))
	{
		vt->colStart = vt->wordSelect ? VTAdjustCoordPrevWord(vt, &vt->selStart) : vt->selStart.chr;
		vt->colEnd   = vt->wordSelect ? VTAdjustCoordNextWord(vt, &vt->selEnd)   : vt->selEnd.chr;
		start = VT_SELSTART;
	}
	else
	{
		vt->colStart = vt->wordSelect ? VTAdjustCoordPrevWord(vt, &vt->selEnd)   : vt->selEnd.chr;
		vt->colEnd   = vt->wordSelect ? VTAdjustCoordNextWord(vt, &vt->selStart) : vt->selStart.chr;
		start = VT_SELEND;
	}
	line = vt->lines + vt->selStart.line;
	line->styles = (line->styles & VT_SELCLEAR) | (vt->selEnd.line == vt->selStart.line ? VT_SELSTART | VT_SELEND : start);

	vt->hasSelect |= 1;
	vt->selCur = vt->selEnd;
	return True;
}

static void VTClearSelection(VirtualTerm vt)
{
	VTIter_t iter;

	if (vt->selStart.line < vt->selEnd.line)
		VTInitIter(vt, &iter, vt->selStart.line, vt->selEnd.line);
	else
		VTInitIter(vt, &iter, vt->selEnd.line, vt->selStart.line);

	do {
		vt->lines[iter.curLine].styles &= VT_SELCLEAR;
	} while (VTIterNext(vt, &iter));

	vt->hasSelect = 0;
}

static void VTSelectAll(VirtualTerm vt)
{
	int i;
	if (vt->buffer == NULL) return;
	memset(&vt->selStart, 0, sizeof vt->selStart);
	for (i = 0; i < vt->totalLines; i ++)
	{
		VTLine line = vt->lines + i;
		line->styles = (line->styles & VT_SELCLEAR) | VT_SELWHOLE;
	}
	vt->hasSelect = 3;
	vt->selEnd.line = i-1;
	vt->selEnd.chr = 1000; /* flags is set to VT_SELWHOLE, that value will never be used in practice */
}

static int VTAutoScroll(SIT_Widget w, APTR cd, APTR ud)
{
	static double lastTime;
	double curTime = FrameGetTime();

	/* timer callback frequency is framerate bound, this test will unbound it */
	if (curTime - lastTime >= 1000/30)
	{
		lastTime = curTime;
		VirtualTerm vt = ud;
		SIT_OnKey key = {.keycode = vt->autoScrollDir < 0 ? SITK_Up : SITK_Down};
		VTRawKey(w, &key, vt);
		vt->selCur = vt->selEnd;
		if (vt->autoScrollDir < 0)
			VTGetCoord(vt, &vt->selEnd, 0, -1);
		else
			VTGetCoord(vt, &vt->selEnd, 1e6, vt->height + 1);

		if (VTMarkSelection(vt))
			SIT_ForceRefresh();
	}
	return 1;
}

static void VTStartAutoScroll(VirtualTerm vt, int dir)
{
	int8_t cur = vt->autoScrollDir;
	if (dir && cur == 0)
		vt->autoScroll = SIT_ActionAdd(vt->canvas, FrameGetTime() + 100, -1, VTAutoScroll, vt);

	if (dir == 0 && vt->autoScroll)
		SIT_ActionReschedule(vt->autoScroll, -1, -1), vt->autoScroll = NULL;
	vt->autoScrollDir = dir;
}

static int VTMouse(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm   vt  = ud;
	SIT_OnMouse * msg = cd;

	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			static uint32_t lastClick;
			if (vt->hasSelect)
				VTClearSelection(vt), SIT_ForceRefresh();
//			fprintf(stderr, "pos: %d, %d\n", msg->x, msg->y);
			vt->selCur = vt->selStart;
			vt->wordSelect = 0;
			if (VTGetCoord(vt, &vt->selStart, msg->x, msg->y) && memcmp(&vt->selStart, &vt->selCur, sizeof vt->selStart) == 0 &&
			    TimeMS() - lastClick < 750)
			{
				vt->wordSelect = 2;
				vt->selEnd = vt->selStart;
				if (VTMarkSelection(vt))
					SIT_ForceRefresh();
				vt->wordSelect = 1;
			}
			else vt->selCur = vt->selStart;
			lastClick = TimeMS();
			return 2;
		}
		break;
	case SITOM_CaptureMove:
//		fprintf(stderr, "pos: %d, %d\n", msg->x, msg->y);
		if (msg->y < 0)
		{
			/* vertical auto-scroll */
			VTStartAutoScroll(vt, -1);
			return 1;
		}
		else if (msg->y > vt->height)
		{
			VTStartAutoScroll(vt, 1);
			return 1;
		}
		else if (vt->autoScrollDir)
		{
			/* stop auto-scrolling */
			VTStartAutoScroll(vt, 0);
		}

		if (vt->hasSelect != 3 /* select all */ && VTGetCoord(vt, &vt->selEnd, msg->x, msg->y) && VTMarkSelection(vt))
			SIT_ForceRefresh();
		break;
	case SITOM_ButtonReleased:
		if (vt->autoScrollDir)
			VTStartAutoScroll(vt, 0);
		break;
	default:
		break;
	}
	return 1;
}

static int VTRawKey(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;
	VirtualTerm vt  = ud;
	int scroll = 0, max = vt->totalLines * vt->lineHeight - vt->height;
	SIT_GetValues(vt->scroll, SIT_ScrollPos, &scroll, NULL);
	switch (msg->keycode) {
	case SITK_PrevPage: scroll -= vt->height; break;
	case SITK_NextPage: scroll += vt->height; break;
	case SITK_Home:     scroll  = 0; break;
	case SITK_End:      scroll  = max; break;
	case SITK_Up:       scroll -= vt->lineHeight; break;
	case SITK_Down:     scroll += vt->lineHeight; break;
	default: return 0;
	}
	if (scroll < 0)   scroll = 0;
	if (scroll > max) scroll = max;
	SIT_SetValues(vt->scroll, SIT_ScrollPos, scroll, NULL);
	return 1;
}

static void VT_CopyToClipboard(VirtualTerm vt)
{
	if (vt->hasSelect)
	{
		int length = VTSelLength(vt);
		if (length > 1)
		{
			DATA8 mem = length < 1024 ? alloca(length) : malloc(length);
			vt->reformat = length;
			VTSelCopy(vt, mem);
			SIT_CopyToClipboard(mem, length);
			if (length >= 1024) free(mem);
			vt->reformat = -1;
		}
	}
}

static int VTKeyboard(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;

	switch (msg->utf8[0]) {
	case 1: /* ctrl+A */
		VTSelectAll(ud);
		SIT_ForceRefresh();
		break;
	case 3: /* ctrl+C */
		VT_CopyToClipboard(ud);
		break;
	case 12: /* ctrl+L: clear all */
		VTClearAll(ud);
		VTAdjustScroll(ud);
		SIT_ForceRefresh();
		break;
	case 22: /* ctrl+V */
		cd = SIT_GetFromClipboard(NULL);
		SIT_SetValues(w, VT_AddText, cd, NULL);
		free(cd);
	}

	return 1;
}

static int VTResize(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	float *     sz = cd;
	/* lines will be readjusted in VTPaint() */
	vt->width  = sz[0];
	vt->height = sz[1];
	return 1;
}

static int VTScrollPad(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	vt->scrollPad = ((float *)cd)[0];
	return 0;
}

/* SITE_OnScroll on scrollbar */
static int VTTrackPos(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	int pos = (int) cd;

	vt->topLine = pos / vt->lineHeight;
	vt->topOffset = vt->topLine * vt->lineHeight - pos;

	return 1;
}

static int VTFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	VTClearAll(ud);
	free(ud);
	return 1;
}

void VTInit(SIT_Widget canvas, SIT_Widget scroll)
{
	VirtualTerm vt = calloc(sizeof *vt, 1);

	/* quick and dirty character classifier lookup table */
	memset(VTCharClass, SEP, 128);
	memset(VTCharClass + 'a', CHR, 26);
	memset(VTCharClass + 'A', CHR, 26);
	memset(VTCharClass + '0', CHR, 10);
	VTCharClass['_'] = CHR;
	VTCharClass[' '] = SPC;
	VTCharClass['\t'] = SPC;
	VTCharClass['\n'] = SPC;
	VTCharClass['\r'] = SPC;

	vt->canvas     = canvas;
	vt->scroll     = scroll;
	vt->totalBytes = VT_DEFMAX;
	vt->tabSize    = 4;
	vt->palette    = VTDefPalette;
//	vt->topTarget  = 1e6;
	vt->reformat   = -1;
	vt->waitConf   = 1;
	vt->wordWrap   = 1;

	SIT_AddCallback(canvas, SITE_OnSetOrGet,   VTSetOrGet,  vt);
	SIT_AddCallback(canvas, SITE_OnPaint,      VTPaint,     vt);
	SIT_AddCallback(canvas, SITE_OnResize,     VTResize,    vt);
	SIT_AddCallback(canvas, SITE_OnFinalize,   VTFinalize,  vt);
	SIT_AddCallback(canvas, SITE_OnClickMove,  VTMouse,     vt);
	SIT_AddCallback(canvas, SITE_OnRawKey,     VTRawKey,    vt);
	SIT_AddCallback(canvas, SITE_OnVanillaKey, VTKeyboard,  vt);
	SIT_AddCallback(scroll, SITE_OnScroll,     VTTrackPos,  vt);
	SIT_AddCallback(scroll, SITE_OnResize,     VTScrollPad, vt);
}
