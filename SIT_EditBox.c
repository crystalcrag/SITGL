/*
 * SIT_EditBox.c : implementation for <input type=text|number|password> and <textarea>.
 *
 * Written by T.Pierron, apr 2020, original code taken from stb_textedit.h by Sean Barrett,
 *
 * Supported features: tab character, word/char wrapping, selection, undo, redo, scrolling, text-align:
 * left/right/center/justify, insert/replace, utf-8.
 *
 * Not supported: EOL other than \n, malformed utf-8, RtoL writings.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <limits.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	struct TagList_t EditBoxClass[] = {
		{ "editType",    SIT_EditType,    C__, SIT_U8,   OFFSET(SIT_EditBox, editType) },
		{ "readOnly",    SIT_ReadOnly,    _SG, SIT_BOOL, OFFSET(SIT_EditBox, readOnly) },
		{ "startSel",    SIT_StartSel,    _SG, SIT_INT,  OFFSET(SIT_EditBox, selStart) },
		{ "endSel",      SIT_EndSel,      _SG, SIT_INT,  OFFSET(SIT_EditBox, selEnd) },
		{ "minValue",    SIT_MinValue,    _SG, SIT_REAL, OFFSET(SIT_EditBox, minValue) },
		{ "maxValue",    SIT_MaxValue,    _SG, SIT_REAL, OFFSET(SIT_EditBox, maxValue) },
		{ "stepValue",   SIT_StepValue,   _SG, SIT_REAL, OFFSET(SIT_EditBox, stepValue) },
		{ "curValue",    SIT_CurValue,    _SG, SIT_PTR,  OFFSET(SIT_EditBox, curValue) },
		{ "placeHolder", SIT_PlaceHolder, _SG, SIT_STR,  OFFSET(SIT_EditBox, cueBanner) },
		{ "editBuffer",  SIT_EditBuffer,  C__, SIT_PTR,  OFFSET(SIT_EditBox, fixedBuffer) },
		{ "editLength",  SIT_EditLength,  C__, SIT_INT,  OFFSET(SIT_EditBox, fixedSize) },
		{ "maxUndo",     SIT_MaxUndo,     C__, SIT_INT,  OFFSET(SIT_EditBox, undoSize) },
		{ "maxLines",    SIT_MaxLines,    C__, SIT_INT,  OFFSET(SIT_EditBox, maxLines) },
		{ "wordWrap",    SIT_WordWrap,    C__, SIT_U8,   OFFSET(SIT_EditBox, wordWrap) },
		{ "tabStyle",    SIT_TabStyle,    _SG, SIT_U8,   OFFSET(SIT_EditBox, tabStyle) },
		{ "roundTo",     SIT_RoundTo,     _SG, SIT_U8,   OFFSET(SIT_EditBox, roundTo) },
		{ "colorMap",    SIT_ColorMap,    _SG, SIT_PTR,  OFFSET(SIT_EditBox, colorMap) },
		{ "lexer",       SIT_Lexer,       _SG, SIT_PTR,  OFFSET(SIT_EditBox, lexer) },
		{ "lexerData",   SIT_LexerData,   _SG, SIT_PTR,  OFFSET(SIT_EditBox, lexerData) },
		{ "caretStyle",  SIT_CaretStyle,  _SG, SIT_U8,   OFFSET(SIT_EditBox, caretMode) },
		{ "editAddText", SIT_EditAddText, _S_, SIT_PTR,  0 },
		{ NULL,          SIT_TagEnd }
	};

	enum
	{
		FLAG_REPLACE   = 0x01,
		FLAG_HASSCROLL = 0x02,
		FLAG_FIXEDSIZE = 0x04,
		FLAG_FIXEDUNDO = 0x08,
		FLAG_SETSTART  = 0x10,
		FLAG_SETEND    = 0x20,
		FLAG_CLEARCLIP = 0x40,
		FLAG_IGNOREEVT = 0x80
	};

static uint32_t lastClick;

static int SIT_MeasureEditBox(SIT_Widget w, APTR cd, APTR unused)
{
	STRPTR old = w->title;
	SizeF * pref = cd;
	SizeF   ret;
	/* size has to be explicitely set */
	w->title = "X";
	layoutMeasureWords(w, &ret);
	w->title = old;
	ret.width  += w->padding[0] + w->padding[2];
	ret.height += w->padding[1] + w->padding[3];
	if (pref->width  < ret.width)  pref->width  = ret.width;
	if (pref->height < ret.height) pref->height = ret.height;
	return 0;
}

#define INVALID_STEP    INT_MAX

static int  SIT_TextEditRender(SIT_Widget, APTR, APTR);
static int  SIT_TextEditClick(SIT_Widget, APTR, APTR);
static int  SIT_TextEditDrag(SIT_Widget, APTR, APTR);
static int  SIT_TextEditInsertChars(SIT_EditBox, int pos, char * text, int len);
static void SIT_TextEditAdjustScroll(SIT_EditBox);
static void SIT_TextEditUndoRedo(SIT_EditBox, int redo);
static void SIT_TextEditRegUndo(SIT_EditBox, int where, int length, int type);
static int  SIT_TextEditPaste(SIT_EditBox, DATA8 text, int len);
static void SIT_TextEditMoveCursorUpOrDown(SIT_EditBox, int dir, int sel);
static void SIT_TextEditMakeCursorVisible(SIT_EditBox);


/* trigger user callback when something has changed */
static void SIT_TextEditNotify(SIT_Widget w)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	if (edit->caretMode & SITV_CaretNotify)
	{
		DOMRow row;
		/* stat: 0,1 = absolute cursor pos, 2,3 = pos with soft lines, 4,5 = total lines/with soft breaks, 6 = pos in bytes, 7 = buffer bytes, 8 = has changes */
		int stat[9], line, chr, max = edit->rowCount - 1;
		for (line = edit->rowTop, chr = edit->charTop, row = edit->rows + line;
			 line < max && chr + row->bytes <= edit->cursor;
			 chr += row->bytes, row ++, line ++);

		stat[0] = stat[2] = edit->cursor - chr;
		stat[1] = stat[3] = line;
		stat[4] =
		stat[5] = edit->rowCount;
		stat[6] = edit->cursor;
		stat[7] = edit->length;
		stat[8] = edit->undoCount;
		SIT_ApplyCallback(w, stat, SITE_OnChange);
	}
	else SIT_ApplyCallback(w, edit->text, SITE_OnChange);
}

/* SITE_OnResize: redo word wrapping */
static int SIT_TextEditResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox state = (SIT_EditBox) w;

	if (w->style.lineHeight != AUTOVAL)
	{
		state->fh = ToPoints(NULL, w, w->style.lineHeight & ~3, 0 /* only needed for relative value */);

		/* style.lineHeight is only the minimum required - relative value are relative to font size not parent box */
		switch (w->style.lineHeight & 3) {
		case 1: state->fh *= w->style.font.size; break; /* em */
		case 2: state->fh *= w->style.font.size * 0.01f; break; /* % */
		}
	}
	else state->fh = w->style.font.size;

	state->rowVisible = w->layout.pos.height / state->fh;
	state->width = w->layout.pos.width;

	if (state->editType != SITV_Multiline)
	{
		/* extend selection background a bit for single line edit */
		state->extendT = MIN(w->layout.padding.top, 2);
		state->extendB = MIN(w->layout.padding.bottom, 2) + state->extendT;
	}

	state->fh = roundf(state->fh);
	state->padLineY = roundf((state->fh - w->style.font.size) * 0.5f); /* for selection */

	if (state->flags & FLAG_HASSCROLL)
		state->width -= state->scrollPad;
	if (state->formatWidth != state->width)
	{
		if (state->length > 0 && state->wordWrap)
		{
			state->formatWidth = state->width;
			SIT_TextEditInsertChars(state, 0, NULL, -1);
		}
		if (state->caretMode & SITV_CaretNotify)
			SIT_TextEditNotify(w);
	}
	SIT_TextEditAdjustScroll(state);
	return 1;
}

/* scrollbar just added: redo word wrapping */
static int SIT_TextEditResizeSB(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox state = (SIT_EditBox) w->parent;
	REAL * v = &w->box.left;
	state->scrollPad = v[2] - v[0];
	state->width = state->super.layout.pos.width - state->scrollPad;
	if (state->length > 0 && state->wordWrap && state->formatWidth != state->width)
	{
		state->charTop = 0;
		state->formatWidth = state->width;
		SIT_TextEditInsertChars(state, 0, NULL, -1);
		/* reset charTop from rowTop */
		DOMRow row;
		int    top, i;
		for (i = state->rowTop, top = 0, row = state->rows; i > 0; top += row->bytes, i --, row ++);
		state->charTop = top;
	}
	return 0;
}

static int SIT_TextEditFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	if (edit->editType == SITV_Password)
	{
		if (edit->flags & FLAG_CLEARCLIP)
		{
			memset(edit->text, ' ', edit->length);
			/* first, overwrite with blank, then clear content */
			SIT_CopyToClipboard(edit->text, -1);
			SIT_CopyToClipboard("", -1);
		}
		/* prevent password being kept in unused memory chunks */
		memset(edit->text, 0, edit->maxText);
	}
	if (edit->caretBlink)
		SIT_ActionReschedule(edit->caretBlink, -1, -1);

	if (edit->colorMap)
		free(edit->colorMap);
	if ((edit->flags & FLAG_FIXEDUNDO) == 0)
		free(edit->undoBuffer);
	if ((edit->flags & FLAG_FIXEDSIZE) == 0)
		free(edit->text);
	if (edit->maxLines == 0)
		free(edit->rows);
	free(edit->fixedMem);
	return 1;
}

static int SIT_TextEditSetCursor(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	switch (edit->flags & (FLAG_SETEND | FLAG_SETSTART)) {
	case 0:             return 0;
	case FLAG_SETEND:   edit->cursor = edit->selStart = edit->selEnd; break;
	case FLAG_SETSTART: edit->cursor = edit->selEnd = edit->selStart; break;
	default:            edit->cursor = edit->selEnd;
	}
	edit->flags &= ~(FLAG_SETEND | FLAG_SETSTART);
	SIT_TextEditMakeCursorVisible(edit);
	return 1;
}

static int SIT_TextEditSetValue(SIT_Widget w, APTR call_data, APTR user_data)
{
	SIT_Variant val  = user_data;
	SIT_EditBox edit = (SIT_EditBox) w;
	TagList     tag  = call_data;
	int *       cur  = &edit->selStart;
	int         pos;

	switch (tag->tl_TagID) {
	case SIT_EndSel: cur ++;
	case SIT_StartSel:
		pos = val->integer;
		if (pos < 0) pos = 0;
		if (pos > edit->length) pos = edit->length;
//		if (pos != *cur)
		{
			*cur = pos;
			edit->flags |= tag->tl_TagID == SIT_EndSel ? FLAG_SETEND : FLAG_SETSTART;
			w->postProcess = SIT_TextEditSetCursor;
		}
		break;
	case SIT_RoundTo:
		pos = val->integer;
		if (pos < 0)  pos = 0;
		if (pos > 16) pos = 16;
		edit->roundTo = pos;
		break;
	case SIT_ColorMap:
		if (val->pointer)
		{
			DATA8 p, end;
			if (edit->colorMap == NULL)
				edit->colorMap = calloc(9, 28);
			for (p = edit->colorMap, end = p + 9 * 28; p < end; p[8] = 0xfc, p += 9);
			p = val->pointer;
			memcpy(edit->colorMap, p + 1, 9 * MIN(p[0], 27));
		}
		else if (edit->colorMap)
		{
			free(edit->colorMap);
			edit->colorMap = NULL;
		}
		break;
	case SIT_EditAddText: /* insert text at current cursor location */
		if (val->pointer)
			SIT_TextEditPaste(edit, val->pointer, strlen(val->pointer));
		break;
	default:
		SIT_SetWidgetValue(w, call_data, user_data);
	}
	return 1;
}

/* damn floatting point formatting */
static int SIT_TextEditFormatDouble(STRPTR buffer, int max, double val, int roundTo)
{
	if (fabs(val) < 1e16)
	{
		/* all of this is to make sure we don't lose precision when displaying number, while not printing garbage either */
		char * p = buffer;
		int    n, digit;
		snprintf(buffer, max, "%.*f", roundTo, val);
		if (*p == '-') p ++;
		for (digit = 0; *p && *p != '.'; p ++, digit ++);
		/* assume 16 significant decimal digits for a double */
		if (roundTo == 16)
		{
			n = 16 - digit;
			while (n > 0 && *p) p ++, n --;
			if (*p && *p >= '5')
			{
				/* argh need rounding from "%f" (e.g. with num -10766.5627312075) */
				p = buffer + snprintf(buffer, max, "%.*f", 16-digit, val);
			}
			else *p = 0;
		}
		else p = strchr(buffer, 0);
		for (p --; p >= buffer && *p == '0'; *p-- = 0);
		if (p >= buffer && *p == '.') *p = 0;
		return p - buffer;
	}
	else return snprintf(buffer, max, "%.0f", val);
}

/* check that number given by user is sane */
static void SIT_TextEditCheckNumber(SIT_EditBox edit, STRPTR buf)
{
	STRPTR eof;
	switch (edit->editType) {
	case SITV_Integer: edit->value.ref = round(strtod(buf, &eof)); break;
	case SITV_Float:
	case SITV_Double:  edit->value.ref = strtod(buf, &eof); break;
	default: return;
	}
	if (*eof) *eof = 0, edit->length = eof - buf;
	edit->value.step = 0;
	if (edit->value.ref < edit->minValue)
		edit->length = SIT_TextEditFormatDouble(edit->text, edit->maxText, edit->value.ref = edit->minValue, edit->roundTo);
	else if (edit->value.ref > edit->maxValue)
		edit->length = SIT_TextEditFormatDouble(edit->text, edit->maxText, edit->value.ref = edit->maxValue, edit->roundTo);

	if (edit->curValue)
	{
		/* int and float conversion will be lossless */
		switch (edit->editType) {
		case SITV_Integer: * (int *)    edit->curValue = edit->value.ref; break;
		case SITV_Float:   * (float *)  edit->curValue = edit->value.ref; break;
		case SITV_Double:  * (double *) edit->curValue = edit->value.ref;
		}
	}
}

static int SIT_TextEditSyncValue(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	if (edit->flags & FLAG_IGNOREEVT)
	{
		/* OnChange event is comming from spinner: curValue is already in sync */
		edit->flags ^= FLAG_IGNOREEVT;
		return 0;
	}
	switch (edit->editType) {
	case SITV_Integer: edit->value.ref = round(strtod(edit->text, NULL)); break;
	case SITV_Float:
	case SITV_Double:  edit->value.ref = strtod(edit->text, NULL); break;
	default: return 0;
	}

	if (edit->curValue)
	{
		double cur = edit->value.ref;
		if (cur < edit->minValue) cur = edit->minValue; else
		if (cur > edit->maxValue) cur = edit->maxValue;
		/* int and float conversion will be lossless */
		switch (edit->editType) {
		case SITV_Integer: * (int *)    edit->curValue = cur; break;
		case SITV_Float:   * (float *)  edit->curValue = cur; break;
		case SITV_Double:  * (double *) edit->curValue = cur;
		}
	}
	return 0;
}

/* click on number inc/dec button */
static int SIT_TextEditSpinnerClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg  = cd;
	SIT_EditBox   edit = (SIT_EditBox) w->parent;
	double        step = edit->stepValue;

	if (msg == (APTR) 1)
		msg = NULL;

	if (msg == NULL || msg->state == SITOM_ButtonPressed)
	{
		int changes = 0;
		if (step == 0) step = 1;
		if (edit->value.step == INVALID_STEP)
		{
			changes = edit->length;
			SIT_TextEditCheckNumber(edit, edit->text);
			if (changes == edit->length)
				changes = 0;
		}

		/* all that effort to prevent marginal error accumulation :-/ */
		int    off = edit->value.step + (int) ud - 1;
		double val = edit->value.ref + off * step;
		if (val < edit->minValue) val = edit->minValue;
		if (val > edit->maxValue) val = edit->maxValue;

		sit.dirty = True;
		if (val != edit->value.ref)
		{
			edit->value.ref = val;
			edit->value.step = off;
			edit->cursor = 1e6;
			SIT_TextEditFormatDouble(edit->text, edit->maxText, val, edit->roundTo);
			SIT_TextEditSetText(&edit->super, edit->text);
			if (msg && edit->autoScroll == NULL)
				edit->autoScroll = SIT_ActionAdd(w, sit.curTime + 500, -1, SIT_TextEditSpinnerClick, ud);
			if (edit->curValue)
			{
				/* int and float conversion will be lossless */
				switch (edit->editType) {
				case SITV_Integer: * (int *)    edit->curValue = val; break;
				case SITV_Float:   * (float *)  edit->curValue = val; break;
				case SITV_Double:  * (double *) edit->curValue = val;
				}
			}
			edit->flags |= FLAG_IGNOREEVT;
			SIT_TextEditNotify(&edit->super);
			return 50;
		}
		else if (changes)
		{
			/* garbage from input removed usually */
			SIT_TextEditSetText(&edit->super, edit->text);
			edit->flags |= FLAG_IGNOREEVT;
			SIT_TextEditNotify(&edit->super);
		}
	}
	else if (msg->state == SITOM_ButtonReleased)
	{
		if (edit->autoScroll)
			SIT_ActionReschedule(edit->autoScroll, -1, -1), edit->autoScroll = NULL;
	}
	return 0;
}

static int SIT_TextEditBlinkCaret(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	edit->caretVisible ^= 1;
	sit.dirty = True;
	return sit.caretBlinkMS;
}

static void SIT_TextEditRefreshCaret(SIT_EditBox edit)
{
	edit->caretVisible = 1;
	if (edit->caretBlink)
		SIT_ActionReschedule(edit->caretBlink, sit.curTime + sit.caretBlinkMS, sit.curTime + 1e6);
}

/* SITE_OnBlur/SITE_OnFocus */
static int SIT_TextEditDirty(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	if (edit->editType != SITV_Multiline)
	{
		switch ((int) cd) {
		case 1:
			/* got focus by kbd: place caret at end and select all */
			SIT_TextEditKey(edit, SITK_End);
			SIT_TextEditKey(edit, 1);
			break;
		case 0:
			/* lost focus: deselect all */
			lastClick = 0;
			edit->selStart = edit->selEnd = edit->cursor;
		}
	}
	SIT_Action act = edit->caretBlink;
	if (cd)
	{
		edit->caretVisible = 1;
		if (act)
			SIT_TextEditRefreshCaret(edit);
		else if (edit->caretMode & SITV_CaretBlink)
			edit->caretBlink = SIT_ActionAdd(w, sit.curTime + sit.caretBlinkMS, sit.curTime + 1e6, SIT_TextEditBlinkCaret, NULL);
	}
	else if (act)
	{
		SIT_ActionReschedule(act, -1, -1);
		edit->caretBlink = NULL;
	}

	sit.dirty = True;
	return 1;
}

Bool SIT_InitEditBox(SIT_Widget w, va_list args)
{
	int fixedMem = 0;
	SIT_EditBox edit = (SIT_EditBox) w;

	w->optimalWidth = SIT_MeasureEditBox;
	w->render       = SIT_TextEditRender;
	w->finalize     = SIT_TextEditFinalize;
	w->setValue     = SIT_TextEditSetValue;
	w->flags        = SITF_RenderChildren;
	edit->editType  = SITV_TextBox;
	edit->tabStyle  = -1;
	edit->undoSize  = -1;
	edit->stepValue =  1;
	edit->roundTo   =  ((SIT_App)sit.root)->defRoundTo;
	edit->maxValue  =  1/0.; /* + INF */
	edit->minValue  = -1/0.; /* - INF */
	edit->wordWrap  = SITV_WWWord;
	edit->caretMode = SITV_CaretIBeam | SITV_CaretBlink;

	SIT_ParseTags(w, args, w->attrs = EditBoxClass);
	SIT_AddCallback(w, SITE_OnResize,    SIT_TextEditResize, NULL);
	SIT_AddCallback(w, SITE_OnClick,     SIT_TextEditClick, NULL);
	SIT_AddCallback(w, SITE_OnMouseMove, SIT_TextEditDrag, NULL);
	SIT_AddCallback(w, SITE_OnBlur,      SIT_TextEditDirty, NULL);
	SIT_AddCallback(w, SITE_OnFocus,     SIT_TextEditDirty, NULL);

	if (edit->editType >= SITV_Integer && edit->curValue)
		SIT_AddCallback(w, SITE_OnChange + EVT_PRIORITY(100), SIT_TextEditSyncValue, NULL);

	if (edit->tabStyle < 0)
		edit->tabStyle = edit->editType == SITV_Multiline ? SITV_TabEditNormal : SITV_TabEditForbid;

	if (edit->editType >= SITV_Password)
	{
		/* no need to alloc more for a number or a password */
		if (! edit->fixedBuffer)
			edit->fixedSize = 128;
		/* disable undo: will prevent spreading password all over the memory */
		edit->undoSize = edit->editType == SITV_Password ? 0 : 128;
		edit->flags |= FLAG_FIXEDUNDO|FLAG_FIXEDSIZE;
	}
	if (edit->editType == SITV_Integer)
	{
		/* clamp to signed 32bit int */
		if (isinf(edit->minValue))
			edit->minValue = INT_MIN;
		if (isinf(edit->maxValue))
			edit->maxValue = INT_MAX;
		edit->stepValue = round(edit->stepValue);
	}
	/* buffer provided by user: don't alloc anything here */
	if (edit->fixedSize > 0)
	{
		DATA8 fixed = edit->fixedBuffer;
		edit->flags |= FLAG_FIXEDSIZE;
		edit->maxText = edit->fixedSize;
		if (fixed)
			edit->text = fixed;
		else
			fixedMem += edit->fixedSize;
	}

	if (edit->editType != SITV_Multiline)
	{
		edit->maxLines = 1;
		edit->wordWrap = SITV_WWNone;
	}
	else if (edit->maxLines == 0)
	{
		/* will grow as necessary */
		edit->rows = calloc(sizeof *edit->rows, 16);
		edit->rowMax = 16;
	}
	if (edit->maxLines > 0)
	{
		fixedMem += sizeof *edit->rows * edit->maxLines;
		edit->rowMax = edit->maxLines;
	}
	if (edit->undoSize >= 0)
	{
		if (edit->undoSize < 16)
			edit->undoSize = 0; /* need more room than that to be useful */
		fixedMem += edit->undoSize;
		edit->flags |= FLAG_FIXEDUNDO;
	}
	else edit->undoSize = 0;

	if (fixedMem > 0)
	{
		DATA8 mem = edit->fixedMem = malloc(fixedMem);
		if (edit->maxLines > 0)
		{
			int sz = sizeof *edit->rows * edit->maxLines;
			edit->rows = (DOMRow) mem;
			memset(mem, 0, sz);
			mem += sz;
		}
		if (edit->text == NULL && (edit->flags & FLAG_FIXEDSIZE))
		{
			edit->maxText = edit->fixedSize;
			edit->text = mem;
			mem[0] = 0;
			mem += edit->maxText;
		}
		if (edit->flags & FLAG_FIXEDUNDO)
		{
			edit->undoBuffer = mem;
		}
	}

	layoutCalcBox(w);
	STRPTR init = edit->fixedBuffer ? edit->fixedBuffer : w->title;
	SizeF  ret;
	w->title = " ";
	layoutMeasureWords(w, &ret);
	w->title = NULL;
	edit->rowCount = 1;
	edit->tabSize = w->style.tabSize * ret.width;

	if (edit->editType >= SITV_Integer)
	{
		/* add spinner buttons */
		edit->spinnerUp = SIT_CreateWidget("#spin.up", SIT_BUTTON, w,
			SIT_NextCtrl, NULL,
			SIT_Right,    SITV_AttachForm, 0, SITV_NoPad,
			SIT_Top,      SITV_AttachForm, 0, SITV_NoPad,
			SIT_Width,    SITV_Em(0.9),
			SIT_Bottom,   SITV_AttachPosition, SITV_AttachPos(50), 0,
			NULL
		);
		edit->spinnerDown = SIT_CreateWidget("#spin.down", SIT_BUTTON, w,
			SIT_NextCtrl, NULL,
			SIT_Right,    SITV_AttachForm, 0, SITV_NoPad,
			SIT_Bottom,   SITV_AttachForm, 0, SITV_NoPad,
			SIT_Width,    SITV_Em(0.9),
			SIT_Top,      SITV_AttachPosition, SITV_AttachPos(50), 0,
			NULL
		);
		SIT_AddCallback(edit->spinnerUp,   SITE_OnClick, SIT_TextEditSpinnerClick, (APTR) 2);
		SIT_AddCallback(edit->spinnerDown, SITE_OnClick, SIT_TextEditSpinnerClick, NULL);

		if (edit->curValue)
		{
			switch (edit->editType) {
			case SITV_Integer: edit->value.ref = * (int    *) edit->curValue; break;
			case SITV_Float:   edit->value.ref = * (float  *) edit->curValue; break;
			case SITV_Double:  edit->value.ref = * (double *) edit->curValue;
			}
			SIT_TextEditFormatDouble(edit->text, edit->maxText, edit->value.ref, edit->roundTo);
			init = edit->text;
		}
	}

	/* init (w->title) is not malloc for SIT_EDITBOX (see SIT_Widget.c:SIT_ParseTags()) */
	if (IsDef(init))
		SIT_TextEditPaste(edit, init, -1);

	return True;
}

/* SIT_Title property set at runtime */
void SIT_TextEditSetText(SIT_Widget w, STRPTR title)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	if (edit->rowCount > 0)
	{
		/* try to keep cursor pos */
		int pos = edit->cursor;
		edit->rowCount = 1;
		memset(edit->rows, 0, sizeof *edit->rows);
		/* clear undo/redo */
		edit->undoCount = edit->redoCount = 0;
		edit->undoLast = edit->redoLast = NULL;
		/* cursor */
		edit->hasPreferredX = 0;
		edit->preferredX = 0;
		edit->cursor = edit->selStart = edit->selEnd = 0;
		edit->rowTop = edit->charTop = 0;
		edit->xpos = 0;
		edit->ypos = 0;
		/* text */
		edit->length = 0;
		if (edit->text)
			edit->text[0] = 0;
		if (title == NULL && edit->curValue && edit->editType >= SITV_Integer)
		{
			/* value changed from outside: refresh text */
			double val;
			switch (edit->editType) {
			case SITV_Integer: val = * (int *)    edit->curValue; break;
			case SITV_Float:   val = * (float *)  edit->curValue; break;
			default:           val = * (double *) edit->curValue;
			}
			SIT_TextEditFormatDouble(edit->text, edit->maxText, val, edit->roundTo);
			SIT_TextEditPaste(edit, edit->text, strlen(edit->text));
		}
		else SIT_TextEditPaste(edit, title ? title : "", -1);
		if (pos > edit->length) pos = edit->length;
		edit->cursor = edit->selStart = edit->selEnd = pos;
		if (w->layout.pos.width > 0)
		{
			SIT_TextEditMakeCursorVisible(edit);
			SIT_TextEditAdjustScroll(edit);
		}
		if (edit->editType >= SITV_Integer)
			edit->value.step = INVALID_STEP;
		SIT_ForceRefresh();
	}
	else w->title = title; /* not yet initialized */
}

/* get text as it is wordwrapped on screen */
DLLIMP int SIT_TextGetWithSoftline(SIT_Widget w, STRPTR buffer, int max)
{
	SIT_EditBox edit = (SIT_EditBox) w;
	DOMRow      row;
	DATA8       s, d;
	int         count, length;

	if (w->type != SIT_EDITBOX)
		return 0;

	for (row = edit->rows, count = edit->rowCount, s = edit->text, d = buffer, length = 0; count > 0; row ++, count --)
	{
		int len = MIN(max, row->bytes);
		memcpy(d, s, len);
		max -= len;
		d += len;
		length += row->bytes;
		s += row->bytes;
		if (row->bytes > 0 && s[-1] != '\n' && count > 1)
		{
			length ++;
			if (max > 0) *d ++ = '\n', max --;
		}
	}
	length ++;
	if (max > 0) *d = 0; else
	if (d > (DATA8) buffer) d[-1] = 0;
	return length;
}

DLLIMP int SIT_TextEditLineLength(SIT_Widget w, int line)
{
	if (w == NULL || w->type != SIT_EDITBOX) return 0;
	SIT_EditBox edit = (SIT_EditBox) w;
	if (line >= edit->rowCount) return 0;
	return edit->rows[line].bytes;
}

/*
 * TextEdit implementation
 */

#define UNDOSIZE           128
#define ROUND(length)      ((length+255) & ~255)
#define HASFLAG(flag)      (state->flags & flag)
#define SEL_INDEX_CODE     255

enum /* undo opcode */
{
	UNDO_INSERT,
	UNDO_DELETE,
	UNDO_REPLACE,
	UNDO_NOMERGE = 0x80
};

enum
{
	CHR, SEP, SPC = ' '
};

/* ASCII control characters textual display (limited to 2 chars) */
static uint8_t chrCodes[] = "0001020304050607BS09LFVTFFCR0E0F101112131415161718191A1B1C1D1E1F";
static uint8_t utf8Next[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4};
static uint8_t ASCIIclass[] = { /* quick'n dirty ASCII classifier for chr 0 ~ 127 */
	SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, ' ', ' ', SEP, SEP, ' ', SEP, SEP,
	SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP,
	' ', SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP, SEP,
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   SEP, SEP, SEP, SEP, SEP, SEP,
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   SEP, SEP, SEP, SEP, 0,
	SEP, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   SEP, SEP, SEP, SEP, SEP
};

#define NEXTCHAR(str)    (str + utf8Next[*(str) >> 4])

static inline int SIT_IsSeprator(uint8_t chr)
{
	return chr < 128 && ASCIIclass[chr] != 0;
}

static inline int SIT_IsSpace(uint8_t chr)
{
	return chr < 128 && ASCIIclass[chr] == SPC;
}

static inline int SIT_ChrClass(uint8_t chr)
{
	return chr < 128 ? ASCIIclass[chr] : 0;
}

/* try to fit at most <max> chars in <maxwidth>, taking tab into account */
static int SIT_TextEditBreakAt(SIT_EditBox state, DATA8 text, int max, REAL maxwidth, REAL * xpos, Bool round, DOMRow expand)
{
	DATA8 s, e, p;
	REAL  i, width;
	APTR  vg = sit.nvgCtx;
	int   spc, total;
	char  chr = 32;
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFontFaceId(vg, state->super.style.font.handle);
	nvgFontSize(vg, state->super.style.font.size);
	nvgTextLetterSpacing(vg, 0);
	if (state->super.style.text.align == TextAlignJustify && expand && expand->px < 65535)
	{
		chr = 33;
		for (s = text, e = s + expand->bytes, spc = 0; s < e; s ++)
			if (SIT_IsSpace(*s)) spc ++;
		total = state->width - (expand->px - spc * state->space.length);
		if (SIT_IsSpace(s[-1])) spc --;
	}
	else spc = total = 0;

	if (state->editType == SITV_Password)
	{
		i = (state->fh - state->padLineY * 2) * 0.5f;
		total = round ? roundf(maxwidth / i) : maxwidth / i;
		if (total > max) total = max;
		if (xpos) *xpos = total * i;
		return total;
	}
	else for (s = p = text, e = s + max, i = 0; s < e; s = NEXTCHAR(s))
	{
		if (*s < chr)
		{
			if (p < s)
			{
				int nb = nvgTextFit(vg, p, s, maxwidth - i, &width);
				p += nb;
				i += width;
				if (p < s) break;
			}
			if (*s == ' ')
			{
				if (spc == 0) continue;
				/* distribute space */
				width = total / spc;
				total -= width;
				spc --;
			}
			else if (*s != '\t')
			{
				DATA8 code = chrCodes + (*s<<1);
				width = nvgTextBounds(vg, 0, 0, code, code + 2, NULL);
			}
			else width = state->tabSize - fmodf(i, state->tabSize);
			if (i + width > maxwidth)
			{
				if (round && i + (width * 0.5f) < maxwidth) i += width, p = s + 1;
				break;
			}
			i += width;
			p = s + 1;
		}
	}
	if (p < s)
	{
		p += nvgTextFit(vg, p, s, maxwidth - i, &width);
		i += width;
		if (round && p < e && *p != '\n')
		{
			int nb = utf8Next[*p>>4];
			width = nvgTextBounds(vg, 0, 0, p, p+nb, NULL);
			if (i + width * 0.5f < maxwidth) p += nb, i += width;
		}
	}
	if (xpos) *xpos = i;
	return p - text;
}

/* need to take care of tab and ctrl characters */
static REAL SIT_TextEditRenderLine(SIT_EditBox state, DATA8 str, int length, REAL x, REAL y, Bool sel, DATA8 lexer)
{
	DATA8 end  = str + length, p;
	REAL  off  = state->offsetAlign, w;
	REAL  offy = state->padLineY;
	APTR  vg   = sit.nvgCtx;
	char  chr  = 32;

	if (state->super.style.text.align == TextAlignJustify && state->space.count > 0)
	{
		/* justify text */
		chr = 33;
	}
	if (state->editType == SITV_Password)
	{
		/* password entry: simplified rendering */
		w = (state->fh - state->padLineY * 2) * 0.5f;
		if (sel)
		{
			REAL sz = length * w;
			nvgFillColorRGBA8(vg, state->bgSel.rgba);
			nvgBeginPath(vg);
			nvgRect(vg, x + off, y-1-state->extendT, sz, state->fh+state->extendB);
			nvgFill(vg);
			nvgFillColorRGBA8(vg, state->super.style.fgSel.rgba);
		}
		else nvgFillColorRGBA8(vg, state->super.style.color.rgba);
		off += w*0.5f;
		y += (state->fh - w) * 0.5f + w*0.5f;
		for (p = str; p < end; p++, x += w)
		{
			nvgBeginPath(vg);
			nvgCircle(vg, x + off, y, w*0.4f);
			nvgFill(vg);
		}
		return x;
	}

	for (p = str; p < end; str = ++ p)
	{
		uint8_t code, attr;
		if (lexer)
			for (code = lexer[0]; p < end && *p >= chr && code == lexer[0]; p ++, lexer ++);
		else
			for (code = 0; p < end && *p >= chr; p ++);

		attr = 0;
		if (p > str)
		{
			/* normal text */
			if (sel || code > 0 || lexer)
			{
				DATA8 cmap, bgcol;
				if (sel || code == SEL_INDEX_CODE)
					cmap = state->super.style.fgSel.rgba, bgcol = state->bgSel.rgba;
				else
					cmap = state->colorMap + code * 9, bgcol = cmap + 4, attr = cmap[8];
				if (cmap[7] > 0) /* background color */
				{
					w = nvgTextBounds(vg, 0, 0, str, p, NULL);
					nvgFillColorRGBA8(vg, bgcol);
					nvgBeginPath(vg);
					nvgRect(vg, x + off, y-1-state->extendT, w, state->fh+state->extendB);
					nvgFill(vg);
				}
				else if (cmap[3] == 0)
				{
					/* bg and fg and set to transparent */
					goto fill_normal;
				}
				nvgFillColorRGBA8(vg, cmap[3] > 0 ? cmap : state->super.style.color.rgba);
			}
			else
			{
				fill_normal:
				if (state->super.style.shadowCount > 0)
				{
					/* no inherited shadow */
					TextShadow txtShadow = state->super.style.shadow;
					int count = state->super.style.shadowCount;

					/* shadow are ordered top to bottom in CSS (like multiple background) */
					for (; count > 0; count --, txtShadow ++)
					{
						nvgFontBlur(vg, txtShadow->blurFloat);
						nvgFillColorRGBA8(vg, txtShadow->color.rgba);
						nvgText(vg, x + off + txtShadow->pos.XYfloat[0], y + offy + txtShadow->pos.XYfloat[1], str, p);
					}
					nvgFontBlur(vg, 0);
				}
				nvgFillColorRGBA8(vg, state->super.style.color.rgba);
			}
			/* handle underline style */
			if (attr)
			{
				REAL x2 = nvgText(vg, x + off, y + offy, str, p) - off;
				if (attr & 1) nvgText(vg, x + off + 1, y + offy, str, p);

				if (attr & 2)
				{
					NVGcolor fg;
					REAL thick = state->super.style.font.size * (1/12.f);
					REAL liney = y + offy + state->super.style.font.size - thick * 0.75f;
					nvgGetCurTextColor(vg, &fg);
					nvgStrokeWidth(vg, thick);
					nvgStrokeColor(vg, fg);
					nvgBeginPath(vg);
					nvgMoveTo(vg, x2 + off, liney);
					nvgLineTo(vg, x  + off, liney);
					nvgStroke(vg);
				}
				x = x2;
			}
			else x = nvgText(vg, x + off, y + offy, str, p) - off;
			if (lexer) { p --; continue; }
		}
		if (p < end)
		{
			DATA8 cmap;
			uint8_t specialChr = *p;
			/* special characters */
			if (specialChr == '\t')
			{
				w = state->tabSize - fmodf(x, state->tabSize);
				if (sel || code > 0)
				{
					if (sel || code == SEL_INDEX_CODE)
						cmap = state->super.style.fgSel.rgba;
					else
						cmap = state->colorMap + code * 9;
					if (cmap[7] > 0)
					{
						nvgFillColorRGBA8(vg, cmap + 4);
						nvgBeginPath(vg);
						nvgRect(vg, x + off, y-1-state->extendT, w, state->fh+state->extendB);
						nvgFill(vg);
					}
				}
				x += w;
			}
			else if (specialChr == ' ')
			{
				/* justified space */
				if (state->space.count == 0) continue;
				w = (int) state->space.total / (int) state->space.count;
				state->space.total -= w;
				state->space.count --;
				if (sel || code > 0)
				{
					if (sel || code == SEL_INDEX_CODE)
						cmap = state->super.style.fgSel.rgba;
					else
						cmap = state->colorMap + code * 9;
					if (cmap[7] > 0)
					{
						nvgFillColorRGBA8(vg, cmap + 4);
						nvgBeginPath(vg);
						nvgRect(vg, x + off, y-1-state->extendT, w, state->fh+state->extendB);
						nvgFill(vg);
					}
				}
				x += w;
			}
			else /* ctrl codes (ASCII 1 ~ 31) */
			{
				DATA8 ctrlCode = chrCodes + (specialChr<<1);
				w = nvgTextBounds(vg, 0, 0, ctrlCode, ctrlCode + 2, NULL);

				nvgFillColorRGBA8(vg, sel || code == 255 ? state->super.style.fgSel.rgba : code > 0 ? state->colorMap + code * 9 : state->super.style.color.rgba);
				nvgBeginPath(vg);
				nvgRect(vg, x + off, y-1, w, state->fh);
				nvgFill(vg);
				if (! sel && code != SEL_INDEX_CODE)
				{
					if (code == 0)
					{
						Background bg = state->super.style.background;
						if (! bg || bg->color.rgba[3] < 64)
						{
							cmap = NULL;
						}
						else cmap = bg->color.rgba;
					}
					else cmap = state->colorMap + code * 9 + 4;
					if (cmap == NULL || cmap[3] == 0)
					{
						/* invert text color */
						DATA8 col = state->super.style.color.rgba;
						nvgFillColor(vg, nvgRGBA(col[0] ^ 255, col[1] ^ 255, col[2] ^ 255, col[3]));
					}
					else nvgFillColorRGBA8(vg, cmap);
				}
				else nvgFillColorRGBA8(vg, state->bgSel.rgba);
				x = nvgText(vg, x + off, y + offy, ctrlCode, ctrlCode + 2) - off;
			}
			if (lexer) lexer ++;
		}
	}
	return x;
}

static void SIT_TextEditDrawCaret(NVGcontext * vg, SIT_EditBox state, DATA8 col, float x, float y, float cw, DATA8 chr)
{
	nvgBeginPath(vg);
	if ((state->caretMode&3) == SITV_CaretBlock && ! HASFLAG(FLAG_REPLACE))
	{
		nvgFillColorRGBA8(vg, col);
		nvgRect(vg, x, y, cw, state->fh);
		nvgFill(vg);
		if (chr)
		{
			uint8_t chr0 = chr[0];
			uint8_t len  = 1;
			uint8_t bg[4] = {0, 0, 0, 0};

			if (chr0 >= 128 || ASCIIclass[chr0] != SPC)
			{
				if (chr0 > 32)
					len = (DATA8) NthChar(chr, 1) - chr;
				else
					chr = chrCodes + (chr0 << 1), len = 2;

				if (state->super.style.background)
					memcpy(bg, &state->super.style.background->color, sizeof bg);
				if (bg[3] == 0)
				{
					bg[0] = col[0] ^ 255;
					bg[1] = col[1] ^ 255;
					bg[2] = col[2] ^ 255;
					bg[3] = 255;
				}
				nvgFillColorRGBA8(vg, bg);
				nvgText(vg, x, y, chr, chr + len);
			}
		}
	}
	else /* SITV_CaretUnderline */
	{
		y += state->fh - 2;
		nvgStrokeColorRGBA8(vg, col);
		nvgMoveTo(vg, x, y);
		nvgLineTo(vg, x+cw, y);
		nvgStroke(vg);
	}
}

/* rendering of editor: render the whole area */
static int SIT_TextEditRender(SIT_Widget w, APTR unused1, APTR unused2)
{
	SIT_EditBox state = (SIT_EditBox) w;
	DOMRow row;
	APTR   vg = sit.nvgCtx;
	DATA8  lexer = NULL;
	DATA8  s, p, c, s1, s2;
	REAL   x, y, ycursor = -1, height, width, offX, offTA;
	char   ta = w->style.text.align, spcLen = 0;
	int    sel1, sel2, max;

	y    = w->offsetY + w->layout.pos.top;
	x    = w->offsetX + w->layout.pos.left;
	offX = x - state->scrollX;
	sel1 = state->selStart;
	sel2 = state->selEnd;
	if (sel1 > sel2) swap_tmp(sel1, sel2, max);
	p = s = state->text;   s2 = p + sel2;
	c = p + state->cursor; s1 = p + sel1; s += state->charTop;
	sel2 = s1 != s2 && s1 < s && s2 >= s;
	if (state->editType != SITV_Multiline)
		/* vertically center text */
		y = roundf(y + (w->layout.pos.height - state->fh) * 0.5f);

	if (s1 < s) s1 = s;
	if (s2 < s) s2 = s;
	height = state->super.layout.pos.height + y;
	width  = state->width;

	if ((state->super.state & (STATE_FOCUS|STATE_KBDFOCUS)) == 0)
	{
		state->bgSel = w->style.color;
		state->bgSel.rgba[3] >>= 1;
	}
	else state->bgSel = w->style.bgSel;
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFontFaceId(vg, w->style.font.handle);
	nvgFontSize(vg, w->style.font.size);
	nvgTextLetterSpacing(vg, 0);
	nvgScissor(vg, x, y - state->extendT, state->width, w->layout.pos.height + state->extendB);

	if (state->length == 0 && IsDef(state->cueBanner))
	{
		CSSColor fg = w->style.color;
		fg.rgba[3] >>= 1;
		nvgFillColorRGBA8(vg, fg.rgba);
		DATA8 eof = strchr(state->cueBanner, 0);
		if (ta == TextAlignRight || ta == TextAlignCenter)
		{
			REAL align = state->width - nvgTextBounds(vg, 0, 0, state->cueBanner, eof, NULL);
			if (ta == TextAlignCenter) align *= 0.5f;
			nvgText(vg, x + align, y, state->cueBanner, eof);
		}
		else nvgText(vg, x, y, state->cueBanner, eof);
	}

	/* render line at once */
	if (state->lexer && state->colorMap)
	{
		int  bytes = 0;
		REAL top;
		for (sel1 = state->rowTop, row = state->rows + sel1, max = state->rowCount, top = y; sel1 < max && top < height;
			 sel1 ++, row ++, top += state->fh)
		{
			if (bytes < row->bytes) bytes = row->bytes;
		}
		/* XXX single line edit can be pretty big :-/ */
		lexer = alloca(bytes);
	}


	for (sel1 = state->rowTop, row = state->rows + sel1, max = state->rowCount; sel1 < max && y < height;
	     s += row->bytes, sel1 ++, row ++)
	{
		/* text-align */
		p = s + row->bytes;
		if (p > s && p[-1] == '\n') p --;
		offTA = 0;
		if (ta > 0)
		{
			if (row->px == 0)
			{
				REAL px;
				if (ta != TextAlignRight)
				{
					/* ignore ending spaces */
					DATA8 e;
					for (e = p; e > s && e[-1] < 128 && ASCIIclass[e[-1]] == SPC; e --);
					SIT_TextEditBreakAt(state, s, e-s, 1e6, &px, False, NULL);
				}
				else SIT_TextEditBreakAt(state, s, p-s, 1e6, &px, False, NULL);
				row->px = px;
			}

			switch (ta) {
			case TextAlignRight: offTA = width - row->px - 2; break;
			case TextAlignCenter: offTA = (width - row->px) * 0.5f; break;
			case TextAlignJustify:
				/* distribute space, only if line is wrapped (wordWrap needs to be > 0 for this mode to work) */
				if (*p != '\n' && p != state->text + state->length)
				{
					DATA8 t, e;
					int   spc;
					for (e = p; e > s && e[-1] < 128 && ASCIIclass[e[-1]] == SPC; e --);
					for (spc = 0, t = s; t < e; t ++)
						if (SIT_IsSpace(*t)) spc ++;
					state->space.total = width - (row->px - spc * state->space.length);
					state->space.count = spc;
					if (s <= c && c <= p)
						spcLen = spc > 0 ? state->space.total / spc : state->space.length;
				}
				else state->space.count = state->space.total = 0, row->px = 65535;
			}
			if (offTA > 0 && sel2)
			{
				nvgFillColorRGBA8(vg, state->bgSel.rgba);
				nvgBeginPath(vg);
				nvgRect(vg, offX, y-1, offTA, state->fh);
				nvgFill(vg);
			}
		}
		state->offsetAlign = offX + offTA;
		if (lexer)
		{
			SIT_OnEditBox msg = {.lexerCMap = lexer, .totalRow = state->rowCount, .cmap = state->colorMap, .textBuffer = s,
				.length = row->bytes, .line = sel1, .byte = s - state->text};
			memset(lexer, 0, msg.length);

			state->lexer(&state->super, &msg, state->lexerData);

			/* selection will take priority */
			if (s1 != s2 && s <= s1 && s1 <= p)
			{
				if (s2 <= p)
					memset(lexer + (s1 - s), SEL_INDEX_CODE, s2 - s1), sel2 = 0;
				else
					memset(lexer + (s1 - s), SEL_INDEX_CODE, msg.length - (s1 - s)), sel2 = 1;
			}
			else if (sel2)
			{
				if (s <= s2 && s2 <= p)
					memset(lexer, SEL_INDEX_CODE, s2 - s), sel2 = 0;
				else
					memset(lexer, SEL_INDEX_CODE, msg.length);
			}

			x = SIT_TextEditRenderLine(state, s, p-s, 0, y, False, lexer);
		}
		/* selection */
		else if (s1 != s2 && s <= s1 && s1 <= p)
		{
			x = SIT_TextEditRenderLine(state, s, s1 - s, 0, y, False, NULL);
			if (s2 <= p)
			{
				/* selection starts and ends on this line */
				x = SIT_TextEditRenderLine(state, s1, s2 - s1, x, y, True, NULL);
				x = SIT_TextEditRenderLine(state, s2, p  - s2, x, y, False, NULL); sel2 = 0;
			}
			else
			{
				x = SIT_TextEditRenderLine(state, s1, p - s1, x, y, True, NULL);
				sel2 = s1 != p || p[0] == '\n'; /* selection not finished */
			}
		}
		else if (sel2 && s <= s2 && s2 <= p)
		{
			/* selection ends on this line */
			x = SIT_TextEditRenderLine(state, s, s2 - s, 0, y, True, NULL);
			x = SIT_TextEditRenderLine(state, s2, p - s2, x, y, False, NULL);
			sel2 = 0;
		}
		else x = SIT_TextEditRenderLine(state, s, p - s, 0, y, sel2, NULL);
		if (sel2)
		{
			/* clear up to end of line */
			REAL off = state->offsetAlign;
			nvgFillColorRGBA8(vg, state->bgSel.rgba);
			nvgBeginPath(vg);
			nvgRect(vg, x+off, y-1, width - x, state->fh);
			nvgFill(vg);
		}
		/* if a line is wrapped this test will pass twice if cursor is at the split position */
		if (s <= c && c <= p)
		{
			/* we want cursor at beginning of line (second pass) */
			SIT_TextEditBreakAt(state, s, c-s, 1e6, &state->xpos, False, row);
			state->ypos = sel1;
			state->xpos += state->offsetAlign - offX;
			ycursor = y;
		}
		y += state->fh;
		x  = offX;
	}
	if (ycursor < 0 && s == c)
	{
		/* end of file */
		state->xpos = 0;
		state->ypos = sel1;
		ycursor = y;
	}
	nvgResetScissor(vg);
	nvgScissor(vg, w->offsetX + w->layout.pos.left-2, w->offsetY + w->layout.pos.top - state->extendT, state->width+4, w->layout.pos.height + state->extendB);

	if (ycursor >= 0 && (state->super.state & (STATE_FOCUS|STATE_KBDFOCUS)) && state->caretVisible && ! state->readOnly)
	{
		DATA8 col = state->caret.rgba;
		nvgStrokeWidth(vg, 2);
		nvgStrokeColorRGBA8(vg, col);
		x = state->xpos + offX;
		/* else not visible (due to scrolling) */
		if ((HASFLAG(FLAG_REPLACE) && state->cursor < state->length) || (state->caretMode & 3) > SITV_CaretIBeam)
		{
			/* horizontal caret */
			REAL cw;
			if (state->cursor < state->length)
			{
				if (*c == '\n') cw = state->fh * 0.5f;
				else if (ta == TextAlignJustify && *c == ' ') cw = spcLen;
				else SIT_TextEditBreakAt(state, c, utf8Next[*c>>4], width, &cw, False, NULL);
				if (*c == '\t') cw -= fmodf(x - offX, state->tabSize);
			}
			else cw = nvgTextBounds(vg, 0, 0, " ", NULL, NULL), c = NULL;
			if (state->super.style.shadowCount > 0)
			{
				TextShadow shadow  = state->super.style.shadow;
				int count = state->super.style.shadowCount;
				for (; count > 0; count --, shadow ++)
					SIT_TextEditDrawCaret(vg, state, shadow->color.rgba, x + shadow->pos.XYfloat[0], ycursor + shadow->pos.XYfloat[1], cw, c);
			}
			SIT_TextEditDrawCaret(vg, state, col, x, ycursor, cw, c);
		}
		else /* vertical caret */
		{
			if (state->super.style.shadowCount > 0)
			{
				TextShadow shadow  = state->super.style.shadow;
				int count = state->super.style.shadowCount;
				for (; count > 0; count --, shadow ++)
				{
					nvgStrokeColorRGBA8(vg, shadow->color.rgba);
					nvgBeginPath(vg);
					REAL x2 = x + shadow->pos.XYfloat[0];
					REAL y2 = ycursor + shadow->pos.XYfloat[1];
					nvgMoveTo(vg, x2, y2 - 1 - state->extendT);
					nvgLineTo(vg, x2, y2 - 1 + state->fh + state->extendB - state->extendT);
					nvgStroke(vg);
				}
				nvgStrokeColorRGBA8(vg, col);
			}
			nvgBeginPath(vg); x ++;
			nvgMoveTo(vg, x, ycursor - 1 - state->extendT);
			nvgLineTo(vg, x, ycursor - 1 + state->fh + state->extendB - state->extendT);
			nvgStroke(vg);
		}
		nvgStrokeWidth(vg, 1);
	}
	nvgResetScissor(vg);
	return 1;
}

static int SIT_TextEditFitIn(SIT_EditBox state, DOMRow row, int pos, REAL x, REAL * xpos)
{
	DATA8 text = state->text + pos;
	REAL  sz   = 0;
	int   max  = row->bytes;
	REAL  off  = 0;
	int   fit;
	if (max > 0 && SIT_IsSpace(text[max-1])) max --;
	switch (state->super.style.text.align) {
	case TextAlignRight:  off = state->width - row->px - 2; break;
	case TextAlignCenter: off = (state->width - row->px) * 0.5f; break;
	}
	off -= state->scrollX;
	fit = x < off ? 0 : SIT_TextEditBreakAt(state, text, max, x - off, &sz, (state->caretMode&3) == SITV_CaretIBeam, row);
	*xpos = sz + off;
	return pos + fit;
}

static int SIT_IsWordBoundary(SIT_EditBox state, DATA8 text)
{
	return text > state->text ? (SIT_IsSeprator(text[-1]) && !SIT_IsSeprator(text[0])) : 1;
}

/* used by text editing functions */
static int SIT_TextEditFitText(SIT_EditBox state, DATA8 text, int max, int wordOnly)
{
	int fit;
	if (wordOnly == SITV_WWNone || state->width == 0)
	{
		for (fit = 0; fit < max && text[fit] != '\n'; fit ++);
		return fit < max ? fit+1 : fit;
	}
	fit = SIT_TextEditBreakAt(state, text, max-(max > 0 && text[max-1] == '\n'), state->width, NULL, False, NULL);
	if (fit < max)
	{
		int len;
		if (text[fit] == '\n')
		{
			fit ++;
			if (fit == max) return fit;
		}
		if (state->super.style.text.align != TextAlignRight)
		{
			/* keep ending spaces as part of the line, even they might cause the line to overflow */
			DATA8 p;
			for (p = text + fit; fit < max && *p < 128 && ASCIIclass[*p] == SPC; p ++, fit ++);
		}
		switch (wordOnly) {
		case SITV_WWWord:
			/* split at word boundary */
			for (len = fit; len > 0 && ! SIT_IsWordBoundary(state, text+len); len --);
			/* word too big: split at character level instead */
			if (len > 0) fit = len;
			break;
		case SITV_WWChar:
			if (fit == 0 && fit < max)
				fit ++; /* at least one to prevent infinite loop */
		}
	}
	return fit;
}

/*
 * main delete function: remove char from buffer and readjust line pointers
 */
static void SIT_TextEditDeleteChars(SIT_EditBox state, int pos, int nb)
{
	DOMRow  rows;
	DATA8   p, s, eof, end;
	int     i, j, rem;
	uint8_t wrap = state->wordWrap;

	if (state->readOnly) return;

	/* what line to start? */
	if (pos < state->charTop)
		for (i = 0, rows = state->rows, rem = 0, p = state->text;
		     i < state->rowCount && rem + rows[i].bytes <= pos; rem += rows[i].bytes, i ++);
	else
		for (i = state->rowTop, rows = state->rows, rem = state->charTop, p = state->text;
		     i < state->rowCount && rem + rows[i].bytes <= pos; rem += rows[i].bytes, i ++);

	/* if wordwrapped: grab previous line if wrapped */
	if (wrap && i > 0 && p[rem-1] != '\n')
		i --, rem -= rows[i].bytes;

	/* how many new lines will be removed? */
	for (s = p += rem, eof = state->text + pos + nb, rem = 0, j = i; p+rows[j].bytes < eof; p += rows[j].bytes, j ++, rem ++);
	if (nb >= 1 && eof[-1] == '\n') rem ++, p += rows[j].bytes, j ++;

	/* how many extra will be changed? */
	for (rem ++, eof = state->text + state->length, end = p + rows[j++].bytes; end < eof && end[-1] != '\n'; end += rows[j].bytes, rem ++, j ++);

	p = state->text + pos;
	memmove(p, p + nb, state->length - pos - nb);
	state->length -= nb;
	end -= nb;
	eof -= nb; *eof = 0;
	if (state->editType >= SITV_Integer)
		state->value.step = INVALID_STEP;

	/* adjust line pointers */
	if (s == eof)
	{
		memset(rows + i, 0, sizeof *rows);
		rem --; i ++;
	}
	else while (s < end)
	{
		int length = SIT_TextEditFitText(state, s, end - s, wrap);
		s += length;
		rem --;
		/* in very rare case, due to wordwrapping, an extra line can be added */
		if (rem < 0)
		{
			/* not efficient, but this is very rare */
			int max = (state->rowCount + 16) & ~15;
			if (state->rowMax < max)
			{
				DOMRow buf = realloc(state->rows, max * sizeof *rows);
				if (buf == NULL) return;
				state->rowMax = max;
				rows = state->rows = buf;
			}
			memmove(rows + i + 1, rows + i, (state->rowCount - i) * sizeof *rows);
		}
		rows[i].bytes = length;
		rows[i].px = 0; i ++;
	}
	if (rem > 0)
	{
		/* collpase rows */
		state->rowCount -= rem;
		if (i < state->rowCount)
			memmove(rows + i, rows + i + rem, (state->rowCount - i) * sizeof *rows);
		if (state->rowCount == 0)
			state->rowCount = 1, memset(state->rows, 0, sizeof *rows);
	}
}

#define MAX_LINES    256

/* max number is 270,549,119: ought to be enough(TM) */
static int addNumToBuf(int cp, DATA8 str)
{
	/* convert num into variable length integer */
	static uint8_t prefix[] = {0, 0, 0x80, 0xc0, 0xe0};
	int n = 0;
	/* 99% of the time, we will need 1 byte */
	if (cp < 0x80) n = 1;
	else if (cp < 0x4080)   cp -= 0x80, n = 2;
	else if (cp < 0x204080) cp -= 0x4080, n = 3;
	else                    cp -= 0x204080, n = 4;
	if (str[0] + n > MAX_LINES-1) return 0;
	str[0] += n;
	str += str[0] - n + 1;
	/* most number we will add are below 127, therefore only 1 byte needed */
	switch (n) {
	case 4: str[3] = cp & 255; cp >>= 8;
	case 3: str[2] = cp & 255; cp >>= 8;
	case 2: str[1] = cp & 255; cp >>= 8;
	case 1: str[0] = cp | prefix[n];
	}
	return 1;
}

typedef struct LineBuf_t *     LineBuf;
struct LineBuf_t
{
	LineBuf next;
	uint8_t buffer[MAX_LINES];
};

/*
 * main edit function: insert char into buffer and readjust line pointers
 */
static int SIT_TextEditInsertChars(SIT_EditBox state, int pos, char * text, int len)
{
	LineBuf next, last;
	uint8_t lines[MAX_LINES];
	uint8_t wrap = state->wordWrap;
	uint8_t setLast = 0;
	DATA8   p, end, start, eof, curlines;
	DOMRow  rows;
	int     modif, add, i, maxLines;

	if (len == 0) return 1;
	if (len > 0)
	{
		int max = state->maxText;
		i = state->length;
		if (i + len + 1 > max)
		{
			if (state->fixedSize > 0) return 0;
			max = ROUND(i+len+1);
			p = realloc(state->text, max);
			if (p == NULL) return 0;
			state->text = p;
			state->maxText = max;
		}
		/* insert text */
		state->length += len;
		p = state->text + pos;
		end = p + len;
		memmove(end, p, i - pos);
		memcpy(p, text, len);
		eof = state->text + state->length;
		*eof = 0;
		i = state->rowTop;
		add = state->charTop;
		if (pos < add)
		{
			for (i = 0, rows = state->rows, add = 0, p = state->text;
				 i < state->rowCount && add + rows[i].bytes <= pos; add += rows[i].bytes, i ++);
		}
		if (state->editType >= SITV_Integer)
			state->value.step = INVALID_STEP;
	}
	else /* used by resize: only recalc line pointers */
	{
		len = state->length;
		eof = text = state->text;
		end = eof + len;
		memset(state->rows, 0, sizeof *rows);
		state->rowCount = 1;
		i = add = 0;
	}
	#ifdef DEBUG_SIT
	/* gdb will not mess up local watch point :-/ */
	memset(lines, 0, sizeof lines);
	#else
	lines[0] = 0;
	#endif
	curlines = lines;
	maxLines = state->maxLines;
	next = last = NULL;

	/* which row is the text inserted? */
	for (rows = state->rows, modif = state->rowCount-1, start = state->text;
	     i < modif && add+rows[i].bytes <= pos; add += rows[i].bytes, i ++);

	/* grab previous line if wordwrapped */
	if (wrap && i > 0 && start[add-1] != '\n')
		i --, add -= rows[i].bytes;

	/* number of wrapped lines that will need to be updated */
	for (start += add, p = start + rows[i].bytes + len, add = -1, modif = i; p < eof && p[-1] != '\n';
	     add --, modif ++, p += rows[modif].bytes);
	if (pos + len == state->length && text[len-1] == '\n')
		add ++, setLast = 1;

	/* split new <text> into rows according to word wrapping method */
	for (modif = 0, eof = p, p = start; p < eof; )
	{
		DATA8   commit = p;
		uint8_t lineSz = curlines[0];
		int     count, chg;
		for (count = chg = 0; p < eof && *p != '\n'; p ++, count ++);
		if (p < eof) p ++, count ++;
		do {
			int length = wrap == 0 ? count : SIT_TextEditFitText(state, start, count, wrap);
			if (maxLines > 0 && state->rowCount + add + chg + 1 > maxLines)
			{
				/* argh, we got too much text, need to remove some */
				#if 0
				if (p < end)
				{
					/* remove everything extra */
					int diff = p - end;
					memmove(p, end, state);
					end = p;
					eof += diff;
					state->length += diff;
				}
				#endif
				/* remove one character at a time: if we end up here, there is not much memory to move anyway */
				for (p = end - 1; (p[0] & 0xc0) == 0x80; p --);
				memmove(p, end, state->text + state->length - end);
				length = end - p;
				p = start = commit;
				eof -= length; len -= length;
				if (end > eof) end = eof;
				state->length -= length;
				curlines[0] = lineSz;
				/* restart wordwrapping on this line */
				count = chg = 0; continue;
			}
			while (! addNumToBuf(length, curlines))
			{
				LineBuf buf = alloca(sizeof *buf);
				buf->next = NULL;
				if (last == NULL) next = buf;
				else last->next = buf; last = buf;
				curlines = buf->buffer;
				curlines[0] = 0;
			}
			start += length;
			count -= length;
			chg ++;
		} while (count > 0);
		add += chg;
		modif += chg;
	}

	/* how many new lines added? */
	if (add > 0)
	{
		int max = state->rowCount + add;
		if (state->rowMax < max)
		{
			max = (max + 15) & ~15;
			DOMRow buf = realloc(state->rows, max * sizeof *rows);
			if (buf == NULL) return 0;
			state->rowMax = max;
			rows = state->rows = buf;
		}
		if (i < state->rowCount)
			memmove(rows + i + add, rows + i, (state->rowCount - i) * sizeof *rows);
	}
	else if (add < 0)
	{
		/* wordwraping can reduce the number of lines */
		int line = i + modif - add;
		memmove(rows + line + add, rows + line, (state->rowCount - line) * sizeof *rows);
	}
	for (p = lines+1, end = p+lines[0], state->rowCount += add; modif > 0; modif --, i ++)
	{
		int num = p[0];
		if (num < 0x80) p += 1; else
		if (num < 0xc0) num = 0x80 + (((p[0] & 63) << 8) | p[1]), p += 2; else
		if (num < 0xe0) num = 0x4080 + (((p[0] & 31) << 16) | (p[1] << 8) | p[2]), p += 3;
		else            num = 0x204080 + (((p[0] & 15) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]), p += 4;
		rows[i].bytes = num;
		rows[i].px = 0;
		if (p == end && next)
		{
			p = next->buffer+1;
			end = p + p[-1];
			next = next->next;
		}
	}
	if (setLast) memset(rows + i, 0, sizeof *rows);
	state->lenInsert = len;

	#if 0
	fprintf(stderr, "====================== %f\n", state->width);
	for (i = 0, p = state->text, rows = state->rows; i < state->rowCount; rows ++, i ++)
	{
		fprintf(stderr, "%d. %d [%d]: ", i, rows->bytes, rows->px);
		fwrite(p, 1, rows->bytes, stderr);
		p += rows->bytes;
		if (p[-1] != '\n') fputs("]\n", stderr);
	}
	#endif

	return 1;
}

int SIT_TextEditSetTop(SIT_EditBox state, int line)
{
	DOMRow rows;
	int    chr, i;
	if (line != state->rowTop)
	{
		for (i = line, chr = 0, rows = state->rows; i > 0; chr += rows->bytes, rows++, i --);
		state->charTop = chr;
		state->rowTop = line;
		return 1;
	}
	return 0;
}

static int SIT_TextEditMoveToPreviousWord(SIT_EditBox state, int c, int move)
{
	/* always move at least one character */
	DATA8 text = state->text;
	DATA8 str  = text + c;
	if (str == text || state->editType == SITV_Password) return 0;
	if (str > text && str[-1] == '\n' && move) return str - text - 1;
	for (str -= move; str > text && str[-1] != '\n' && !SIT_IsWordBoundary(state, str); str --);
	return str - text;
}

static int SIT_TextEditMoveToNextWord(SIT_EditBox state, int c, int move)
{
	DATA8 text = state->text;
	DATA8 str  = text + c;
	DATA8 eof  = text + state->length;
	if (state->editType == SITV_Password) return state->length;
	if (str < eof)
	{
		uint8_t cls;
		for (cls = SIT_ChrClass(*str); str < eof && *str != '\n' && SIT_ChrClass(*str) == cls; str = NEXTCHAR(str));
		if (str < eof && *str != '\n' && SIT_ChrClass(*str) == SPC && move)
			for (str ++; str < eof && *str != '\n' && SIT_ChrClass(*str) == SPC; str = NEXTCHAR(str));
		if (move && str == text + c && str < eof) str ++;
	}
	return str - text;
}

static int SIT_TextEditMoveToSOL(SIT_EditBox state, int c, int unused)
{
	DATA8 text = state->text;
	DATA8 str  = text + c;
	if (str == text) return 0;
	while (str > text && str[-1] != '\n') str --;
	return str - text;
}

static int SIT_TextEditMoveToEOL(SIT_EditBox state, int c, int unused)
{
	DATA8 text = state->text;
	DATA8 str  = text + c;
	DATA8 eof  = text + state->length;
	if (str < eof)
	{
		while (str < eof && str[0] != '\n')
			str = NEXTCHAR(str);
		if (str < eof) str ++;
	}
	return str - text;
}

/*
 * Mouse input handling
 */

/* make sure the cursor is visible when editing something */
static void SIT_TextEditMakeCursorVisible(SIT_EditBox state)
{
	DOMRow rows;
	int    i, max, pos, c = state->cursor;

	if (state->rowVisible == 0) return; /* entire widget not yet visible */
	/* cannot rely on ypos: it is set when content is redrawn */
	if (state->editType == SITV_Multiline)
	{
		if (c < state->charTop)
		{
			for (i = state->rowTop, pos = state->charTop, rows = state->rows + i;
				 i > 0 && pos > c; i --, rows --, pos -= rows->bytes);
			SIT_TextEditSetTop(state, i);
		}
		else if (state->rowCount > state->rowVisible)
		{
			for (i = state->rowTop, max = state->rowCount, pos = state->charTop, rows = state->rows + i;
				 i < max && rows->bytes && pos + rows->bytes <= c; pos += rows->bytes, i ++, rows ++);
			if (i >= state->rowTop + state->rowVisible)
				SIT_TextEditSetTop(state, i - state->rowVisible + 1);
		}
	}
	if (state->wordWrap == 0)
	{
		/* need to check if caret goes off-screen horizontally :-/ */
		DATA8 p, s;
		REAL  xpos;
		nvgFontFaceId(sit.nvgCtx, state->super.style.font.handle);
		nvgFontSize(sit.nvgCtx, state->super.style.font.size);
		nvgTextLetterSpacing(sit.nvgCtx, 0);
		for (s = state->text, p = s + state->cursor; p > s && p[-1] != '\n'; p --);
		SIT_TextEditBreakAt(state, p, (s+state->cursor)-p, 1e6, &xpos, False, NULL);

		if (xpos < state->scrollX)
			state->scrollX = xpos;
		else if (xpos >= state->scrollX + state->width)
			state->scrollX = xpos - state->width + 2 /* caret thickness */;
	}
	SIT_TextEditRefreshCaret(state);

	if (state->caretMode & SITV_CaretNotify)
		SIT_TextEditNotify(&state->super);
}

/* traverse the layout to locate the nearest character to a display position */
static int SIT_TextEditLocateCoord(SIT_EditBox state, REAL x, REAL y)
{
	int    length = state->length;
	REAL   baseY  = 0;
	DOMRow row    = state->rows + state->rowTop;
	int    i      = state->charTop;

	/* search rows to find one that straddles 'y' */
	while (i < length)
	{
		int numChars = row->bytes;
		if (numChars <= 0)
			return length;

		if (y < baseY)
			return 0;

		if (y < baseY + state->fh)
			break;

		row   ++;
		i     += numChars;
		baseY += state->fh;
	}

	/* below all text, return 'after' last character */
	if (i >= length)
		return length;

	/* locate character within line: has to be done by outside API because of kerning + preferred pos */
	state->hasPreferredX = 1;
	return SIT_TextEditFitIn(state, row, i, x, &state->preferredX);
}

static void SIT_TextEditAdjustSelRange(SIT_EditBox state)
{
	typedef int (*pfnExtendSel)(SIT_EditBox state, int c, int unused);
	pfnExtendSel prev, next;
	if (state->selectWord == 2 || state->editType == SITV_Password)
		/* select by line */
		prev = SIT_TextEditMoveToSOL, next = SIT_TextEditMoveToEOL;
	else /* select by word */
		prev = SIT_TextEditMoveToPreviousWord, next = SIT_TextEditMoveToNextWord;

	state->selStart = state->selStart <= state->selEnd ?
		prev(state, state->selInit, 0) :
		next(state, state->selInit, 0) ;

	state->selEnd = state->selEnd < state->selStart ?
		prev(state, state->cursor, 0) :
		next(state, state->cursor, 0) ;
}

/* on mouse down, move the cursor to the clicked location, and reset the selection */
static int SIT_TextEditClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_EditBox   state = (SIT_EditBox) w;
	SIT_OnMouse * msg = cd;

	switch (msg->state) {
	case SITOM_ButtonReleased:
		if (state->autoScroll)
			SIT_ActionReschedule(state->autoScroll, -1, -1), state->autoScroll = NULL;
		break;
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			if (state->editType != SITV_Multiline)
				msg->y = 0;

			state->hasPreferredX = 0;
			int oldpos = state->cursor;
			int cursor = SIT_TextEditLocateCoord(state, msg->x - w->padding[0], msg->y);
			if (state->selectWord == 2 || cursor != state->cursor || TimeMS() - lastClick > sit.dblClickMS)
			{
				state->selectWord = 0;
				state->selInit = state->cursor = state->selStart = state->selEnd = cursor;
			}
			else
			{
				state->selectWord ++;
				SIT_TextEditAdjustSelRange(state);
			}
			sit.dirty = 1;
			lastClick = TimeMS();
			SIT_TextEditRefreshCaret(state);
			if ((state->caretMode & SITV_CaretNotify) && cursor != oldpos)
				SIT_TextEditNotify(w);
			return 2;
		}
		else if (state->editType >= SITV_Integer && (msg->button == SITOM_ButtonWheelUp || msg->button == SITOM_ButtonWheelDown))
		{
			/* mouse wheel: inc/dec number */
			SIT_TextEditSpinnerClick(state->spinnerUp, NULL, msg->button == SITOM_ButtonWheelUp ? (APTR) 2 : 0);
		}
	default: break;
	}
	return 0;
}

/* async action callback */
static int SIT_TextEditAutoScroll(SIT_Widget w, APTR cd, APTR ud)
{
	/* 1 == vertical, 2 == horizontal */
	static uint8_t actions[] = {2, 1, 0, 1, 2};
	SIT_EditBox state = (SIT_EditBox) w;
	int old = state->cursor;
	int dir = state->autoScrollDir;
	int act = actions[dir+2];
	state->hasPreferredX = 1;
	if (act == 1)
	{
		state->preferredX = dir < 0 ? 0 : 1e6;
		SIT_TextEditMoveCursorUpOrDown(state, dir, 1);
	}
	if (old != state->cursor || act == 2)
	{
		SIT_TextEditMakeCursorVisible(state);
		SIT_TextEditAdjustScroll(state);
		if (act == 2)
		{
			SIT_OnMouse msg = {.x = state->msgX, .y = state->msgY, .state = SITOM_CaptureMove};
			SIT_TextEditDrag(w, &msg, NULL);
		}
	}
	return 50;
}

static void SIT_TextEditStartAutoScroll(SIT_EditBox state, int dir)
{
	int8_t cur = state->autoScrollDir;
	if (dir && cur == 0)
		state->autoScroll = SIT_ActionAdd(&state->super, sit.curTime + 100, -1, SIT_TextEditAutoScroll, NULL);

	if (dir == 0 && state->autoScroll)
		SIT_ActionReschedule(state->autoScroll, -1, -1), state->autoScroll = NULL;
	state->autoScrollDir = dir;
}

/* on mouse drag, move the cursor and selection endpoint to the clicked location */
static int SIT_TextEditDrag(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	SIT_EditBox   state = (SIT_EditBox) w;

	if (msg->state == SITOM_CaptureMove)
	{
		static uint8_t horiz[] = {1, 0, 1, 0, 1};
		int y = msg->y;
		state->msgX = msg->x;
		state->msgY = y;
		if (state->editType != SITV_Multiline)
			y = 0;

		if (state->selStart == state->selEnd)
			state->selStart = state->cursor;
		SIT_TextEditRefreshCaret(state);

		if (y < 0)
		{
			/* vertical auto-scroll */
			SIT_TextEditStartAutoScroll(state, -1);
			return 1;
		}
		else if (y > w->layout.pos.height)
		{
			SIT_TextEditStartAutoScroll(state, 1);
			return 1;
		}
		else if (horiz[state->autoScrollDir+2] == 0)
		{
			/* stop auto-scrolling */
			SIT_TextEditStartAutoScroll(state, 0);
		}

		int cursor = SIT_TextEditLocateCoord(state, msg->x - w->padding[0], y);
		if (cursor != state->cursor)
		{
			state->cursor = state->selEnd = cursor;
			if (state->selectWord)
				SIT_TextEditAdjustSelRange(state);
			if (horiz[state->autoScrollDir+2])
			{
				/* horizontal auto-scroll */
				if (state->preferredX < 0)
					SIT_TextEditStartAutoScroll(state, -2);
				else if (state->preferredX > state->width)
					SIT_TextEditStartAutoScroll(state, 2);
				else if (state->autoScrollDir)
					SIT_TextEditStartAutoScroll(state, 0);
			}
			SIT_TextEditMakeCursorVisible(state);
			sit.dirty = 1;
		}
		return 1;
	}
	return 0;
}

/*
 * keyboard input handling
 */

#define STB_TEXT_HAS_SELECTION(s)   ((s)->selStart != (s)->selEnd)

/* delete characters while updating undo */
static void SIT_TextEditDelete(SIT_EditBox state, int where, int len)
{
	SIT_TextEditRegUndo(state, where, len, UNDO_DELETE);
	SIT_TextEditDeleteChars(state, where, len);
	state->hasPreferredX = 0;
}

/* delete the selection */
static void SIT_TextEditDeleteSelect(SIT_EditBox state)
{
	if (STB_TEXT_HAS_SELECTION(state))
	{
		int start, length;
		if (state->selStart < state->selEnd)
		{
			start = state->selStart;
			length = state->selEnd - state->selStart;
		} else {
			start = state->selEnd;
			length = state->selStart - state->selEnd;
		}
		state->selEnd = state->selStart = state->cursor = start;
		SIT_TextEditMakeCursorVisible(state);
		SIT_TextEditDelete(state, start, length);
		if (state->undoLast)
			state->undoLast[0] |= UNDO_NOMERGE;
	}
}

/* move cursor to first character of selection */
static void SIT_TextEditMoveCursorTo(SIT_EditBox state, int start)
{
	if (STB_TEXT_HAS_SELECTION(state))
	{
	   if (state->selEnd < state->selStart)
	   {
			int temp = state->selEnd;
			state->selEnd = state->selStart;
			state->selStart = temp;
		}
		state->cursor = start ? state->selStart : state->selEnd;
		state->selEnd = state->selStart;
		state->hasPreferredX = 0;
		SIT_TextEditMakeCursorVisible(state);
	}
}

/* update selection and cursor to match each other */
static void SIT_TextEditPrepSelection(SIT_EditBox state)
{
	if (!STB_TEXT_HAS_SELECTION(state))
		state->selStart = state->selEnd = state->cursor;
	else
		state->cursor = state->selEnd;
}

#ifdef UTF8ToUTF16
#undef UTF8ToUTF16
#endif

static void SIT_TextEditTransform(DATA8 dup, int max, int method)
{
	STRPTR utf16 = alloca(max * 2);
	int len = UTF8ToUTF16(utf16, max, dup, max);
	if (method == TextTransformLowercase) StrToLower16(utf16, len);
	else StrToUpper16(utf16, len);

	UTF16ToUTF8(dup, max, utf16, len);
}

/* characters might need to be replaced */
static int SIT_TextEditProcessText(SIT_EditBox state, DATA8 dest, DATA8 text, int length)
{
	DATA8   p, e, s, d;
	int     col = 0;
	int     len = 0;
	uint8_t flg = 0;
	uint8_t tab = state->tabStyle;
	uint8_t spc;
	uint8_t inc;
	uint8_t tmp;

	/* get starting column */
	if (tab == SITV_TabEditToSpace && state->cursor > 0)
	{
		for (s = state->text, e = s + state->cursor, p = e - 1; p > s && *p != '\n'; p --);
		if (p > s) p ++;
		col = e - p;
	}
	if (dest) d = dest, inc = 1;
	else      d = &tmp, inc = 0;

	/* convert '\r' into '\n' and tab into space if needed */
	for (p = s = text, e = p + length; p < e; p ++)
	{
		tmp = *p;
		switch (tmp) {
		case '\r':
			if (p[1] == '\n') d -= inc, len --;
			else *d = '\n';
			flg |= 1;
			break;
		case '\n': *d = '\n'; col = 0; s = p+1; break;
		case '\t':
			switch (tab) {
			case SITV_TabEditForbid:
				len --; flg |= 2;
				break;
			case SITV_TabEditToSpace:
				spc = state->super.style.tabSize;
				spc = spc - (p - s + col) % state->super.style.tabSize - 1;
				len += spc;
				flg |= 2;
				if (dest) memset(d, ' ', spc+1), d += spc;
				break;
			default: *d = '\t';
			}
			break;
		default: *d = tmp;
		}
		d += inc;
	}
	return flg ? length + len : 0;
}

/* API paste: replace existing selection with given text */
static int SIT_TextEditPaste(SIT_EditBox state, DATA8 text, int len)
{
	DATA8 p    = text;
	DATA8 end  = text + len;
	DATA8 dup  = NULL;
	Bool  init = False;
	SIT_TextEditDeleteSelect(state);
	if (len < 0) len = strlen(text), init = True;
	if (state->editType != SITV_Multiline)
	{
		/* replace new lines by space */
		for (p = text; p < end; p ++)
			if (*p == '\n') *p = ' ';
	}
	/* clear CR character: text might be a static string, can't change it in place */
	int editLen = SIT_TextEditProcessText(state, NULL, text, len);
	if (editLen > 0)
	{
		DATA8 d = editLen > 2047 ? malloc(len+1) : alloca(len+1);
		SIT_TextEditProcessText(state, d, text, len);
		text = d;
		len  = editLen;
	}

	/* honor CSS text-transform */
	switch (state->super.style.text.transform) {
	case TextTransformLowercase:
	case TextTransformUppercase:
		if (dup)
		{
			dup = alloca(len);
			memcpy(dup, text, end-text);
		}
		SIT_TextEditTransform(dup, len, state->super.style.text.transform);
	}

	if (SIT_TextEditInsertChars(state, state->cursor, text, len) && ! init)
	{
		len = state->lenInsert;
		if (state->width > 0) /* not yet fully initialized */
			SIT_TextEditRegUndo(state, state->cursor, len, UNDO_INSERT);
		state->cursor += len;
		state->hasPreferredX = 0;
		SIT_TextEditMakeCursorVisible(state);
		if (editLen > 4096) free(text);
		if (HAS_EVT(&state->super, SITE_OnChange))
			SIT_TextEditNotify(&state->super);
		return 1;
	}
	if (editLen > 4096) free(text);
	return 0;
}

/* cursor keys: up, down, pgup, pgdown */
static void SIT_TextEditMoveCursorUpOrDown(SIT_EditBox state, int dir, int sel)
{
	REAL xpos;
	int  row;

	if (sel)
		SIT_TextEditPrepSelection(state);
	else if (STB_TEXT_HAS_SELECTION(state))
		SIT_TextEditMoveCursorTo(state, 0);

	/* compute current position of cursor */
	xpos = state->xpos;
	row  = state->ypos;
	if (! state->hasPreferredX)
	{
		state->hasPreferredX = 1;
		state->preferredX = xpos;
	}
	else xpos = state->preferredX;
	row += dir;
	if (row >= 0)
	{
		DOMRow rows;
		int    i, j;
		if (row >= state->rowCount)
			row = state->rowCount - 1, xpos = 1e6;
		state->ypos = row;
		/* XXX use relative loop from rowTop */
		for (j = i = 0, rows = state->rows; j < row; i += rows->bytes, j ++, rows ++);
		state->cursor = SIT_TextEditFitIn(state, rows, i, xpos, &xpos);
	}
	else state->cursor = 0, xpos = 0;
	if (sel)
		state->selEnd = state->cursor;
}

/* cursor keys: ctrl+up, ctrl+down */
static void SIT_TextEditMoveViewUpOrDown(SIT_EditBox state, int dir)
{
	int top = state->rowTop + dir;
	if (top + state->rowVisible > state->rowCount)
		top = state->rowCount - state->rowVisible;
	if (top < 0) top = 0;

	if (top != state->rowTop)
	{
		DOMRow rows = state->rows + state->rowTop;
		int    i, j, c;
		state->rowTop = top;
		state->charTop += dir < 0 ? -rows[-1].bytes : rows[0].bytes;
		c = state->cursor;
		i = state->charTop;
		if (c >= i)
		{
			for (j = state->rowVisible, rows = state->rows+top; j > 0 && rows->bytes && i + rows->bytes <= c; i += rows->bytes, j --, rows ++);
			if (j > 0) return;
		}
		SIT_TextEditMoveCursorUpOrDown(state, dir, 0);
	}
}

static int SIT_TextEditReplaceLen(SIT_EditBox state, int pos, DATA8 utf8, int len)
{
	DATA8 src, dst, end, start;
	for (src = utf8, end = utf8 + len, dst = start = state->text + pos; src < end; )
	{
		src = NEXTCHAR(src);
		dst = NEXTCHAR(dst);
	}
	return dst - start;
}

static int SIT_TextEditScroll(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_TextEditSetTop((SIT_EditBox) w->parent, (int) cd);
	return 1;
}

static void SIT_TextEditAdjustScroll(SIT_EditBox state)
{
	if (state->editType != SITV_Multiline) return;
	if (state->rowCount > state->rowVisible)
	{
		if ((state->flags & FLAG_HASSCROLL) == 0)
		{
			if (state->super.vscroll == NULL)
			{
				SIT_CreateWidgets(&state->super, "<scrollbar nextCtrl=NONE name=vscroll top=FORM,,NOPAD bottom=FORM,,NOPAD right=FORM,,NOPAD>");
				SIT_AddCallback(state->super.vscroll, SITE_OnChange, SIT_TextEditScroll, NULL);
				SIT_AddCallback(state->super.vscroll, SITE_OnResize, SIT_TextEditResizeSB, NULL);
			}
			else SIT_SetValues(state->super.vscroll, SIT_Visible, True, NULL);
			state->flags |= FLAG_HASSCROLL;
			SIT_TextEditResize(&state->super, NULL, NULL);
		}
		/* empty lines at bottom? */
		int count = state->rowTop + state->rowVisible - state->rowCount;
		while (count > 0 && state->rowTop > 0)
		{
			state->charTop -= state->rows[-- state->rowTop].bytes;
			count --;
		}
		SIT_SetValues(state->super.vscroll, SIT_ScrollPos, state->rowTop, SIT_MaxValue, state->rowCount, SIT_PageSize, state->rowVisible, NULL);
	}
	else if (state->flags & FLAG_HASSCROLL)
	{
		SIT_SetValues(state->super.vscroll, SIT_Visible, False, NULL);
		state->rowTop = 0;
		state->charTop = 0;
		state->scrollPad = 0;
		state->flags &= ~FLAG_HASSCROLL;
		state->width = state->super.layout.pos.width;
		SIT_TextEditResize(&state->super, NULL, NULL);
	}
}

/* standard text insertion or replacement */
int SIT_TextEditInsertText(SIT_EditBox state, DATA8 utf8)
{
	static uint8_t ctrlCodes[] = {
		/* A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  P  Q  R  S  T  U  V  W  X  Y  Z */
		0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1
	};
	/* can't add newline in single-line mode */
	if ((utf8[0] == '\n' || utf8[0] == '\r') && state->maxLines > 0 && state->rowCount == state->maxLines)
		return False;

	if (utf8[0] < DIM(ctrlCodes) && ctrlCodes[utf8[0]])
	{
		/* these are command keys instead */
		return SIT_TextEditKey(state, utf8[0]);
	}

	if (state->readOnly)
		return False;

	if (utf8[0] == '\t')
	{
		int nb;
		switch (state->tabStyle) {
		case SITV_TabEditForbid: return False;
		case SITV_TabEditToSpace:
			nb = SIT_TextEditProcessText(state, NULL, utf8, 1);
			utf8 = alloca(nb);
			memset(utf8 = alloca(nb+1), ' ', nb);
			utf8[nb] = 0;
		}
	}

	int len = strlen(utf8);
	int pos = state->cursor;
	int top = state->rowTop;
	int num = state->rowCount;
	SIT_TextEditMakeCursorVisible(state);
	if (state->super.style.text.transform > 0)
	{
		SIT_TextEditTransform(utf8, len, state->super.style.text.transform);
	}
	if (HASFLAG(FLAG_REPLACE) && !STB_TEXT_HAS_SELECTION(state) && pos < state->length)
	{
		int dstlen = SIT_TextEditReplaceLen(state, pos, utf8, len);
		SIT_TextEditRegUndo(state, pos, dstlen, UNDO_REPLACE);

		/* not the most optimized, but at least it works */
		SIT_TextEditDeleteChars(state, pos, dstlen);
		if (SIT_TextEditInsertChars(state, pos, utf8, len))
		{
			state->cursor += len;
			state->hasPreferredX = 0;
		}
	} else {
		SIT_TextEditDeleteSelect(state); /* implicitly clamps */
		pos = state->cursor;
		if (SIT_TextEditInsertChars(state, pos, utf8, len))
		{
			len = state->lenInsert;
			SIT_TextEditRegUndo(state, pos, len, UNDO_INSERT);
			state->cursor += len;
			state->hasPreferredX = 0;
		}
	}
	if (pos != state->cursor)
		SIT_TextEditMakeCursorVisible(state);
	if (top != state->rowTop || num != state->rowCount)
		SIT_TextEditAdjustScroll(state);
	sit.dirty = True;
	return True;
}

/* return starting/ending character where cursor is */
static int SIT_TextEditStartLine(SIT_EditBox state, int next)
{
	DOMRow row  = state->rows + state->rowTop;
	int    pos  = state->charTop;
	int    line = state->ypos - state->rowTop + next;

	if (line > 0)
	{
		while (line > 0)
			pos += row->bytes, row ++, line --;
	}
	else if (line < 0)
	{
		while (line < 0)
			row --, pos -= row->bytes, line ++;
	}
	if (next && SIT_IsSpace(state->text[pos-1])) pos --;

	return pos;
}

/* API key: process a keyboard input */
int SIT_TextEditKey(SIT_EditBox state, int key)
{
	static TEXT defChr[] = " \0\n";
	int pos = state->cursor;
	int top = state->rowTop;
	int num = state->rowCount;
	uint8_t type = state->editType;
	switch (key) {
	case 1: /* Ctrl+A: select all */
		if (state->selStart != 0 || state->selEnd != state->length)
		{
			state->selStart = 0;
			state->selEnd   = state->length;
			sit.dirty       = True;
		}
		break;

	case 24: /* Ctrl+X: cut text */
		if (state->readOnly) return 0;
	case 3:  /* Ctrl+C: copy to clipboard */
		if (STB_TEXT_HAS_SELECTION(state) && type != SITV_Password)
		{
			int s1 = state->selStart;
			int s2 = state->selEnd;
			if (s1 > s2) swap(s1, s2);
			if (SIT_CopyToClipboard(state->text + s1, s2 - s1) && key == 24)
			{
				SIT_TextEditDeleteSelect(state), sit.dirty = True;
				if (HAS_EVT(&state->super, SITE_OnChange))
					SIT_TextEditNotify(&state->super);
			}
		}
		break;
	case 22: /* Ctrl+V: paste */
		if (state->readOnly == 0)
		{
			int    size = 0;
			STRPTR text = SIT_GetFromClipboard(&size);
			if (text && size > 1)
				SIT_TextEditPaste(state, text, size - 1);
			if (state->editType == SITV_Password)
			{
				/* password is still in clipboard: clear it when we are done */
				state->flags |= FLAG_CLEARCLIP;
				memset(text, 0, size);
			}
			free(text);
		}
		break;
	case 25: /* Ctrl+Y: redo */
	case 26: /* Ctrl+Z: undo */
		SIT_TextEditMakeCursorVisible(state);
		SIT_TextEditUndoRedo(state, key == 25);
		sit.dirty = True;
		pos = state->cursor;
		break;

	case SITK_Space:
	case SITK_Space | SITK_FlagShift:
		if (state->readOnly == 0 && SIT_TextEditInsertText(state, defChr))
		{
			sit.dirty = True;
			return 1;
		}
		return 0;

	case SITK_Insert:
		state->flags ^= FLAG_REPLACE;
		sit.dirty = True;
		break;

	case SITK_Return:
		if (state->readOnly == 0 && SIT_TextEditInsertText(state, defChr + 2))
		{
			sit.dirty = True;
			return 1;
		}
		return 0;

	case SITK_Up:
		if (type == SITV_Multiline)
		{
			SIT_TextEditMoveCursorUpOrDown(state, -1, 0);
			break;
		}
		else if (type >= SITV_Integer)
		{
			SIT_TextEditSpinnerClick(state->spinnerUp, NULL, (APTR) 2);
			break;
		}
		/* on windows, up&down in single-line behave like left&right */
	case SITK_Left:
		/* if currently there's a selection, move cursor to start of selection */
		if (STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditMoveCursorTo(state, 1), sit.dirty = True;

		if (state->cursor > 0)
		{
			int   pos2 = pos;
			DATA8 text = state->text + pos;
			for (text --, pos2 --; pos2 > 0 && (text[0] & 0xc0) == 0x80; text --, pos2 --);
			state->cursor = pos2;
		}
		state->hasPreferredX = 0;
		break;

	case SITK_Down:
		if (type == SITV_Multiline)
		{
			SIT_TextEditMoveCursorUpOrDown(state, 1, 0);
			break;
		}
		else if (type >= SITV_Integer)
		{
			SIT_TextEditSpinnerClick(state->spinnerUp, NULL, NULL);
			break;
		}
	case SITK_Right:
		/* if currently there's a selection, move cursor to end of selection */
		if (STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditMoveCursorTo(state, 0), sit.dirty = True;

		if (state->cursor < state->length)
			state->cursor += utf8Next[state->text[state->cursor] >> 4];
		state->hasPreferredX = 0;
		break;

	case SITK_Up | SITK_FlagShift:
		if (type == SITV_Multiline)
		{
			SIT_TextEditMoveCursorUpOrDown(state, -1, 1);
			break;
		}
	case SITK_Left | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		/* move selection left */
		if (state->selEnd > 0)
		{
			int   pos2 = state->selEnd;
			DATA8 text = state->text + pos2;
			for (text --, pos2 --; pos2 > 0 && (text[0] & 0xc0) == 0x80; text --, pos2 --);
			state->selEnd = pos2;
		}
		state->cursor = state->selEnd;
		state->hasPreferredX = 0;
		break;

	case SITK_Left | SITK_FlagCtrl:
		if (STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditMoveCursorTo(state, 1);
		else
			state->cursor = SIT_TextEditMoveToPreviousWord(state, state->cursor, 1);
		break;

	case SITK_Left | SITK_FlagCtrl | SITK_FlagShift:
		if (! STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditPrepSelection(state);

		state->cursor = SIT_TextEditMoveToPreviousWord(state, state->cursor, 1);
		state->selEnd = state->cursor;
		break;

	case SITK_Right | SITK_FlagCtrl:
		if (STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditMoveCursorTo(state, 0);
		else
			state->cursor = SIT_TextEditMoveToNextWord(state, state->cursor, 1);
		break;

	case SITK_Right | SITK_FlagCtrl | SITK_FlagShift:
		if (! STB_TEXT_HAS_SELECTION(state))
			SIT_TextEditPrepSelection(state);

		state->cursor = SIT_TextEditMoveToNextWord(state, state->cursor, 1);
		state->selEnd = state->cursor;
		break;

	case SITK_Down | SITK_FlagShift:
		if (type == SITV_Multiline)
		{
			SIT_TextEditMoveCursorUpOrDown(state, 1, 1);
			break;
		}
	case SITK_Right | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		/* move selection right */
		if (state->selEnd < state->length)
		{
			state->selEnd += utf8Next[state->text[state->selEnd] >> 4];
			state->cursor = state->selEnd;
			state->hasPreferredX = 0;
		}
		break;

	case SITK_Down | SITK_FlagCtrl:
		if (type == SITV_Multiline)
			SIT_TextEditMoveViewUpOrDown(state, 1);
		break;

	case SITK_Up | SITK_FlagCtrl:
		if (type == SITV_Multiline)
			SIT_TextEditMoveViewUpOrDown(state, -1);
		break;

	case SITK_Delete:
	case SITK_Delete | SITK_FlagShift:
	case SITK_Delete | SITK_FlagCtrl:
		if (state->readOnly) return 0;
		SIT_TextEditMakeCursorVisible(state);
		if (! STB_TEXT_HAS_SELECTION(state))
        {
			if (key & SITK_FlagCtrl)
			{
				SIT_TextEditDelete(state, state->cursor, SIT_TextEditMoveToNextWord(state, state->cursor, 1) - state->cursor);
				sit.dirty = True;
			}
			else if (state->cursor < state->length)
			{
				SIT_TextEditDelete(state, state->cursor, utf8Next[state->text[state->cursor] >> 4]);
				sit.dirty = True;
			}
		}
		else SIT_TextEditDeleteSelect(state), sit.dirty = True;

		if (sit.dirty && HAS_EVT(&state->super, SITE_OnChange))
			SIT_TextEditNotify(&state->super);
		pos = state->cursor;
		break;

	case SITK_BackSpace:
	case SITK_BackSpace | SITK_FlagShift:
	case SITK_BackSpace | SITK_FlagCtrl:
		if (state->readOnly) return 0;
		SIT_TextEditMakeCursorVisible(state);
		if (! STB_TEXT_HAS_SELECTION(state))
		{
			if (state->cursor > 0)
			{
				int   pos2 = 1;
				DATA8 text;
				if ((key & SITK_FlagCtrl) == 0)
				{
					text = state->text + state->cursor - 1;
					while ((text[0] & 0xc0) == 0x80) text --, pos2 ++;
				}
				else pos2 = state->cursor - SIT_TextEditMoveToPreviousWord(state, state->cursor, 1);
				state->cursor -= pos2;
				SIT_TextEditDelete(state, state->cursor, pos2);
				sit.dirty = True;
			}
		}
		else SIT_TextEditDeleteSelect(state), sit.dirty = True;

		if (sit.dirty && HAS_EVT(&state->super, SITE_OnChange))
			SIT_TextEditNotify(&state->super);
		pos = state->cursor;
		break;

	case SITK_Home | SITK_FlagCtrl:
		state->cursor = state->selStart = state->selEnd = 0;
		state->hasPreferredX = 0;
		break;

	case SITK_End | SITK_FlagCtrl:
		state->cursor = state->selStart = state->selEnd = state->length;
		state->hasPreferredX = 0;
		break;

	case SITK_Home | SITK_FlagCtrl | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		state->cursor = state->selEnd = 0;
		state->hasPreferredX = 0;
		break;

	case SITK_End | SITK_FlagCtrl | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		state->cursor = state->selEnd = state->length;
		state->hasPreferredX = 0;
		break;

	case SITK_Home:
		SIT_TextEditMoveCursorTo(state, 1);
		state->cursor = (type == SITV_Multiline ? SIT_TextEditStartLine(state, 0) : 0);
		state->hasPreferredX = 0;
		break;

	case SITK_End:
		SIT_TextEditMoveCursorTo(state, 1);
		state->cursor = (type == SITV_Multiline ? SIT_TextEditStartLine(state, 1) : state->length);
		state->hasPreferredX = 0;
		break;

	case SITK_Home | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		state->cursor = (type == SITV_Multiline ? SIT_TextEditStartLine(state, 0) : 0);
		state->selEnd = state->cursor;
		state->hasPreferredX = 0;
		break;

	case SITK_End | SITK_FlagShift:
		SIT_TextEditPrepSelection(state);
		state->cursor = (type == SITV_Multiline ? SIT_TextEditStartLine(state, 1) : state->length);
		state->selEnd = state->cursor;
		state->hasPreferredX = 0;
		break;

	case SITK_PrevPage:
	case SITK_PrevPage | SITK_FlagShift:
		if (type == SITV_Multiline)
			SIT_TextEditMoveCursorUpOrDown(state, -state->rowVisible, (key & SITK_FlagShift) > 0);
		break;

	case SITK_NextPage:
	case SITK_NextPage | SITK_FlagShift:
		if (type == SITV_Multiline)
			SIT_TextEditMoveCursorUpOrDown(state, state->rowVisible, (key & SITK_FlagShift) > 0);
		break;

	default: return 0;
	}
	if (pos != state->cursor)
		SIT_TextEditMakeCursorVisible(state), sit.dirty = True;
	if (top != state->rowTop || num != state->rowCount)
		SIT_TextEditAdjustScroll(state), sit.dirty = True;
	return 1;
}

/*
 * undo/redo processing.
 *
 * the way operations are stored in the undo/redo buffer is as follows:
 * - 8 bytes header (it is NOT aligned on any word/dword boundaries), composed of:
 *     - 1 byte for type of operation (see UNDO_*)
 *     - 3 bytes BE for location (absolute pos in state->text)
 *     - 3 bytes BE for length
 *     - 1 byte that is always set to 0
 * - immediately after, if the operation is UNDO_DELETE or UNDO_REPLACE is a stream of bytes
 *   containing the text deleted. Note: due to UTF-8, bytes in stream is not necessarily equals to
 *   length field in header.
 * - operations are stored one after the other, without padding.
 * - when undoing stuff, to get a pointer of the previous operations, you first locate that NUL byte
 *   by scanning the undo buffer in reverse. Once found, you subtract 7 bytes to get to the start of
 *   the header.
 */
static int SIT_TextEditUndoMakeRoom(SIT_EditBox state, int opsize, int offset, DATA8 * reloc)
{
	DATA8 op = state->undoBuffer;
	DATA8 end = state->undoLast;
	if (end)
	{
		if (end[0] != UNDO_INSERT) end += BE24(end+4);
		end += 8-offset;
	}
	if (state->flags & FLAG_FIXEDUNDO)
	{
		/* use a fixed amount of memory */
		if (opsize > state->undoSize)
		{
			/* need to discard everything :-/ */
			state->undoCount = 0;
			state->undoLast  = NULL;
			return 0;
		}
		while (op < end && end - op + opsize > state->undoSize)
		{
			/* not enough space, but we can discard earlier operations */
			int len = 8;
			if (op[0] != UNDO_INSERT) len += BE24(op+4);
			op += len;
			state->undoCount --;
		}
		/* discard */
		if (op > state->undoBuffer)
		{
			int diff = op - state->undoBuffer;
			*reloc -= diff;
			state->undoLast -= diff;
			end += offset;
			memmove(state->undoBuffer, op, end - op);
			return 1;
		}
	}
	else if (end - op + opsize > state->undoSize)
	{
		/* need more room */
		int   max = (end - op + opsize + UNDOSIZE - 1) & ~(UNDOSIZE-1);
		DATA8 buf = realloc(state->undoBuffer, max);
		if (buf)
		{
			state->undoBuffer = buf;
			state->undoSize   = max;
			end = *reloc;
			if (state->redoLast)
				state->redoLast = buf + (state->redoLast - op);
			if (state->undoLast)
				state->undoLast = buf + (state->undoLast - op);
			*reloc = end ? buf + (end - op) : buf;
		}
		else return 0;
	}
	if (*reloc == NULL)
		*reloc = end ? end + offset : state->undoBuffer;
	if (offset == 0)
		state->undoLast = *reloc, state->undoCount ++;
	return 1;
}

/* redo buf will be put at the end of undo log and grow backward */
static int SIT_TextEditRedoMakeRoom(SIT_EditBox state, int opsize, DATA8 * alloc)
{
	DATA8 op  = state->redoLast;
	DATA8 s1  = *alloc;
	DATA8 end = state->undoBuffer + state->undoSize;
	int   space;

	if (! op) op = end;
	space = op - s1;

	if (state->flags & FLAG_FIXEDUNDO)
	{
		/* use a fixed amount of memory */
		if (opsize > space)
		{
			/* need to discard everything :-/ */
			state->redoCount = 0;
			state->redoLast  = NULL;
			return 0;
		}
		for (s1 = end; op < s1 && s1 - op + opsize > space; state->redoCount --)
		{
			/* not enough space, but we can discard earlier operations */
			while (s1[-1] != 0) s1 --;
			s1 -= 8;
		}
		/* discard */
		if (s1 < end)
		{
			space = s1 - op;
			state->redoLast += end - s1;
			memmove(state->redoLast, op, space);
		}
	}
	else if (space < opsize)
	{
		/* need more room */
		int   max = (state->undoSize - space + opsize + UNDOSIZE - 1) & ~(UNDOSIZE-1);
		DATA8 buf = realloc(state->undoBuffer, max);
		if (buf)
		{
			int diff = max - state->undoSize;
			s1 = state->undoBuffer;
			end = buf + max;
			if (state->redoLast)
				state->redoLast = buf + (state->redoLast - s1);
			if (state->undoLast)
				state->undoLast = buf + (state->undoLast - s1);
			if (state->redoLast)
			{
				memmove(state->redoLast + diff, state->redoLast, state->undoSize - (state->redoLast-buf));
				state->redoLast += diff;
			}
			state->undoBuffer = buf;
			state->undoSize   = max;
		}
		else return 0;
	}
	if (state->redoLast == NULL)
		state->redoLast = end;
	*alloc = state->redoLast -= opsize;
	state->redoCount ++;
	return 1;
}

static void SIT_TextEditRegUndo(SIT_EditBox state, int pos, int length, int type)
{
	DATA8 undo = state->undoLast;
	int   loc, len;
	/* cancel redo log */
	state->redoLast = NULL;
	state->redoCount = 0;
	if (state->undoCount > 0)
	{
		loc = BE24(undo+1);
		len = BE24(undo+4);
		/* try to merge with previous op */
		if (undo[0] == type && TimeMS() - state->lastLog < 750)
		{
			int optype = 0;
			switch (type) {
			case UNDO_REPLACE: optype = loc + SIT_TextEditReplaceLen(state, pos, undo + 8, len) == pos && len + length < 128 ? 2 : 0; break;
			case UNDO_INSERT:  optype = pos == loc+len; break;
			case UNDO_DELETE:  optype = pos == loc ? 2 /* DEL */ : (pos == loc-1 ? 3 : 0); /* Backspace */
			}
			/* merge */
			if (optype > 0)
			{
				if (! SIT_TextEditUndoMakeRoom(state, length+len+8, len+8, &undo))
					return; /* not enough space :-/ */

				int sz = length + len;
				TOBE24(undo+4, sz);
				if (optype == 3)
				{
					/* need to insert from beginning */
					DATA8 s = undo + 8;
					memmove(s + length, s, len);
					loc -= length;
					TOBE24(undo+1, loc);
					len = 0;
				}
				if (optype > 1)
				{
					/* need to save what's going to get replaced/deleted */
					memcpy(undo + 8 + len, state->text + pos, length);
				}
				return;
			}
		}
		if ((undo[0]&15) != UNDO_INSERT) undo += len;
		undo += 8;
	}

	/* create a new operation */
	if (SIT_TextEditUndoMakeRoom(state, type == UNDO_INSERT ? 8 : 8 + length, 0, &undo))
	{
		state->lastLog = TimeMS();
		undo[0] = type;
		loc = pos;
		len = length;
		TOBE24(undo+1, pos);
		TOBE24(undo+4, length);
		undo[7] = 0;
		if (type != UNDO_INSERT)
		{
			/* keep characters that will be removed */
			memcpy(undo + 8, state->text + loc, len);
		}
	}
}

static void SIT_TextEditUndoRedo(SIT_EditBox state, int redo)
{
	static uint8_t converseOp[] = {UNDO_DELETE, UNDO_INSERT, UNDO_REPLACE};
	uint8_t op[8], log;
	DATA8   undo = redo ? state->redoLast : state->undoLast;
	DATA8   rep  = NULL;
	DATA8   text;
	int     loc, len, opsz, off;

	if (undo == NULL) return;

	loc = BE24(undo+1);
	len = BE24(undo+4);
	state->cursor = loc;
	off = undo + 8 - state->undoBuffer;
	state->hasPreferredX = 0;
	memcpy(op, undo, 8);
	if (redo)
	{
		off -= state->undoSize;
		state->redoCount --;
		text = NULL;
		if (state->redoCount > 0)
			state->redoLast += 8 + (undo[0] != UNDO_INSERT ? BE24(undo+4) : 0);
		else
			state->redoLast = NULL;
	}
	else
	{
		state->undoCount --;
		if (state->undoCount > 0)
		{
			/* DELETE and REPLACE have a reverse C-string (starts with a 0) stored after operation block */
			for (text = undo-1; *text; text --);
			state->undoLast = text - 7;
		}
		else state->undoLast = NULL;
		text = undo;
	}
	/* send operation into redo log */
	op[0] = converseOp[op[0]&15];
	opsz = 8;
	switch (op[0]) {
	case UNDO_DELETE:  opsz += len; break;
	case UNDO_REPLACE:
		opsz += SIT_TextEditReplaceLen(state, loc, undo + 8, len);
		memcpy(rep = alloca(opsz-8), state->text + loc, opsz-8); /* at most 128 bytes */
	}
	log = redo ? SIT_TextEditUndoMakeRoom(state, opsz, 0, &text) :
	             SIT_TextEditRedoMakeRoom(state, opsz, &text);

	sit.dirty = 1;

	switch (op[0]) {
	case UNDO_DELETE:
		if (log)
		{
			memcpy(text, op, 8);
			memcpy(text + 8, state->text + loc, len);
		}
		SIT_TextEditDeleteChars(state, loc, len);
		state->cursor = loc;
		break;
	case UNDO_REPLACE:
		SIT_TextEditDeleteChars(state, loc, opsz-8);
		/* no break; */
	case UNDO_INSERT:
		state->cursor = loc + len;
		/* cannot use <undo>, it can be relocated :-/ */
		if (off < 0) off += state->undoSize;
		SIT_TextEditInsertChars(state, loc, state->undoBuffer + off, len);
		if (log)
		{
			len = state->lenInsert;
			memcpy(text, op, 8);
			if (op[0] == UNDO_REPLACE)
			{
				memcpy(text + 8, rep, opsz-8); opsz -= 8;
				TOBE24(text + 4, opsz);
			}
		}
	}
	SIT_TextEditMakeCursorVisible(state);
	if (HAS_EVT(&state->super, SITE_OnChange))
		SIT_TextEditNotify(&state->super);
}
