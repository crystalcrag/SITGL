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
static void VTDebugLines(VirtualTerm vt);

#if 0
static uint8_t VTDefPalette[] = {
	/* XTerm colors */
	0x00, 0x00, 0x00, 0xff, // black
	0x7b, 0x00, 0x00, 0xff, // red
	0x00, 0x7b, 0x00, 0xff, // green
	0x7b, 0x7b, 0x00, 0xff, // brown
	0x00, 0x00, 0x7b, 0xff, // blue
	0x7b, 0x00, 0x7b, 0xff, // magenta
	0x00, 0x7b, 0x7b, 0xff, // cyan
	0xbd, 0xbd, 0xbd, 0xff, // gray
	0x7b, 0x7b, 0x7b, 0xff, // dark gray
	0xff, 0x00, 0x00, 0xff, // light red
	0x00, 0xff, 0x00, 0xff, // light green
	0xff, 0xff, 0x00, 0xff, // yellow
	0x00, 0x00, 0xff, 0xff, // light blue
	0xff, 0x00, 0xff, 0xff, // light mag.
	0x00, 0xff, 0xff, 0xff, // light cyan
	0xff, 0xff, 0xff, 0xff  // white
};
#else
static uint8_t VTDefPalette[] = {
	0x00, 0x00, 0x00, 0xff,
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
	0xFF, 0xFF, 0xFF, 0xff,
	0xDD, 0xD6, 0x05, 0xff,
};
#endif

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
	int height = vt->fontSize * vt->totalLines;

	if (height > vt->height)
	{
		if (! vt->hasScroll)
		{
			SIT_SetValues(vt->scroll, SIT_Visible, 1, NULL);
			vt->hasScroll = 1;
		}
		SIT_SetValues(vt->scroll,
			SIT_ScrollPos,  vt->topLine * vt->fontSize - vt->topOffset,
			SIT_MaxValue,   height,
			SIT_PageSize,   vt->height,
			SIT_LineHeight, vt->fontSize,
			NULL
		);
	}
	else if (vt->hasScroll)
	{
		SIT_SetValues(vt->scroll, SIT_Visible, 0, NULL);
		vt->scrollPad = 0;
		vt->hasScroll = 0;
	}
}

/* need to take tabs and styles into account */
static int VTTextFit(VirtualTerm vt, NVGcontext * vg, float max, DATA8 start, DATA8 end, DATA16 attr, Bool round)
{
	DATA8 next, eol;
	float x;
	int   length;

	nvgFontFaceId(vg, *attr & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);

	/* try to fit as many characters into <max> width */
	for (x = 0, next = start; ; next += length)
	{
		float width;
		switch (*next) {
		case 27: /* style attribute: always 3 bytes */
			length = 3;
			attr[0] = (next[1] << 8) | next[2];
			nvgFontFaceId(vg, *attr & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
			width = 0;
			break;
		case '\n':
			goto break_all;
		case '\t': /* tab characters need special processing */
			width = vt->tabSizePx - (int) x % vt->tabSizePx;
			if (x + width > max) goto break_all;
			length = 1;
			break;
		default:
			/* stop of the next EOL/tab/attr */
			for (eol = next; eol < end; eol ++)
			{
				uint8_t chr = *eol;
				if (chr == '\n' || chr == 27 || chr == '\t') break;
			}
			length = nvgTextFit(vg, next, eol, max - x, &width);
		}
		x += width;
		if (length == 0) break;
	}
	break_all:
	if (round)
	{
		if (next < eol)
		{
			eol = NEXTCHAR(next);
			float w = *next == '\t' ? vt->tabSizePx - (int) x % vt->tabSizePx : nvgTextBounds(vg, 0, 0, next, eol, NULL);
			if (x + w * 0.5 < max) next = eol;
		}
	}
	else if (*next == '\n' || next == start)
	{
		/* wrap at least one character to avoid infinite loop */
		next ++;
	}
	return next - start;
}

/* width of terminal has changed: reformat all lines */
static void VTReformat(VirtualTerm vt, NVGcontext * vg)
{
	uint16_t attrs;
	DATA8    start, eof, next;
	DATA8    selStart, selEnd;
	float    max = vt->width - vt->scrollPad;
	char     hasSel = vt->hasSelect;
	int      total;

	if (vt->buffer == NULL)
		return;

	if (hasSel)
	{
		selStart = vt->buffer + vt->lines[vt->selStart.line] + vt->selStart.chr;
		selEnd   = vt->buffer + vt->lines[vt->selEnd.line]   + vt->selEnd.chr;
	}
	else selStart = selEnd = NULL;

	start = vt->buffer;
	attrs = 0;
	total = vt->reformat;
	eof = start + vt->bufUsage;
	if (total >= 0 && total < vt->totalLines)
	{
		start += vt->lines[total];
		attrs  = vt->styles[total];
		total  = vt->totalLines-1;
		if (total < 0) total = 0;
	}
	else total = 0;

	for (; start < eof; start = next)
	{
		uint16_t curAttrs = attrs;
		next = start + VTTextFit(vt, vg, max, start, eof, &attrs, False);

		if (vt->wordWrap && next > start && next < eof && next[-1] != '\n')
		{
			/* only keep full words */
			DATA8 prev;
			for (prev = next; prev > start && VTCharClass[*prev] != SPC; prev --);
			if (prev > start) next = prev + 1;
		}

		if (hasSel)
		{
			if (start <= selStart && selStart < next)
			{
				vt->selStart.line = total;
				vt->selStart.chr  = selStart - start;
			}
			if (start <= selEnd && selEnd < next)
			{
				vt->selEnd.line = total;
				vt->selEnd.chr  = selEnd - start;
			}
		}
		if (total >= vt->allocLines)
		{
			int max = (total + VT_LINES) & ~(VT_LINES-1);

			DATA32 lines = realloc(vt->lines, max * 6);
			if (!lines) return;
			vt->lines  = lines;
			vt->styles = (DATA16) (lines + max);
			memmove(vt->styles, vt->lines + vt->allocLines, vt->allocLines * 2);
			vt->allocLines = max;
		}

		vt->lines[total] = start - vt->buffer;
		vt->styles[total] = curAttrs;
		total ++;
	}
	vt->lines[total] = start - vt->buffer;
	vt->styles[total] = 0;
	vt->totalLines = total;
	vt->reformat = -1;

	if (hasSel)
	{
		vt->selCur = vt->selStart;
		VTMarkSelection(vt);
	}

	//fprintf(stderr, "total = %d\n", total);
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
	float uline = vt->lineAttr & VT_ATTRLINE ? x : -1;
	float dx = 0, dy = 0;

	/* selected text don't get shifted */
	if (useCurFgBg != VT_SELTEXT)
		dx = vt->dx, dy = vt->dy;

	nvgFontFaceId(vg, vt->lineAttr & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
	if ((useCurFgBg & VT_USECURFG) == 0)
		nvgFillColorRGBA8(vg, vt->palette + (vt->lineAttr & 15) * 4);

	while (start < end)
	{
		DATA8   next;
		uint8_t chr;
		float   ox;
		for (next = start, chr = 0; next < end; next ++)
		{
			chr = *next;
			if (chr == '\t' || chr == '\n' || chr == 27) break;
		}
		if (next > start)
		{
			ox = x;
			if ((useCurFgBg & VT_NOCUSTBG) == 0 && (vt->lineAttr & 0xf0))
			{
				/* paint a custom background */
				nvgSave(vg);
				nvgBeginPath(vg);
				nvgRect(vg, x, y, nvgTextBounds(vg, 0, 0, start, next, NULL), vt->fontSize);
				if ((useCurFgBg & VT_USECURBG) == 0)
					nvgFillColorRGBA8(vg, vt->palette + ((vt->lineAttr & 0xf0) >> 2));
				nvgFill(vg);
				nvgRestore(vg);
			}
			x = nvgText(vg, x + dx, y + dy, start, next) - dx;
			/* user didn't provide a bold font face: fake it by overwriting text 1px to the right */
			if (vt->fakeBold && (vt->lineAttr & VT_ATTRBOLD))
				/* 1px might not be enough for big font size, but this is already a hack */
				nvgText(vg, ox+1 + dx, y + dy, start, next);

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
			vt->lineAttr = (next[1] << 8) | next[2];
			nvgFontFaceId(vg, vt->lineAttr & VT_ATTRBOLD ? vt->fontBoldId : vt->fontId);
			next += 3;
			uline = vt->lineAttr & VT_ATTRLINE ? x : -1;
			break;
		case '\n':
			return x;
		case '\t':
			/* skip tab */
			ox = vt->tabSizePx - ((int) x - vt->startx) % vt->tabSizePx;
			if ((useCurFgBg & VT_NOCUSTBG) == 0 && (vt->lineAttr & 0xf0))
			{
				nvgSave(vg);
				nvgBeginPath(vg);
				nvgRect(vg, x, y, ox, vt->fontSize);
				if ((useCurFgBg & VT_USECURBG) == 0)
					nvgFillColorRGBA8(vg, vt->palette + ((vt->lineAttr & 0xf0) >> 2));
				nvgFill(vg);
				nvgRestore(vg);
			}
			x += ox;
			next ++;
			if (uline >= 0) { chr = 0; goto underline; }
		}
		start = next;
	}
	return x;
}

static float nvgTextBoundsWithTabs(VirtualTerm vt, NVGcontext * vg, float x, DATA8 start, DATA8 end, DATA16 attrs)
{
	while (start < end)
	{
		DATA8   next;
		uint8_t chr;
		for (next = start, chr = 0; next < end; next ++)
		{
			chr = *next;
			if (chr == '\t' || chr == '\n' || chr == 27) break;
		}
		if (next > start)
			x += nvgTextBounds(vg, x, 0, start, next, NULL);
		switch (chr) {
		case 27:
			if (attrs) attrs[0] = (next[1] << 8) | next[2];
			nvgFontFaceId(vg, next[1] & (VT_ATTRBOLD >> 8) ? vt->fontBoldId : vt->fontId);
			next += 3;
			break;
		case '\n':
			x += vt->spaceLen;
			next ++;
			break;
		case '\t':
			/* skip tab */
			x += vt->tabSizePx - ((int) x - vt->startx) % vt->tabSizePx;
			next ++;
		}
		start = next;
	}
	return x;
}

/* try to set the top visible line as close as possible to <top> */
static void VTAdjustTop(VirtualTerm vt, int top)
{
	int maxVisible = (vt->height + vt->fontSize - 1) / vt->fontSize;
	int offset = 0;

	if (top + maxVisible > vt->totalLines)
		top = vt->totalLines - maxVisible, offset = vt->height - maxVisible * vt->fontSize;
	if (top < 0) top = offset = 0;

	vt->topLine = top;
	vt->topOffset = offset;

	//fprintf(stderr, "top = %d, off = %d, total = %d, fs = %d, height = %d\n", top, offset, vt->totalLines, vt->fontSize, vt->height);
}

static int VTPaint(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	VirtualTerm   vt    = ud;
	NVGcontext *  vg    = paint->nvg;
	uint16_t      attrs;

	if (vt->formatWidth != vt->width - vt->scrollPad)
	{
		if (vt->waitConf)
		{
			/* only need to do this once */
			TEXT fontBold[64];
			CopyString(fontBold, nvgGetFontName(vg, paint->fontId), sizeof fontBold);
			StrCat(fontBold, sizeof fontBold, 0, "-bold");
			vt->ctx = vg;
			vt->fontSize = paint->fontSize;
			vt->fontId = paint->fontId;
			vt->spaceLen = nvgTextBounds(vg, 0, 0, " ", NULL, NULL);
			vt->tabSizePx = vt->spaceLen * vt->tabSize;
			vt->waitConf = False;
			vt->fontBoldId = nvgFindFont(vg, fontBold);
			if (vt->fontBoldId < 0)
				vt->fontBoldId = vt->fontId, vt->fakeBold = 1;
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

	nvgIntersectScissor(vg, x, y, paint->w, paint->h);

	y += vt->topOffset;
	i = vt->topLine;
	vt->lineAttr = vt->styles ? vt->styles[i] : 0;
	vt->startx   = x;

	//fprintf(stderr, "paint: top = %d, off = %d\n", vt->topLine, vt->topOffset);

	/* render shadows layer per layer */
	SITTSH textShadow = paint->shadow;
	for (shadow = paint->shadowCount, drawFgBg = 0; shadow >= 0; shadow --, textShadow ++, drawFgBg |= VT_NOCUSTBG)
	{
		float startx = x;
		float starty = y;
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
		attrs = i < vt->totalLines ? vt->styles[i] : 0;

		for (max = vt->totalLines - vt->topLine, vt->lineAttr = attrs; max > 0 && y < b; i ++, max --)
		{
			uint16_t flags = vt->styles[i];
			uint8_t  hasNL = 0;
			DATA8    start = vt->buffer + vt->lines[i], end;

			end = vt->buffer + vt->lines[i+1];
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
						nvgRect(vg, x2, y, w, vt->fontSize);
						nvgFill(vg);
						nvgFillColorRGBA8(vg, vt->selColors);
						/* if someone uses a god awful theme, selection will be able to override this crap */
						x2 = nvgTextWithTabs(vt, vg, x2, y, start, start + e, VT_SELTEXT);
					}
					else x2 = nvgTextBoundsWithTabs(vt, vg, x2, start, start + e, &vt->lineAttr);
					nvgFillColor(vg, fg);
					start += e;
				}
				if (start < end-hasNL)
					nvgTextWithTabs(vt, vg, x2, y, start, end, drawFgBg);
			}
			else nvgTextWithTabs(vt, vg, x, y, start, end, drawFgBg);
			y += paint->fontSize;
		}
		x = startx;
		y = starty;
		i = vt->topLine;
	}

	return 1;
}

static void VTAddText(VirtualTerm vt, STRPTR text)
{
	static uint8_t ctrlCodes[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0
	};
	int len = strlen(text);

	if (vt->bufUsage + len > vt->bufMax)
	{
		int   max    = (vt->bufUsage + len + (VT_CHUNK-1)) & ~(VT_CHUNK-1);
		DATA8 buffer = realloc(vt->buffer, max);
		if (! buffer) return;
		vt->buffer = buffer;
		vt->bufMax = max;
	}

	DATA8 dest = vt->buffer + vt->bufUsage;
	DATA8 end;
	/* parse VT100 escape sequence */
	while (*text)
	{
		uint8_t chr = *text++;
		switch (chr < 32 ? ctrlCodes[chr] : 1) {
		default: break;                /* discard */
		case 1:  *dest++ = chr; break; /* normal */
		case 2:                        /* special: attributes */
			if (text[0] != '[') break;
			for (end = text += 1; *end && ! isalpha(*end); end ++);
			if (*end != 'm') { text = end+1; break; } /* not a style attribute: don't care */
			while (*text != 'm')
			{
				int attr = strtoul(text, &text, 10);
				if (30 <= attr && attr <= 37)
				{
					/* set foreground color: dark tones */
					vt->curAttr &= ~0x0f;
					vt->curAttr |= attr - 30;
				}
				else if (40 <= attr && attr <= 47)
				{
					/* set background color: dark tones */
					vt->curAttr &= ~0xf0;
					vt->curAttr |= (attr - 40) << 4;
				}
				else if (90 <= attr && attr <= 97)
				{
					/* foreground: bright tones */
					vt->curAttr &= ~0x0f;
					vt->curAttr |= attr - 90 + 8;
				}
				else if (100 <= attr && attr <= 107)
				{
					/* background: bright tones */
					vt->curAttr &= ~0xf0;
					vt->curAttr |= (attr - 100 + 8) << 4;
				}
				else switch (attr) {
				case  0: vt->curAttr = vt->defAttr; break;
				case  1: vt->curAttr |=  VT_ATTRBOLD; break;
				case 21: // no break;
				case  2: vt->curAttr &= ~VT_ATTRBOLD; break;
				case  4: vt->curAttr |=  VT_ATTRLINE; break;
				case  7: vt->curAttr |=  VT_ATTRINV;  break;
				case 24: vt->curAttr &= ~VT_ATTRLINE; break;
				case 27: vt->curAttr &= ~VT_ATTRINV;
				}
				if (*text == ';') text ++;
			}
			/* convert attributes into fixed size (3 bytes) */
			dest[0] = 27;
			dest[1] = vt->curAttr >> 8;
			dest[2] = vt->curAttr & 0xff;
			dest += 3;
			text ++;
		}
	}
	vt->bufUsage = dest - vt->buffer;
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
	vt->buffer = NULL;
	vt->curAttr = 0;
	vt->hasSelect = 0;
	vt->wordSelect = 0;
	vt->autoScrollDir = 0;
	vt->bufUsage = vt->bufMax = 0;
	vt->topLine = vt->topOffset = 0;
	vt->totalLines = 0;
	if (vt->autoScroll)
		SIT_ActionReschedule(vt->autoScroll, -1, -1), vt->autoScroll = NULL;
	memset(&vt->selStart, 0, 3 * sizeof vt->selStart);
}

#define VTLINE_BUF(vt, coord)    (vt->buffer + vt->lines[coord.line] + coord.chr)

/* calc selection length in bytes */
static int VTSelLength(VirtualTerm vt)
{
	DATA8 end   = VTLINE_BUF(vt, vt->selEnd);
	DATA8 start = VTLINE_BUF(vt, vt->selStart);
	int   length = 1;
	int   line;

	if (start > end)
	{
		DATA8 tmp = end;
		end = start;
		start = tmp;
		line = vt->selEnd.line;
	}
	else line = vt->selStart.line;

	start -= vt->selStart.chr;

	while (start < end)
	{
		uint16_t flags = vt->styles[line];
		DATA8    next  = vt->buffer + vt->lines[line+1];
		int      s, e;

		switch (flags & VT_SELWHOLE) {
		case VT_SELSTART: s = vt->colStart; e = next - start; break;
		case VT_SELEND:   s = 0; e = vt->colEnd; break;
		case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
		case VT_SELWHOLE: s = 0; e = next - start;
		}

		DATA8 eol = start + e;
		for (start += s; start < eol; )
		{
			if (*start == 27)
				start += 3;
			else
				length ++, start ++;
		}
		start = next;
	}
	return length;
}

/* copy selection into user supplied bufffer (length limited) */
static void VTSelCopy(VirtualTerm vt, DATA8 out)
{
	DATA8 end   = VTLINE_BUF(vt, vt->selEnd);
	DATA8 start = VTLINE_BUF(vt, vt->selStart);
	int   max   = vt->reformat;
	int   line;

	vt->reformat = -1;

	if (max == 0 || out == NULL) return;

	if (start > end)
	{
		DATA8 tmp = end;
		end = start;
		start = tmp;
		line = vt->selEnd.line;
	}
	else line = vt->selStart.line;

	start -= vt->selStart.chr;

	while (start < end)
	{
		uint16_t flags = vt->styles[line];
		DATA8    next  = vt->buffer + vt->lines[line+1];
		int      s, e;

		switch (flags & VT_SELWHOLE) {
		case VT_SELSTART: s = vt->colStart; e = next - start; break;
		case VT_SELEND:   s = 0; e = vt->colEnd; break;
		case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
		case VT_SELWHOLE: s = 0; e = next - start;
		}

		DATA8 eol = start + e;
		for (start += s; start < eol; )
		{
			if (*start != 27)
			{
				if (max > 0)
					*out++ = *start, max --;
				start ++;
			}
			else start += 3;
		}
		start = next;
	}
	if (max > 0) *out = 0;
	else out[-1] = 0;
}

static int VTSetOrGet(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnVal * val = cd;
	VirtualTerm vt  = ud;
	int         arg;

	switch (val->stage) {
	case SITV_Get:
		switch (val->tag) {
		case VT_MaxBuffer: SIT_SET(cd, vt->maxBuffer, int); break;
		case VT_Palette:   SIT_SET(cd, vt->palette, DATA8); break;
		case VT_TabSize:   SIT_SET(cd, vt->tabSize, int); break;
		case VT_TopLine:   SIT_SET(cd, vt->topLine, int); break;
		case VT_DefFgBg:   SIT_SET(cd, vt->defAttr, int); break;
		case VT_WordWrap:  SIT_SET(cd, vt->wordWrap, int); break;
		case VT_SelLength: SIT_SET(cd, VTSelLength(vt), int); break;
		case VTX_Private:  vt->reformat = (int) val->ptr; break;
		case VTX_Private2: VTSelCopy(vt, val->ptr); break;
		}
		break;
	case SITV_Set:
		switch (val->tag) {
		case VT_AddText:
			VTAddText(vt, SIT_GET(val, STRPTR));
			break;
		case VT_TabSize:
			arg = SIT_GET(val, int);
			if (arg < 2) arg = 2;
			if (arg > 100) arg = 100;
			vt->tabSize = arg;
			vt->formatWidth = 0;
			SIT_ForceRefresh();
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
		case VT_DebugLines:
			arg = SIT_GET(val, int);
			VTDebugLines(vt);
		}
		break;
	case SITV_PostProcess:
		break;
	}

	return 1;
}

static Bool VTGetCoord(VirtualTerm vt, VTCoord coord, int x, int y)
{
	NVGcontext * vg = vt->ctx;
	DATA8        start, next;
	uint16_t     attrs;
	int          line = vt->topLine + (y - vt->topOffset) / vt->fontSize;

	if (vt->totalLines == 0)
		return False;
	if (line >= vt->totalLines)
		line  = vt->totalLines - 1;
	if (line < 0)
		line = 0;

	start = vt->buffer + vt->lines[line];
	next  = vt->buffer + vt->lines[line+1];
	attrs = vt->styles[line];
	coord->line = line;

	nvgFontSize(vg, vt->fontSize);
	nvgTextAlign(vg, NVG_ALIGN_TOP | NVG_ALIGN_LEFT);

	coord->chr = VTTextFit(vt, vg, x, start, next, &attrs, True);
	return True;
}

/* quick and dirty character classifier */
static inline char VTCharType(uint8_t chr)
{
	return chr < 128 ? VTCharClass[chr] : CHR;
}

static int VTAdjustCoordPrevWord(VirtualTerm vt, VTCoord coord)
{
	DATA8 min;
	DATA8 start = vt->buffer + vt->lines[coord->line];
	DATA8 word  = start + coord->chr;
	char  type  = VTCharType(*word);

	for (min = start; *min == 27; min += 3);
	if (word > min)
	{
		for (word --; word > min && VTCharType(*word) == type; word --);
	}
	if (word > min) word = NEXTCHAR(word);
	return word - start;
}

static int VTAdjustCoordNextWord(VirtualTerm vt, VTCoord coord)
{
	DATA8 start = vt->buffer + vt->lines[coord->line];
	DATA8 end   = vt->buffer + vt->lines[coord->line+1];
	DATA8 word  = start + coord->chr;
	char  type  = VTCharType(*word);

	if (end[-1] == '\n') end --;

	if (word < end)
	{
		for (word ++; word < end && VTCharType(*word) == type; word = NEXTCHAR(word));
	}
	return word - start;
}

/* update selection flags on lines from selCur to selEnd */
static Bool VTMarkSelection(VirtualTerm vt)
{
	DATA8 cur   = VTLINE_BUF(vt, vt->selCur);
	DATA8 start = VTLINE_BUF(vt, vt->selStart);
	int   line, end;

	line = vt->selCur.line;
	end  = vt->selEnd.line;

	if (vt->wordSelect != 2) /* initial point for word selection */
	{
		if (memcmp(&vt->selEnd, &vt->selStart, sizeof vt->selEnd) == 0 && ! vt->wordSelect)
		{
			/* move selection back to initial point: remove selection */
			vt->selCur = vt->selEnd;
			if (vt->hasSelect)
			{
				vt->hasSelect = 0;
				vt->styles[line] &= 0xff;
				return True;
			}
			return False;
		}

		/* move mouse over same character */
		if (memcmp(&vt->selEnd, &vt->selCur, sizeof vt->selEnd) == 0)
			return False;
	}

	DATA16 style;
	if (line < end)
	{
		while (line < end)
		{
			style = vt->styles + line;
			if (cur < start) *style &= 0xff; /* clear selection */
			else             *style |= VT_SELWHOLE;
			line ++;
			cur = vt->buffer + vt->lines[line];
		}
	}
	else while (line > end)
	{
		style = vt->styles + line;
		if (cur > start) *style &= 0xff; /* clear selection */
		else             *style |= VT_SELWHOLE;
		line --;
		cur = vt->buffer + vt->lines[line];
	}
	style = vt->styles + line;
	*style = (*style & 255) | (cur < start ? VT_SELSTART : VT_SELEND);

	style = vt->styles + vt->selStart.line;
	cur = VTLINE_BUF(vt, vt->selEnd);
	if (start <= cur)
	{
		vt->colStart = vt->wordSelect ? VTAdjustCoordPrevWord(vt, &vt->selStart) : vt->selStart.chr;
		vt->colEnd   = vt->wordSelect ? VTAdjustCoordNextWord(vt, &vt->selEnd)   : vt->selEnd.chr;
		end = VT_SELSTART;
	}
	else
	{
		vt->colStart = vt->wordSelect ? VTAdjustCoordPrevWord(vt, &vt->selEnd)   : vt->selEnd.chr;
		vt->colEnd   = vt->wordSelect ? VTAdjustCoordNextWord(vt, &vt->selStart) : vt->selStart.chr;
		end = VT_SELEND;
	}
	*style = (*style & 0xff) | (line == vt->selStart.line ? VT_SELSTART | VT_SELEND : end);

	vt->hasSelect |= 1;
	vt->selCur = vt->selEnd;
	return True;
}

static void VTDebugLines(VirtualTerm vt)
{
	DATA8 eof = vt->buffer + vt->bufUsage;
	DATA8 line = vt->buffer;
	int   i = 0;
	while (line < eof)
	{
		TEXT  buffer[16];
		DATA8 start = line;
		DATA8 sel   = "--";
		int   style = vt->styles[i];
		switch (style & VT_SELWHOLE) {
		case VT_SELBOTH:  sel = "SE"; break;
		case VT_SELEND:   sel = "-E"; break;
		case VT_SELSTART: sel = "S-"; break;
		case VT_SELWHOLE: sel = "AA";
		}
		i ++;
		line = vt->buffer + vt->lines[i];
		buffer[0] = 0;
		if (line-start > 10)
		{
			CopyString(buffer, start, 11);
			strcpy(buffer+10, "...");
		}
		else CopyString(buffer, start, line-start);
		fprintf(stderr, "%2d: [%2d] [%2x] %s: %s\n", i-1, line - start, style, sel, buffer);
	}
}

static void VTClearSelection(VirtualTerm vt)
{
	DATA8 start = VTLINE_BUF(vt, vt->selStart);
	DATA8 end   = VTLINE_BUF(vt, vt->selEnd);
	int   line;

	if (start > end)
	{
		DATA8 tmp = start;
		start = end;
		end = tmp;
		line = vt->selEnd.line;
	}
	else line = vt->selStart.line;

	if (start == end)
	{
		vt->styles[line] &= 0xff;
	}
	else while (start < end)
	{
		vt->styles[line] &= 0xff; line ++;
		start = vt->buffer + vt->lines[line];
	}
	vt->hasSelect = 0;
}

static void VTSelectAll(VirtualTerm vt)
{
	int i;
	if (vt->buffer == NULL) return;
	memset(&vt->selStart, 0, sizeof vt->selStart);
	for (i = 0; i < vt->totalLines; i ++)
	{
		DATA16 styles = vt->styles + i;
		*styles = (*styles & 0xff) | VT_SELWHOLE;
	}
	vt->hasSelect = 3;
	vt->selEnd.line = i;
	vt->selEnd.chr = 1; /* not exact, but just need to not be 0 */
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
		#if 1
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
		#endif

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
	int scroll = 0, max = vt->totalLines * vt->fontSize - vt->height;
	SIT_GetValues(vt->scroll, SIT_ScrollPos, &scroll, NULL);
	switch (msg->keycode) {
	case SITK_PrevPage: scroll -= vt->height; break;
	case SITK_NextPage: scroll += vt->height; break;
	case SITK_Home:     scroll  = 0; break;
	case SITK_End:      scroll  = max; break;
	case SITK_Up:       scroll -= vt->fontSize; break;
	case SITK_Down:     scroll += vt->fontSize; break;
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
	vt->width  = sz[0];
	vt->height = sz[1];
	if (vt->totalLines > 0)
		VTAdjustScroll(vt);
	return 1;
}

static int VTScrollPad(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	vt->scrollPad = ((float *)cd)[0];
	return 0;
}

static int VTTrackPos(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	int pos = (int) cd;

	vt->topLine = pos / vt->fontSize;
	vt->topOffset = vt->topLine * vt->fontSize - pos;

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

	vt->canvas    = canvas;
	vt->scroll    = scroll;
	vt->maxBuffer = 64 * 1024;
	vt->tabSize   = 4;
	vt->palette   = VTDefPalette;
//	vt->topTarget = 1e6;
	vt->reformat  = -1;
	vt->waitConf  = 1;
	vt->wordWrap  = 1;

	GetSelectionColor(vt->selColors, vt->selColors+4);

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
