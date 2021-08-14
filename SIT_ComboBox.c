/*
 * SIT_ComboBox.c : drop down list with user editable text area, similar to a HTML <select>.
 *
 * Written by T.Pierron, july 2020.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <ctype.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	TagList ComboClass[] = {
		{ SIT_InitialValues, "initialValues", CSG, SIT_PTR,  OFFSET(SIT_ComboBox, initValues) },
		{ SIT_SelectedIndex, "selectedIndex", _SG, SIT_INT,  OFFSET(SIT_ComboBox, selIndex) },
		{ SIT_ReadOnly,      "readOnly",      _SG, SIT_BOOL, OFFSET(SIT_ComboBox, readOnly) },
		{ SIT_AutoComplete,  "autoComplete",  C__, SIT_INT,  OFFSET(SIT_ComboBox, autoComplete) },
		{ SIT_ItemCount,     NULL,            __G, SIT_INT,  OFFSET(SIT_ComboBox, items) },
		{ SIT_TagEnd }
	};

static int SIT_ComboFinalize(SIT_Widget, APTR cd, APTR ud);

/* optimal width of combo box: check that all strings within dropdown list can be visible */
static int SIT_ComboMeasure(SIT_Widget w, APTR cd, APTR unused)
{
	SIT_CBRow    row;
	SIT_ComboBox combo = (SIT_ComboBox) w;
	SizeF *      pref  = cd;
	SizeF        min   = {0, 0};
	STRPTR       title = w->title;
	int          i;

	for (row = combo->rowTags, i = combo->items; i > 0; i --, row ++)
	{
		SizeF size;
		w->title = row->entry;
		layoutMeasureWords(w, &size);
		if (min.width  < size.width)  min.width  = size.width;
		if (min.height < size.height) min.height = size.height;
	}
	w->title = title;

	min.width  += w->padding[0] + w->padding[2] + roundf(w->style.font.size * 1.2);
	min.height += w->padding[1] + w->padding[3];

	if (pref->width  < min.width)  pref->width  = min.width;
	if (pref->height < min.height) pref->height = min.height;

	return 0;
}

/* change content of combo box */
static void SIT_ComboSetList(SIT_ComboBox cb)
{
	STRPTR p, buf, next;
	int    i, max, idx = cb->selIndex;

	SIT_ComboFinalize(&cb->super, NULL, (APTR) 1);

	cb->values  = NULL;
	cb->rowTags = NULL;
	cb->count   = cb->items = 0;

	/* initValues is a user supplied pointer: consider it read-only */
	for (max = 0, next = p = cb->initValues; *p; p = next)
	{
		for (next = p; *next && *next != '\t'; next ++);
		i = next - p + 1;
		if (max < i) max = i;
		if (*next) next ++;
	}

	buf = alloca(max);

	/* add new */
	for (p = cb->initValues; *p; p = next)
	{
		for (next = p; *next && *next != '\t'; next ++);
		CopyString(buf, p, next - p + 1);
		if (*next) next ++;
		SIT_ComboInsertItem(&cb->super, -1, buf, NULL);
	}
	if (idx < 0) idx = 0;
	if (idx >= cb->items)
		idx = cb->items-1;

	if (idx != cb->selIndex)
	{
		cb->super.title = cb->rowTags[idx].entry;
		cb->selIndex = idx;
	}
}

/* postProcess callback */
static int SIT_ComboSetStyles(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;

	if (w->flags & SITF_Style1Changed) /* combo list changed */
		SIT_ComboSetList(cb);

//	if (w->flags & SITF_Style2Changed)
//		SendMessage(w->sw_Handle, CB_SETCURSEL, cb->sc_SelIndex, 0);

	w->postProcess = NULL;
	w->flags &= ~SITF_StylesChanged;
	return 0;
}

/* setValue callback */
static int SIT_ComboSetValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox  cb    = (SIT_ComboBox) w;
	SIT_Variant * value = ud;
	TagList *     tag   = cd;

	switch (tag->tl_TagID) {
	case SIT_InitialValues:
		cb->initValues = value->string;
		w->flags |= SITF_Style1Changed;
		break;
	case SIT_SelectedIndex:
		cb->selIndex = value->integer;
		w->flags |= SITF_Style2Changed;
		break;
	case SIT_ReadOnly: /* create only */
		cb->readOnly = value->boolean;
		break;
	default:
		return SIT_SetWidgetValue(w, cd, ud);
	}
	if (w->flags & SITF_StylesChanged)
		w->postProcess = SIT_ComboSetStyles;
	return 0;
}

#if 0
int SIT_ComboBoxItemHeight(SIT_Widget w)
{
	int nb  = SendMessage(w->sw_Handle, CB_GETCOUNT, 0, 0);
	int max = GetSystemMetrics(SM_CYSCREEN) - 40;

	if (nb > 15) nb = 15;
	if (nb <= 0) nb = 1;

	nb = nb * ((SIT_ComboBox)w)->sc_ItemHeight + 6;

	return MIN(nb, max);
}

static Bool DoAutoComplete(HWND combo, HWND edit, int type, TCHAR ch)
{
	ULONG  max    = GetWindowTextLength(combo)+2;
	LPWSTR buffer = alloca(max * sizeof *buffer);
	LPWSTR item   = alloca((max + 100) * sizeof *item);
	LPWSTR temp   = NULL;
	int    i, nb, nth;

	GetWindowText(combo, buffer, max);

	// Handle keyboard input
	if (VK_RETURN == ch)
	{
		SendMessage(combo, CB_SHOWDROPDOWN, FALSE, 0);
		SendMessage(edit, EM_SETSEL, 0, -1); // Selects the entire item
		i = SendMessage(combo, CB_FINDSTRINGEXACT, -1, (LPARAM) buffer);
		SendMessage(combo, CB_SETCURSEL, i, 0);
		if (i == CB_ERR)
			i = (1<<16)-1;
		// Doesn't seem to send any notification code
		SIT_SubClassHandler(GetParent(combo), WM_COMMAND, (CBN_SELCHANGE<<16)|i, (LPARAM) combo);
		return False;
	}
	else if (iscntrl(ch)) return False;

	if (type == 2 && ! SendMessage(combo, CB_GETDROPPEDSTATE, 0, 0))
	{
		SendMessage(combo, CB_SHOWDROPDOWN, TRUE, 0);
		SetFocus(edit);
	}

	// Get the substring from 0 to start of selection
	nb = SendMessage(combo, CB_GETCOUNT, 0, 0);
	nth = LOWORD(SendMessage(combo, CB_GETEDITSEL, 0, 0));
	buffer[nth] = 0;
	item[0] = ch; item[1] = 0; wcscat(buffer, item); nth ++;

	// ComboBoxEx doesn't have a substring match (regular ComboBox does) :-/
	for (i = 0, max += 100; i < nb; i ++)
	{
		int    len = SendMessage(combo, CB_GETLBTEXTLEN, i, 0) + 1;
		LPWSTR buf = len > max ? temp = realloc(temp, len) : item;
		SendMessage(combo, CB_GETLBTEXT, i, (LPARAM) buf);
		if (wcsnicmp(buf, buffer, nth) == 0) break;
	}
	if (temp) free(temp);
	if (i < nb)
	{
		SendMessage(combo, CB_SETCURSEL, i, 0);
		SendMessage(edit, EM_SETSEL, nth, -1);
		return True;
	}
	return False;
}
#endif

static int SIT_ComboFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;

	if (cb->rowTags) free(cb->rowTags);
	if (cb->values)  free(cb->values);
	w->title = NULL;

	return 0;
}

DLLIMP APTR SIT_ComboGetRowTag(SIT_Widget w, int i, STRPTR * msg)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;
	SIT_CBRow    row;
	if (w == NULL || w->type != SIT_COMBOBOX) return NULL;
	if (i < 0) i = MAX(cb->selIndex, 0);
	if (i >= cb->items) i = cb->items - 1;
	if (i < 0) return NULL;
	row = cb->rowTags + i;
	if (msg) *msg = row->entry;
	return row->tag;
}

DLLIMP int SIT_ComboDeleteItem(SIT_Widget w, int index)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;
	SIT_CBRow    row;
	STRPTR       msg, end;

	if (w == NULL || w->type != SIT_COMBOBOX) return -1;
	if (index < 0 || index >= cb->items) index = cb->items - 1;
	if (index < 0) return -1;
	row = cb->rowTags + index;
	msg = row->entry;
	end = strchr(msg, 0) + 1;
	if (index + 1 < cb->items)
		memmove(row, row + 1, cb->items - index - 1);
	if (end < cb->values + cb->count)
		memmove(msg, end, cb->count - (end - cb->values));
	if (cb->selIndex >= index)
		cb->selIndex --;
	cb->items --;
	cb->count -= end - msg;
	return index;
}

DLLIMP int SIT_ComboInsertItem(SIT_Widget w, int index, STRPTR item, APTR rowtag)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;
	SIT_CBRow    row;
	if (w == NULL || w->type != SIT_COMBOBOX) return -1;
	int max = (cb->count + 255) & ~255;
	int tag = (cb->items + 31) & ~31;
	int len = strlen(item) + 1;

	if (index < 0) index = cb->items;
	if (index <= cb->selIndex)
		cb->selIndex ++;

	if (cb->count + len > max)
	{
		STRPTR val;
		max = (cb->count + len + 255) & ~255;
		val = realloc(cb->values, max);
		if (val == NULL) return -1;
		if (cb->count == 0) val[0] = 0;
		for (max = 0, row = cb->rowTags; max < cb->items; max ++, row ++)
			row->entry = val + (row->entry - cb->values);
		cb->values = val;
	}
	if (cb->items + 1 > tag)
	{
		max = (cb->items + 32) & ~31;
		cb->rowTags = realloc(cb->rowTags, max * sizeof *cb->rowTags);
	}
	if (index < cb->items)
		memmove(cb->rowTags + index + 1, cb->rowTags, (cb->items - index) * sizeof *cb->rowTags);
	row = cb->rowTags + index;
	/* values don't have to be sorted as long as rowTags are */
	row->entry = strcpy(cb->values + cb->count, item);
	row->tag   = rowtag;
	cb->items ++;
	cb->count += len;

	return cb->items - 1;
}

/* use text entered to search inside the list */
static int SIT_ComboProcessChar(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox cb  = (SIT_ComboBox) w;
	SIT_OnKey *  msg = cd;
	int          len = strlen(msg->utf8);

	if (cb->searchLen + len <= sizeof cb->searchTxt)
	{
		SIT_CBRow row;
		int       i;
		/* searchTxt is not null terminated */
		memcpy(cb->searchTxt + cb->searchLen, msg->utf8, len);
		cb->searchLen += len;

		for (i = 0, row = cb->rowTags, len = cb->searchLen; i < cb->items; i ++, row ++)
		{
			STRPTR text = row->entry;
			/* skip initial spaces */
			while (*text == '\t' || *text == ' ') text ++;
			if (strncasecmp(text, cb->searchTxt, len) == 0)
			{
				if (i != cb->selIndex)
				{
					w->title = row->entry;
					cb->selIndex = i;
					sit.dirty = 1;
					SIT_ApplyCallback(w, (APTR) i, SITE_OnScroll);
				}
				break;
			}
		}
		return 1;
	}
	return 0;
}

/* arrow keys: select new entry */
static int SIT_ComboProcessKey(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox cb  = (SIT_ComboBox) w;
	SIT_OnKey *  msg = cd;
	int          sel = cb->selIndex;

	switch (msg->keycode) {
	case SITK_Up:   sel --;  break;
	case SITK_Home: sel = 0; break;
	case SITK_Down: sel ++;  break;
	case SITK_End:  sel = 1e6; break;
	case SITK_BackSpace: /* will clear searchTxt */
	case SITK_Delete:
	case SITK_Escape: break;
	default: return 0;
	}

	cb->searchLen = 0;
	if (sel < 0) sel = 0;
	if (sel >= cb->items) sel = cb->items-1;
	if (sel != cb->selIndex)
	{
		w->title = cb->rowTags[sel].entry;
		cb->selIndex = sel;
		sit.dirty = 1;
		SIT_ApplyCallback(w, (APTR) sel, SITE_OnChange);
	}

	return 1;
}

static void SIT_ComboInitPopupBox(SIT_ComboBox cb, RectF * box)
{
	SIT_Widget w = cb->arrow;
	BoxF       extra = cb->super.layout.border;
	REAL       pad[4];
	REAL *     val;
	int        i;

	/* add 2 pixels around text */
	for (i = 0, val = &extra.top; i < 4; *val += 2, val ++, i ++);
	extra.right += w->box.left - w->box.right;

	memcpy(pad, cb->super.padding, sizeof pad);
	for (i = 0; i < 4; i ++)
	{
		static uint8_t off[] = {1, 2, 3, 0};

		val = pad + off[i];
		*val -= (&extra.top)[i];
		if (*val < 0) *val = 0;
	}
	w = cb->popup;

	cb->popupRect.left   = box->left + pad[0];
	cb->popupRect.top    = box->top  + pad[1];
	cb->popupRect.width  = box->width - (pad[0]+pad[2]);
	cb->popupRect.lineh  = box->height - (pad[1]+pad[3]);
	cb->popupRect.hup    = cb->selIndex * cb->popupRect.lineh + w->padding[1];
	cb->popupRect.hdown  = (cb->items - cb->selIndex - 1) * cb->popupRect.lineh + w->padding[3];

	REAL y = cb->popupRect.top - cb->popupRect.hup;

	if (y < 0)
		cb->popupRect.hup -= y, cb->popupRect.hdown -= y;

	y = cb->popupRect.top + cb->popupRect.hdown + cb->popupRect.lineh - sit.scrHeight;
	if (y > 0)
		cb->popupRect.hup += y, cb->popupRect.hdown -= y;
}

/* render text within combo */
static int SIT_ComboRender(SIT_Widget w, APTR cd, APTR ud)
{
	CSSColor old;
	RectF *  box = cd;

	if (w->state & (STATE_FOCUS|STATE_KBDFOCUS))
	{
		/* draw a filled rectangle below text */
		SIT_ComboBox cb = (SIT_ComboBox) w;
		SIT_ComboInitPopupBox(cb, box);

		memcpy(&old, &w->style.color, sizeof old);
		memcpy(&w->style.color, &w->style.fgSel, sizeof old);
		nvgBeginPath(sit.nvgCtx);
		nvgRect(sit.nvgCtx, cb->popupRect.left, cb->popupRect.top, cb->popupRect.width, cb->popupRect.lineh);
		nvgFillColorRGBA8(sit.nvgCtx, w->style.bgSel.rgba);
		nvgFill(sit.nvgCtx);
	}
	else old.val = 0;

	w->layout.flags |= LAYF_RenderWordBg;
	if (w->style.shadowTotal > 0)
		renderTextShadow(w, box);
	renderWords(w, box, 0);

	if (w->state & (STATE_FOCUS|STATE_KBDFOCUS))
		memcpy(&w->style.color, &old, sizeof old);

	return 1;
}

/* handle SITE_OnFocus */
static int SIT_ComboMarkDirty(SIT_Widget w, APTR cd, APTR ud)
{
	sit.dirty = 1;
	return 1;
}

/* anim displaying popup menu */
static int SIT_ComboPopup(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox combo  = (SIT_ComboBox) w;
	SIT_Widget   popup  = combo->popup;
	REAL         factor = combo->animTime - (combo->animStop - sit.curTime);

	if (factor >= combo->animTime || cd)
	{
		combo->anim = NULL;
		factor = 1;
	}
	else factor /= combo->animTime;

	popup->box.left   = roundf(combo->popupRect.left - popup->padding[0] + 2);
	popup->box.right  = roundf(combo->popupRect.left + combo->popupRect.width + popup->padding[2] + 2);
	popup->box.top    = roundf(combo->popupRect.top - combo->popupRect.hup * factor);
	popup->box.bottom = roundf(combo->popupRect.top + combo->popupRect.lineh + combo->popupRect.hdown * factor);

	SIT_LayoutCSSSize(popup);
	sit.dirty = 1;

	return 0;
}

/* handle SITE_OnClick */
static int SIT_ComboClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ComboBox  cb    = (SIT_ComboBox) w->parent;
	SIT_Widget    popup = cb->popup;
	SIT_OnMouse * msg   = cd;

	if (msg->button != 0)
		return 0;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (! cb->anim)
		{
			RectF box;
			renderInitBox(&cb->super, &box, True, 15);
			SIT_ComboInitPopupBox(cb, &box);

			cb->anim = SIT_ActionAdd(&cb->super, sit.curTime, cb->animStop = sit.curTime + cb->animTime, SIT_ComboPopup, NULL);
			cb->oldSel = cb->selIndex;
			popup->visible = 1;
			return 2;
		}
		break;
	case SITOM_ButtonReleased:
		if (popup->visible)
		{
			popup->visible = 0;
			sit.dirty = 1;
			if (cb->oldSel != cb->selIndex)
				SIT_ApplyCallback(w, (APTR) cb->selIndex, SITE_OnChange);
		}
		if (cb->anim)
		{
			SIT_ActionReschedule(cb->anim, -1, -1);
			cb->anim = NULL;
		}
	default: break;
	}
	return 0;
}

/* SITE_OnMouseMove inside arrow button */
static int SIT_ComboSelectItem(SIT_Widget w, APTR cd, APTR ud)
{
	/* mouse coords are relative to arrow, not popup */
	SIT_ComboBox  cb  = (SIT_ComboBox) w->parent;
	SIT_OnMouse * msg = cd;
	int           sel;

	if (! cb->popup->visible)
		return 0;

	sel = (w->box.top + w->offsetY + msg->y - (cb->popupRect.top - cb->popupRect.hup + cb->popup->padding[1])) / cb->popupRect.lineh;

	if (sel >= 0 && sel < cb->items && sel != cb->selIndex)
	{
		cb->selIndex = sel;
		cb->super.title = cb->rowTags[sel].entry;
		sit.dirty = 1;
	}
	return 1;
}

/* render items of popup menu */
static int SIT_ComboRenderItems(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_CBRow    row;
	SIT_ComboBox cb = (SIT_ComboBox) w->userData;
	NVGCTX       vg = sit.nvgCtx;
	RectF *      box = cd;
	BoxF         border = w->layout.border;
	REAL         y, x;
	int          i, j;

	/* render border: top part */
	border.right += border.left;
	x = box->left - border.left;
//	y = cb->super.box.left + cb->super.offsetX + 2 + cb->super.layout.border.left;
//	if (x > y) x = y;
	nvgSave(vg);
	nvgIntersectScissor(vg, x, box->top-border.top, box->width+border.right,
		(cb->popupRect.top+2-cb->super.padding[1])-box->top+border.top);
	renderBorder(w, box, 1+2+8);
	nvgRestore(vg);

	/* render border: bottom part */
	y = cb->super.box.bottom + cb->super.offsetY - cb->super.layout.border.bottom;
	nvgSave(vg);
	nvgIntersectScissor(vg, x, y, box->width+border.right, box->height);
	renderBorder(w, box, 2+4+8);
	nvgRestore(vg);
	#if 0
	nvgBeginPath(vg);
	nvgRect(vg, x, y, box->width+border.right, box->height);
	nvgStrokeColorRGBA8(vg, "\xff\0\xff\xff");
	nvgStroke(vg);
	#endif

	nvgSave(vg);
	nvgIntersectScissor(vg, box->left, box->top, box->width, box->height);
	CSSColor old = w->style.color;

	for (i = cb->items, j = cb->selIndex, row = cb->rowTags, y = cb->popupRect.top - cb->popupRect.hup + w->layout.border.top + 2;
	     i > 0; j --, i --, row ++, y += cb->popupRect.lineh)
	{
		w->title = row->entry;
		box->top = y;
		if (j == 0)
		{
			nvgBeginPath(vg);
			nvgRect(vg, cb->popupRect.left, y + w->padding[1] - w->layout.border.top - 2, cb->popupRect.width, cb->popupRect.lineh);
			nvgFillColorRGBA8(vg, cb->super.style.bgSel.rgba);
			nvgFill(vg);
			w->style.color = w->style.fgSel;
		}
		else w->style.color = old;
		renderWords(w, box, 0);
	}
	w->style.color = old;
	w->title = NULL;
	nvgRestore(vg);

	return 1;
}

/* SITE_OnResize on popup menu */
static int SIT_ComboCancelBorder(SIT_Widget w, APTR cd, APTR ud)
{
	w->layout.flags |= LAYF_NoBorders;
	return 1;
}


Bool SIT_InitComboBox(SIT_Widget w, va_list args)
{
	SIT_ComboBox cb = (SIT_ComboBox) w;

	/* Default values */
	cb->selIndex    = -2; /* Unless explicitely set to -1, will select first entry */
	cb->animTime    = 150;
	w->optimalWidth = SIT_ComboMeasure;
	w->setValue     = SIT_ComboSetValues;
	w->finalize     = SIT_ComboFinalize;
	w->render       = SIT_ComboRender;
	w->flags        = SITF_RenderChildren;

	SIT_AddCallback(w, SITE_OnVanillaKey, SIT_ComboProcessChar, NULL);
	SIT_AddCallback(w, SITE_OnRawKey,     SIT_ComboProcessKey, NULL);
	SIT_AddCallback(w, SITE_OnFocus,      SIT_ComboMarkDirty, NULL);
	SIT_AddCallback(w, SITE_OnBlur,       SIT_ComboMarkDirty, NULL);

	layoutCalcBox(w);

	cb->arrow = SIT_CreateWidget("arrow", SIT_HTMLTAG, w,
		SIT_Right,    SITV_AttachForm, 0, SITV_NoPad,
		SIT_Top,      SITV_AttachForm, 0, SITV_NoPad,
		SIT_Width,    SITV_Em(1.2),
		SIT_Bottom,   SITV_AttachForm, 0, SITV_NoPad,
		NULL
	);

	SIT_AddCallback(cb->arrow, SITE_OnClick, SIT_ComboClick, NULL);
	SIT_AddCallback(cb->arrow, SITE_OnMouseMove, SIT_ComboSelectItem, NULL);

	SIT_Widget popup;
	cb->popup = popup = SIT_CreateWidget("#popup", SIT_CANVAS | SIT_EXTRA(sizeof (struct SIT_Callback_t) * 2), sit.root, NULL);
	popup->cbSlot = 1;
	popup->render = SIT_ComboRenderItems;
	popup->userData = cb;
	popup->flags |= SITF_TopLevel;
	popup->visible = 0;

	SIT_AddCallback(popup, SITE_OnResize, SIT_ComboCancelBorder, NULL);

	SIT_ParseTags(w, args, w->attrs = ComboClass);

	if (w->postProcess)
		w->postProcess(w, NULL, NULL);

	return True;
}
