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

static uint8_t VTUtf8Next[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4};

#define NEXTCHAR(str)    (str + VTUtf8Next[*(str) >> 4])

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
			SIT_ScrollPos,  vt->topOffset + vt->topLine * vt->fontSize,
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

static void VTInitIterLines(VirtualTerm vt, VTCoord iter, VTCoord coord)
{
	VTLines buf;
	int     total;
	for (buf = (VTLines) coord->buf->node.ln_Prev, total = 0; buf; total += VT_LINES, PREV(buf));
	*iter = *coord;
	iter->chr = total;
}

static void VTInitIter(VirtualTerm vt, VTCoord iter, int line)
{
	VTLines buf;
	int     total;

	for (buf = HEAD(vt->lines), total = 0; buf && line > total+VT_LINES; NEXT(buf), total += VT_LINES);

	iter->buf  = buf;
	iter->line = line - total;
	iter->chr  = total;
}

static DATA8 VTIterNext(VirtualTerm vt, VTCoord iter)
{
	int line = iter->line + 1;
	if (iter->chr + line >= vt->totalLines)
		return vt->buffer + vt->bufUsage;
	if (line == VT_LINES)
		NEXT(iter->buf), line = 0, iter->chr += VT_LINES;
	iter->line = line;
	return vt->buffer + iter->buf->start[line];
}

static DATA8 VTIterPrev(VirtualTerm vt, VTCoord iter)
{
	int line = iter->line - 1;
	if (line < 0)
	{
		if (iter->chr == 0) return vt->buffer;
		PREV(iter->buf);
		line = VT_LINES-1;
		iter->chr -= VT_LINES;
	}
	iter->line = line;
	return vt->buffer + iter->buf->start[line];
}

/* need to take tabs into account */
static int VTTextFit(VirtualTerm vt, NVGcontext * vg, float max, DATA8 start, DATA8 end, Bool round)
{
	DATA8 eol, next;
	float x;
	int   chr;

	for (eol = start; eol < end && *eol != '\n'; eol ++);
	/* try to fit as many characters into <max> width */
	for (chr = 0, x = 0, next = start; next < eol; next += chr)
	{
		float length;
		if (*next == '\t')
		{
			/* tab characters need special processing */
			length = vt->tabSizePx - (int) x % vt->tabSizePx;
			if (x + length > max) break;
			chr = 1;
		}
		else chr = nvgTextFit(vg, next, eol, max - x, &length);
		x += length;
		if (chr == 0) break;
	}
	/* wrap at least one character to avoid infinite loop */
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
		next ++;
	}
	return next - start;
}

static Bool VTMarkSelection(VirtualTerm vt);

static void VTReformat(VirtualTerm vt, APTR vg)
{
	VTLines buf;
	DATA8   start, eof, next;
	DATA8   selStart, selEnd;
	float   max = vt->width - vt->scrollPad;
	char    hasSel = vt->hasSelect;
	int     lines, total;

	if (hasSel)
	{
		selStart = vt->buffer + vt->selStart.buf->start[vt->selStart.line] + vt->selStart.chr;
		selEnd   = vt->buffer + vt->selEnd.buf->start[vt->selEnd.line] + vt->selEnd.chr;
	}
	else selStart = selEnd = NULL;

	for (start = vt->buffer, eof = start + vt->bufUsage, lines = total = 0, buf = HEAD(vt->lines); start < eof; start = next)
	{
		next = start + VTTextFit(vt, vg, max, start, eof, False);

		if (buf == NULL || lines >= VT_LINES)
		{
			buf = calloc(sizeof *buf, 1);
			if (! buf) return;
			ListAddTail(&vt->lines, &buf->node);
			lines = 0;
		}

		if (hasSel)
		{
			if (start <= selStart && selStart < next)
			{
				vt->selStart.line = lines;
				vt->selStart.chr  = selStart - start;
				vt->selStart.buf  = buf;
			}
			if (start <= selEnd && selEnd < next)
			{
				vt->selEnd.line = lines;
				vt->selEnd.chr  = selEnd - start;
				vt->selEnd.buf  = buf;
			}
		}

		buf->start[lines] = start - vt->buffer;
		buf->styles[lines] &= 0xff;
		lines ++;
		total ++;
	}
	vt->totalLines = total;
	vt->reformat = NULL;

	if (hasSel)
	{
		vt->selCur = vt->selStart;
		VTMarkSelection(vt);
	}

	fprintf(stderr, "total = %d\n", total);
}

static float nvgTextWithTabs(NVGcontext * vg, float x, float y, STRPTR start, STRPTR end, int tabSize)
{
	while (start < end)
	{
		STRPTR next;
		for (next = start; next < end && *next != '\t'; next ++);
		if (next > start)
			x = nvgText(vg, x, y, start, next);
		if (next < end)
			/* skip tab */
			x += tabSize - (int) x % tabSize, next ++;
		start = next;
	}
	return x;
}

static float nvgTextBoundsWithTabs(NVGcontext * vg, float x, STRPTR start, STRPTR end, int tabSize)
{
	while (start < end)
	{
		STRPTR next;
		for (next = start; next < end && *next != '\t'; next ++);
		if (next > start)
			x += nvgTextBounds(vg, x, 0, start, next, NULL);
		if (next == end) break;
		/* skip tab */
		x += tabSize - (int) x % tabSize, next ++;
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
	if (top < 0) top = 0;

	vt->topLine = top;
	vt->topOffset = offset;
}


static int VTPaint(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	VirtualTerm   vt    = ud;
	NVGcontext *  vg    = paint->nvg;
	VTLines       lines;

	if (vt->formatWidth != vt->width - vt->scrollPad)
	{
		/* redo line wrapping */
		vt->ctx = vg;
		vt->fontSize = paint->fontSize;
		vt->fontId = paint->fontId;
		vt->formatWidth = vt->width - vt->scrollPad;
		vt->tabSizePx = nvgTextBounds(vg, 0, 0, " ", NULL, NULL) * vt->tabSize;
		VTReformat(vt, vg);
		if (vt->topLine != vt->topTarget)
			VTAdjustTop(vt, vt->topTarget);
		VTAdjustScroll(vt);
	}

	float x = paint->x;
	float y = paint->y;
	float b = y + paint->h;
	int   i, max;

	y += vt->topOffset;
	for (lines = HEAD(vt->lines), i = vt->topLine; i > VT_LINES; NEXT(lines), i -= VT_LINES);

//	nvgScissor(vg, x, y, paint->w, paint->h);
	for (max = vt->totalLines - vt->topLine; max > 0 && y < b; )
	{
		uint16_t flags = lines->styles[i];
		DATA8    start = vt->buffer + lines->start[i], end;
		i ++; max --;
		if (i == VT_LINES)
			NEXT(lines), i = 0;
		end = vt->buffer + (max == 0 ? vt->bufUsage : lines->start[i]);
		if (end > start && end[-1] == '\n') end --;

		if (flags & VT_SELWHOLE)
		{
			/* line partially or entirely selected */
			uint16_t s, e;
			switch (flags & VT_SELWHOLE) {
			case VT_SELSTART: s = vt->colStart; e = end - start; break;
			case VT_SELEND:   s = 0; e = vt->colEnd; break;
			case VT_SELBOTH:  s = vt->colStart; e = vt->colEnd; break;
			case VT_SELWHOLE: s = 0; e = end - start;
			}
			float x2 = x;
			if (s > 0)
				x2 = nvgTextWithTabs(vg, x, y, start, start + s, vt->tabSizePx), start += s, e -= s, s = 0;
			if (e > 0)
			{
				NVGcolor fg;
				float w = nvgTextBoundsWithTabs(vg, x2, start, start + e, vt->tabSizePx) - x2;
				nvgGetCurTextColor(vg, &fg);
				nvgFillColorRGBA8(vg, vt->selColors + 4);
				nvgBeginPath(vg);
				nvgRect(vg, x2, y, w, vt->fontSize);
				nvgFill(vg);
				nvgFillColorRGBA8(vg, vt->selColors);
				x2 = nvgTextWithTabs(vg, x2, y, start, start + e, vt->tabSizePx);
				nvgFillColor(vg, fg);
				start += e;
			}
			if (start < end)
				nvgTextWithTabs(vg, x2, y, start, end, vt->tabSizePx);
		}
		else nvgTextWithTabs(vg, x, y, start, end, vt->tabSizePx);
		y += paint->fontSize;
	}

	return 1;
}

static void VTAddText(VirtualTerm vt, STRPTR text)
{
	int len = strlen(text);

	if (vt->bufUsage + len > vt->bufMax)
	{
		int   max    = (vt->bufUsage + len + VT_MASK) & ~VT_MASK;
		DATA8 buffer = realloc(vt->buffer, max);
		if (! buffer) return;
		vt->buffer = buffer;
		vt->bufMax = max;
	}

	DATA8 dest = vt->buffer + vt->bufUsage;
	memcpy(dest, text, len);
	vt->bufUsage += len;
	if (! vt->reformat || vt->reformat > dest)
		vt->reformat = dest;
}

static void VTDebugLines(VirtualTerm vt);

static int VTSetOrGet(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnVal * val = cd;
	VirtualTerm vt  = ud;
	int         arg;

	switch (val->stage) {
	case SITV_Get:
		break;
	case SITV_Set:
		switch (val->tag) {
		case VT_AddText:
			VTAddText(vt, SIT_GET(val, STRPTR));
			break;
		case VT_CursorX:
			arg = SIT_GET(val, int);
			if (arg < 0) arg = 0;
			vt->cx = arg;
			break;
		case VT_CursorY:
			arg = SIT_GET(val, int);
			if (arg < 0) arg = 0;
			vt->cy = arg;
			break;
		case VT_TabSize:
			arg = SIT_GET(val, int);
			if (arg < 2) arg = 2;
			if (arg > 100) arg = 100;
			vt->tabSize = arg;
			vt->formatWidth = 0;
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

static void VTGetCoord(VirtualTerm vt, VTCoord coord, int x, int y)
{
	NVGcontext * vg = vt->ctx;
	VTCoord_t    iter;
	DATA8        start, next;
	int          line = vt->topLine + (y + vt->topOffset) / vt->fontSize;

	if (line >= vt->totalLines)
		line  = vt->totalLines - 1;
	if (line < 0)
		line = 0;

	VTInitIter(vt, &iter, line);
	start = vt->buffer + iter.buf->start[iter.line];
	coord->buf = iter.buf;
	coord->line = iter.line;
	next = VTIterNext(vt, &iter);

	nvgFontFaceId(vg, vt->fontId);
	nvgFontSize(vg, vt->fontSize);
	nvgTextAlign(vg, NVG_ALIGN_TOP | NVG_ALIGN_LEFT);

	coord->chr = VTTextFit(vt, vg, x, start, next, True);
}

#define VTLINE_BUF(vt, coord)      ((vt)->buffer + (coord).buf->start[(coord).line] + (coord).chr)

static Bool VTMarkSelection(VirtualTerm vt)
{
	VTCoord_t iter;
	DATA8     cur   = VTLINE_BUF(vt, vt->selCur);
	DATA8     start = VTLINE_BUF(vt, vt->selStart);
	int       line, end;

	for (end = vt->selEnd.line, iter.buf = (VTLines) vt->selEnd.buf->node.ln_Prev; iter.buf; PREV(iter.buf), end += VT_LINES);
	VTInitIterLines(vt, &iter, &vt->selCur);
	line = iter.chr + iter.line;

	if (memcmp(&vt->selEnd, &vt->selStart, sizeof vt->selEnd) == 0)
	{
		/* move selection back to initial point: remove selection */
		vt->selCur = vt->selEnd;
		if (vt->hasSelect)
		{
			vt->hasSelect = 0;
			iter.buf->styles[iter.line] &= 0xff;
			return True;
		}
		return False;
	}

	/* move mouse over same character */
	if (memcmp(&vt->selEnd, &vt->selCur, sizeof vt->selEnd) == 0)
		return False;

	DATA16 style;
	if (line < end)
	{
		while (line < end)
		{
			style = iter.buf->styles + iter.line;
			if (cur < start) *style &= 0xff; /* clear selection */
			else             *style |= VT_SELWHOLE;
			cur = VTIterNext(vt, &iter);
			line ++;
		}
	}
	else while (line > end)
	{
		style = iter.buf->styles + iter.line;
		if (cur > start) *style &= 0xff; /* clear selection */
		else             *style |= VT_SELWHOLE;
		cur = VTIterPrev(vt, &iter);
		line --;
	}
	style = iter.buf->styles + iter.line;
	*style = (*style & 255) | (cur < start ? VT_SELSTART : VT_SELEND);

	style = vt->selStart.buf->styles + vt->selStart.line;
	cur = VTLINE_BUF(vt, vt->selEnd);
	VTInitIterLines(vt, &iter, &vt->selStart);
	iter.chr += iter.line;
	if (start < cur)
	{
		vt->colStart = vt->selStart.chr;
		vt->colEnd   = vt->selEnd.chr;
		iter.line = VT_SELSTART;
	}
	else
	{
		vt->colStart = vt->selEnd.chr;
		vt->colEnd   = vt->selStart.chr;
		iter.line = VT_SELEND;
	}
	*style = (*style & 0xff) | (line == iter.chr ? VT_SELSTART | VT_SELEND : iter.line);

	vt->hasSelect = 1;
	vt->selCur = vt->selEnd;
	return True;
}

static void VTDebugLines(VirtualTerm vt)
{
	VTCoord_t iter, start = {.buf = HEAD(vt->lines)};
	VTInitIterLines(vt, &iter, &start);

	DATA8 eof = vt->buffer + vt->bufUsage;
	DATA8 line = vt->buffer;
	int   i = 0;
	while (line < eof)
	{
		TEXT  buffer[16];
		DATA8 start = line;
		DATA8 sel   = "--";
		switch (iter.buf->styles[iter.line] & 0x700) {
		case VT_SELBOTH:  sel = "SE"; break;
		case VT_SELEND:   sel = "-E"; break;
		case VT_SELSTART: sel = "S-"; break;
		case VT_SELWHOLE: sel = "AA";
		}
		line = VTIterNext(vt, &iter);
		buffer[0] = 0;
		if (line-start > 10)
		{
			CopyString(buffer, start, 11);
			strcpy(buffer+10, "...");
		}
		else CopyString(buffer, start, line-start);
		fprintf(stderr, "%2d: [%2d] %s: %s\n", i, line - start, sel, buffer);
		i ++;
	}
}

static void VTClearSelection(VirtualTerm vt)
{
	VTCoord_t iter;
	DATA8 end = vt->buffer + vt->selEnd.buf->start[vt->selEnd.line] + vt->selEnd.chr;
	DATA8 start = vt->buffer + vt->selStart.buf->start[vt->selStart.line] + vt->selStart.chr;

	if (start > end)
	{
		DATA8 tmp = start;
		start = end;
		end = tmp;
		VTInitIterLines(vt, &iter, &vt->selEnd);
	}
	else VTInitIterLines(vt, &iter, &vt->selStart);

	while (start < end)
	{
		iter.buf->styles[iter.line] &= 0xff;
		start = VTIterNext(vt, &iter);
	}

	vt->hasSelect = 0;
}

static void VTSelectAll(VirtualTerm vt)
{
	VTCoord_t iter;
	VTCoord_t start = {.buf = HEAD(vt->lines)};
	DATA8     line  = vt->buffer;
	DATA8     eof   = vt->buffer + vt->bufUsage;

	vt->selStart = start;
	VTInitIterLines(vt, &iter, &start);
	while (line < eof)
	{
		DATA16 styles = &iter.buf->styles[iter.line];
		*styles = (*styles & 0xff) | VT_SELWHOLE;
		line = VTIterNext(vt, &iter);
	}
	vt->hasSelect = 1;
	vt->selEnd.buf = iter.buf;
	vt->selEnd.line = iter.line;
}

static int VTMouse(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm   vt  = ud;
	SIT_OnMouse * msg = cd;

	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			if (vt->hasSelect)
				VTClearSelection(vt), SIT_ForceRefresh();
			//fprintf(stderr, "pos: %d, %d\n", msg->x, msg->y);
			VTGetCoord(vt, &vt->selStart, msg->x, msg->y);
			vt->selCur = vt->selStart;
			return 2;
		}
		break;
	case SITOM_CaptureMove:
		//fprintf(stderr, "pos: %d, %d\n", msg->x, msg->y);
		VTGetCoord(vt, &vt->selEnd, msg->x, msg->y);
		if (VTMarkSelection(vt))
			SIT_ForceRefresh();
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

static int VTKeyboard(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;

	if (msg->utf8[0] == 1)
	{
		VTSelectAll(ud);
		SIT_ForceRefresh();
	}

	return 1;
}

static int VTResize(SIT_Widget w, APTR cd, APTR ud)
{
	VirtualTerm vt = ud;
	float *     sz = cd;
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
	VirtualTerm vt = ud;
	ListNode *  node;
	while ((node = ListRemHead(&vt->lines)))
		free(node);
	free(vt->buffer);
	free(vt);
	return 1;
}

void VTInit(SIT_Widget canvas, SIT_Widget scroll)
{
	VirtualTerm vt = calloc(sizeof *vt, 1);

	vt->canvas    = canvas;
	vt->scroll    = scroll;
	vt->maxLines  = 300;
	vt->maxBuffer = 64 * 1024;
	vt->tabSize   = 4;
	vt->palette   = VTDefPalette;
	vt->topTarget = 1e6;

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
