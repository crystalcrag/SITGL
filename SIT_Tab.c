/*
 * SIT_Tab.c : split interface into multiple panes.
 *
 * Written by T.Pierron, oct 2020.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList TabClass[] = {
		{ SIT_TabStyle,  "tabStyle",  C__, SIT_INT,  OFFSET(SIT_Tab, tabStyle) },
		{ SIT_TabCount,  NULL,        __G, SIT_INT,  OFFSET(SIT_Tab, nbTab) },
		{ SIT_TabStr,    "tabStr",    _SG, SIT_PTR,  OFFSET(SIT_Tab, tabStr) },
		{ SIT_TabSpace,  "tabSpace",  _SG, SIT_UNIT, OFFSET(SIT_Tab, tabSpace) },
		{ SIT_TabActive, "tabActive", _SG, SIT_INT,  OFFSET(SIT_Tab, curTab) },
		{ SIT_TagEnd }
	};


/* SITE_OnChange callback */
static int SIT_TabAutoManage(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget c;
	SIT_Tab    tab = (APTR) w;
	int        nth = tab->curTab = (ULONG) cd;

	for (c = HEAD(w->children); c; NEXT(c))
	{
		int n = c->tabOrder;

		if (! tab->visiBitField)
			c->visible = n <= 0 || n-1 == nth;
		else /* some controls can be visible over multiple tabs */
			c->visible = n == 0 || (n & (1<<nth)) > 0;

//		if (c->visible) fprintf(stderr, "auto showing %S\n", c->name);
	}
	if (ud) return 1;

	SIT_TabItem item = tab->items + nth;

	/*
	 * Hidden tab content is not rearranged when window size change. Check if we need
	 * to recalc layout for tab that has just been selected.
	 */
	if (item->clientArea.width != w->layout.pos.width || item->clientArea.height != w->layout.pos.height)
	{
		#ifdef DEBUG
		fprintf(stderr, "*** recomputing layout of tab %d: %ld != %ld || %ld != %ld\n", nth,
			item->clientArea.width, w->layout.pos.width, item->clientArea.height, w->layout.pos.height);
		#endif
//		float box[4];
//		memcpy(box, &w->box, sizeof box);
		SIT_LayoutWidgets(w, KeepDialogSize);
//		memcpy(&w->box, box, sizeof box);
		item->clientArea.width  = w->layout.pos.width;
		item->clientArea.height = w->layout.pos.height;
	}
	return 1;
}

/* SITE_OnReisze handler */
static int SIT_TabSetSize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Tab     tab  = (SIT_Tab) w;
	SIT_TabItem item = tab->items + tab->curTab;

	if (tab->items == NULL) return 0;
	/* keep size current tab has been layout with, we will need to reflow the other tab if it changes */
	item->clientArea.width  = w->layout.pos.width;
	item->clientArea.height = w->layout.pos.height;

	return 1;
}

static int SIT_TabMeasure(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Tab tab = (SIT_Tab) w;
	SizeF * res = cd;
	int     i, n, nb, mode = (ULONG) ud;
	SizeF   sz;

	if (ud == AdjustRenderRect)
	{
		RectF * rect = cd;
		float h = tab->maxHeight;
		rect->top += h;
		rect->height -= h;
		return 1;
	}

	for (i = n = 0, nb = tab->nbTab, tab->maxHeight = 0; i < nb; i ++)
	{
		SIT_Widget label = tab->items[i].label;

		if (label->layout.pos.width == 0)
			SIT_MeasureWidget(label);

		n += label->box.right - label->box.left;
		REAL h = label->flags & SITF_FixedHeight ? label->fixed.height : label->box.bottom - label->box.top;
		//fprintf(stderr, "tab %d h = %g\n", i, h);
		if (tab->maxHeight < h)
			tab->maxHeight = h;
	}
	sz.width  = n+6;
	sz.height = 0;

	w->layout.padding.top = tab->padTop + tab->maxHeight;
	w->padding[1] = w->layout.padding.top + w->layout.border.top;

	/* layout children */
	if (mode == FitUsingInitialBox || mode == FitUsingOptimalBox)
	{
		for (i = 0; i < nb; i ++)
		{
			SIT_Widget c;
			Bool reflow;
			for (c = HEAD(w->children), reflow = False; c; NEXT(c))
			{
				int n = c->tabOrder;

				if (c->tabOrder == -1) continue;

				if (! tab->visiBitField)
					n = n <= 0 || n-1 == i;
				else /* some controls can be visible over multiple tabs */
					n = n == 0 || (n & (1<<i)) > 0;

				if (n != c->visible)
					c->visible ^= 1, reflow = True;
			}

			if ((reflow || i == 0) && SIT_LayoutWidgets(w, (ULONG) mode))
			{
				n = w->box.right  - w->box.left; if (sz.width  < n) sz.width  = n;
				n = w->box.bottom - w->box.top;  if (sz.height < n) sz.height = n;
			}
			memset(&w->box, 0, sizeof w->box);
		}
	}

	/* restore Visible flags */
	SIT_TabAutoManage(w, (APTR) tab->curTab, (APTR) True);

	if (res->width  <= 0) res->width  = sz.width;
	if (res->height <= 0) res->height = sz.height;
	sz.height += tab->maxHeight;

	#if 0
	if (mode == FitUsingOptimalBox)
	{
		if (w->minBox.width  < 0) w->minBox.width  = sz.width;
		if (w->minBox.height < 0) w->minBox.height = sz.height;
	}
	#endif

//	fprintf(stderr, "*** tab size '%s' = %gx%g, mode = %d\n", w->name, res->width, res->height, mode);

	return 0;
}

/* extend box of selected tab */
static int SIT_TabExtendSel(SIT_Widget w, APTR cd, APTR resizePolicy)
{
	if (resizePolicy == AdjustRenderRect)
	{
		SIT_Tab tab  = (SIT_Tab) w->parent;
		RectF * rect = cd;
		REAL    h    = w->box.bottom - w->box.top;

		if (tab->items[tab->curTab].label == w)
			/* only selected tab */
			rect->height += tab->super.layout.border.top;

		rect->top -= h;

		return 1;
	}
	else /* use original optimalBox callback */
	{
		int SIT_MeasureLabel(SIT_Widget w, APTR cd, APTR unused);

		return SIT_MeasureLabel(w, cd, resizePolicy);
	}
}

static int SIT_TabRenderCorner(SIT_Widget w, APTR cd, APTR ud)
{
	//
	return 1;
}

/* onclick on tab pane */
static int SIT_TabSetCurrent(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Tab       tab = (SIT_Tab) w->parent;
	SIT_TabItem   items;
	SIT_OnMouse * msg = cd;
	int           cur, max;

	if (msg->state != SITOM_ButtonPressed || msg->button != SITOM_ButtonLeft)
		return 0;

	for (items = tab->items, cur = 0, max = tab->nbTab; cur < max && items->label != w; cur ++, items ++);

	if (cur != tab->curTab)
	{
		/* new current tab */
		SIT_Widget  label;
		SIT_TabItem old = tab->items + tab->curTab;
		label = old->label;
		label->oldState = STATE_ACTIVATED;
		label->state = 0;
		label = items->label;
		label->oldState = 0;
		label->state = STATE_ACTIVATED;
		layoutUpdateStyles(old->label);
		layoutUpdateStyles(items->label);
		tab->curTab = cur;

		SIT_ApplyCallback(&tab->super, (APTR) cur, SITE_OnChange);
	}
	return -1;
}

/* set list of tabs based on SIT_TabStr property */
static void SIT_TabSet(SIT_Widget w, STRPTR tabs)
{
	SIT_Tab tab = (APTR) w;
	int     nb  = StrCount(tabs, '\t') + 1;
	int     old = tab->nbTab;

	if (tabs == NULL)
	{
		/* hmm, why not */
		if (tab->items) free(tab->items), tab->items = NULL;
		tab->curTab = 0;
		tab->nbTab = 0;
		return;
	}

	/* SIT_TabStr is defined as PTR and can contain static strings */
	tabs = STRDUPA(tabs);

	SIT_TabItem items = realloc(tab->items, nb * sizeof *items);
	SIT_Widget  prev  = NULL;
	if (items == NULL) return;
	tab->items = items;

	if (nb > old)
		memset(items + old, 0, (nb - old) * sizeof *items);

	int i;
	for (i = tab->curTab; tabs; i --)
	{
		SIT_Widget label = items->label;
		STRPTR next = strchr(tabs, '\t');
		if (next) *next ++ = 0;
		if (label == NULL)
		{
			items->label = label = SIT_CreateWidget(i == 0 ? "#pane:active" : "#pane", SIT_LABEL, w, SIT_Title, tabs, SIT_TabNum, -1, NULL);
			label->optimalWidth = SIT_TabExtendSel;
			label->render = SIT_TabRenderCorner;
			label->flags |= SITF_ToggleButon;
			label->layout.flags |= LAYF_AdjustRect | LAYF_AdjustHitRect;
			SIT_AddCallback(label, SITE_OnClick, SIT_TabSetCurrent, NULL);
		}
		else SIT_SetValues(label, SIT_Title, tabs, NULL);

		if (prev)
			SIT_SetValues(label, SIT_Left, SITV_AttachWidget, prev, (int) tab->tabSpace, SIT_Top, SITV_AttachForm, NULL, SITV_BorderBox, NULL);
		else
			SIT_SetValues(label, SIT_Left, SITV_AttachForm, NULL, SITV_BorderBox, SIT_Top, SITV_AttachForm, NULL, SITV_BorderBox, NULL);

		prev = label;
		tabs = next; items ++;
	}
	tab->nbTab = nb;
}

static int SIT_TabSetValues(SIT_Widget w, APTR cd, APTR ud)
{
	TagList *     tag   = cd;
	SIT_Variant * value = ud;
	SIT_Tab       tab   = (SIT_Tab) w;

	switch (tag->tl_TagID) {
	case SIT_TabActive:
		if (value->integer < 0) value->integer = tab->nbTab-1;
		tab->curTab = value->integer;
		break;
	default:
		return SIT_SetWidgetValue(w, cd, ud);
	}
	return 0;
}

static int SIT_TabFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Tab tab = (SIT_Tab) w;
	free(tab->items);
	return 1;
}

Bool SIT_InitTab(SIT_Widget w, va_list args)
{
	SIT_Tab tab = (SIT_Tab) w;

	/* default values */
	w->optimalWidth = SIT_TabMeasure;
	w->setValue     = SIT_TabSetValues;
	w->finalize     = SIT_TabFinalize;
	w->flags        = SITF_RenderChildren;
	w->layout.flags = LAYF_AdjustRect;

	SIT_ParseTags(w, args, w->attrs = TabClass);

	tab->visiBitField = (tab->tabStyle & SITV_TabVisiblityBitField) > 0;
	tab->tabStyle &= ~SITV_TabVisiblityBitField;

	layoutCalcBox(w);
	tab->padTop = w->layout.padding.top;

	SIT_TabSet(w, tab->tabStr);

	SIT_AddCallback(w, SITE_OnResize, SIT_TabSetSize,    NULL);
	SIT_AddCallback(w, SITE_OnChange, SIT_TabAutoManage, NULL);

	return True;
}

DLLIMP SIT_Widget SIT_TabGetNth(SIT_Widget w, int nth)
{
	SIT_Tab tab = (SIT_Tab) w;
	if (w == NULL || w->type != SIT_TAB) return NULL;
	if (0 <= nth && nth < tab->nbTab)
		return tab->items[nth].label;

	return NULL;
}

DLLIMP void SIT_TabSplice(SIT_Widget w, int pos, int del, ...)
{
#if 0
	va_list args;
	SIT_Tab tab = (SIT_Tab) w;
	STRPTR  name;
	Bool    reflow = False;

	if (w->type != SIT_TAB) return;
	if (pos < 0) pos = tab->nbTab-(del>0);

	for ( ; del > 0; del --)
	{
		SIT_Widget c;
		for (c = HEAD(w->children); c; NEXT(c))
		{
			int nb = c->tabOrder;

			if (nb == pos) SIT_DestroyWidget(c); else
			if (nb >  pos) c->tabOrder --;
		}
	}

	va_start(args, del);
	while ((name = va_arg(args, STRPTR)))
	{
	}
	va_end(args);

	tab->st_NbTab = TabCtrl_GetItemCount(w->sw_Handle);

	if (tab->st_NbTab == 1 && tab->st_AutoHide == 1)
		SIT_AutoHideTab(tab), reflow = True;

	if (reflow || tab->st_TabStyle == SITV_TabMultiLine)
	{
		RECT rect = {};
		TabCtrl_AdjustRect(w->sw_Handle, True, &rect);
		w->sw_Box.right  += rect.right  - rect.left;
		w->sw_Box.bottom += rect.bottom - rect.top;
		w->sw_CurrentBox.cx = w->sw_Box.right  - w->sw_Box.left;
		w->sw_CurrentBox.cy = w->sw_Box.bottom - w->sw_Box.top;
		SIT_InitiateReflow(w);
	}
#endif
}
