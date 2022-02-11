/*
 * SIT_ToolTip.c : termporary popup window displaying some text.
 *
 * Written by T.Pierron, Dec 09, 2010.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList TooltipClass[] = {
		{ SIT_DisplayTime,   "displayTime",   _SG, SIT_INT, OFFSET(SIT_Tooltip, displayTime) },
		{ SIT_DelayTime,     "delayTime",     _SG, SIT_INT, OFFSET(SIT_Tooltip, delayTime) },
		{ SIT_ToolTipAnchor, "toolTipAnchor", _SG, SIT_INT, OFFSET(SIT_Tooltip, anchor) },
		{ SIT_TagEnd }
	};


static int SIT_TooltipMeasure(SIT_Widget w, APTR cd, APTR ud)
{
	SizeF * ret = cd;
	SizeF   sz = {0};

	layoutMeasureWords(w, &sz);
	sz.width  += w->padding[0] + w->padding[2];
	sz.height += w->padding[1] + w->padding[3];

	w->childBox = *ret = sz;
	return 1;
}

int SIT_TooltipTimer(SIT_Widget w, APTR last, APTR ud)
{
	if (last)
	{
		SIT_SetValues(w, SIT_Visible, False, NULL);
	}
	else if (w->visible == 0 && ! sit.dragCb)
	{
		if (((SIT_Tooltip)w)->anchor == SITV_TooltipFollowMouse)
		{
			int XYWH[4] = {sit.mouseX, sit.mouseY, 20, 20};
			SIT_MoveNearby(w, XYWH, SITV_AlignRight | SITV_AlignBottom);
		}
		SIT_SetValues(w, SIT_Visible, True, NULL);
		sit.curTooltip = w;
	}
	return 0;
}

static int SIT_TooltipResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Tooltip tip = (SIT_Tooltip) w;

	if (tip->anchor == SITV_TooltipNearParent)
	{
		SIT_Widget rel = tip->relTo;
		REAL       dy  = rel->offsetY + rel->box.bottom;
		REAL       dx  = rel->offsetX + rel->box.left;
		REAL       h   = w->box.bottom - w->box.top;
		REAL       l   = w->box.right - w->box.left;

		/* not enough place below ? */
		if (dy + h > sit.scrHeight)
			dy = rel->offsetY + rel->box.top - h - w->attachment[3].sa_Offset;
		else
			dy += w->attachment[1].sa_Offset;

		if (dx + l > sit.scrWidth)
			dx = sit.scrWidth - l - w->attachment[2].sa_Offset;

		dx -= w->offsetX;
		dy -= w->offsetY;

		w->box.left  = dx;     w->box.top    = dy;
		w->box.right = dx+l;   w->box.bottom = dy+h;
	}
	else /* SITV_TooltipFollowMouse */
	{
		int XYWH[4] = {sit.mouseX, sit.mouseY, 20, 20};
		if (tip->anchor == SITV_TooltipFixed)
		{
			REAL x, y;
			XYWH[0] = x = w->fixed.left - tip->offX;
			XYWH[1] = y = w->fixed.top  - tip->offY;
			SIT_MoveNearby(w, XYWH, TOOLTIP_DEFALIGN);
			tip->offX = w->fixed.left - x;
			tip->offY = w->fixed.top - y;
		}
		else SIT_MoveNearby(w, XYWH, TOOLTIP_DEFALIGN);
	}
	return 1;
}

static int SIT_TooltipSetValue(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Variant * val = ud;
	SIT_Tooltip   tip = (SIT_Tooltip) w;
	TagList *     tag = cd;

	if (tag->tl_TagID == SIT_DisplayTime && val->integer == SITV_ResetTime)
	{
		/* drag is in progress: don't show tooltip in that case */
		if (sit.dragCb)
			return 0;
		/* display the tooltip again or extend display time if already displayed */
		if (sit.curTooltip == w)
		{
			/* already displayed: extend delay (else is about to) */
			if (w->visible && tip->displayTime < SITV_TooltipManualTrigger)
				SIT_ActionReschedule(sit.toolTip, sit.curTime, sit.curTime + tip->displayTime);
		}
		else
		{
			/* cancel current tooltip */
			if (sit.curTooltip)
				SIT_SetValues(sit.curTooltip, SIT_Visible, False, NULL);
			if (sit.toolTip)
				SIT_ActionReschedule(sit.toolTip, -1, -1), sit.toolTip = NULL;

			/* ignore delay though */
			sit.curTooltip = w;
			if (tip->displayTime < SITV_TooltipManualTrigger)
				sit.toolTip = SIT_ActionAdd(w, sit.curTime, sit.curTime + tip->displayTime, SIT_TooltipTimer, NULL);
		}
	}
	else SIT_SetWidgetValue(w, cd, ud);

	return 1;
}

Bool SIT_InitTooltip(SIT_Widget w, va_list args)
{
	SIT_Tooltip tip = (SIT_Tooltip) w;

	/* default values */
	tip->displayTime = 10000;
	tip->delayTime = 1000;
	tip->relTo = w->parent;
	w->flags |= SITF_TopLevel | SITF_RenderChildren | SITF_FallthroughEvt;
	w->optimalWidth = SIT_TooltipMeasure;
	w->setValue = SIT_TooltipSetValue;
	w->attachment[1].sa_Offset =
	w->attachment[3].sa_Offset = sit.defFontHeight / 5;

	SIT_ParseTags(w, args, w->attrs = TooltipClass);
	SIT_AddCallback(w, SITE_OnResize, SIT_TooltipResize, NULL);
	w->visible = 0;
	if (w->parent)
	{
		w->parent->tooltip = w;

		/* need to be immediate child of nearest top level control (otherwise will be drawn over by sibling control) */
		SIT_Widget parent;
		for (parent = w->parent; (parent->flags & SITF_TopLevel) == 0; parent = parent->parent);
		w->parent = parent;
	}

	/* extract CSS info for layoutParseHTML */
	layoutCalcBox(w);

	if (IsDef(w->title))
		layoutParseHTML(w, w->title);

	return True;
}

/* auto generate a tooltip widget based on the SIT_ToolTip tag */
void SIT_TooltipAuto(SIT_Widget parent, STRPTR fmt)
{
	int    len  = strlen(parent->name) + 1;
	STRPTR name = alloca(len+1);

	/* auto generate internal widget name: 't' + parent widget name */
	name[0] = 't';
	strcpy(name + 1, parent->name);

	SIT_CreateWidget(name, SIT_TOOLTIP, parent, SIT_Title, fmt, NULL);
}
