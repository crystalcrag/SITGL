/*
 * SIT_ScrollBar.c : vertical or horizontal proportionnal scrollbar
 *
 * written by T.Pierron, amy 2020.
 */

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList SBClass[] = {
		{ SIT_MinValue,     "minValue",     _SG, SIT_INT,  OFFSET(SIT_ScrollBar, min) },
		{ SIT_MaxValue,     "maxValue",     _SG, SIT_INT,  OFFSET(SIT_ScrollBar, max) },
		{ SIT_PageSize,     "pageSize",     _SG, SIT_INT,  OFFSET(SIT_ScrollBar, pageSize) },
		{ SIT_LineHeight,   "lineHeight",   _SG, SIT_INT,  OFFSET(SIT_ScrollBar, lineHeight) },
		{ SIT_ScrollPos,    "scrollPos",    _SG, SIT_INT,  OFFSET(SIT_ScrollBar, scrollPos) },
		{ SIT_HorizScroll,  "horizScroll",  C__, SIT_BOOL, OFFSET(SIT_ScrollBar, isHoriz) },
		{ SIT_WheelMult,    "wheelMult",    _SG, SIT_INT,  OFFSET(SIT_ScrollBar, wheelMult) },
		{ SIT_IsDragged,    NULL,           __G, SIT_U16,  OFFSET(SIT_ScrollBar, isDragged) },
		{ SIT_ArrowType,    "arrowType",    C__, SIT_INT,  OFFSET(SIT_ScrollBar, arrowType) },
		{ SIT_TagEnd }
	};

/* set minimal width/height */
static int SIT_ScrollBarMeasure(SIT_Widget w, APTR cd, APTR unused)
{
	SIT_Widget    arrow;
	SIT_ScrollBar sb  = (SIT_ScrollBar) w;
	SizeF *       ret = cd;
	REAL          off;
	SIT_Attach *  ref;

	if (sb->isHoriz)
	{
		off = ret->height;
		if (off <= 0)
		{
			off = SIT_EmToReal(sit.root, ((SIT_App)sit.root)->defSBSize);
			if (off <= 0) off = GetSystemMetrics(SM_CYHSCROLL);
			ret->height = off;
		}
		ref = w->attachment;
	}
	else /* vertical scrollbar */
	{
		off = ret->width;
		if (off <= 0)
		{
			off = SIT_EmToReal(sit.root, ((SIT_App)sit.root)->defSBSize);
			if (off <= 0) off = GetSystemMetrics(SM_CXHSCROLL);
			ret->width = off;
		}
		ref = w->attachment + 1;
	}
	if ((arrow = sb->arrowUp))   arrow->fixed.width = arrow->fixed.height = off, arrow->flags |= SITF_FixedHeight|SITF_FixedWidth;
	if ((arrow = sb->arrowDown)) arrow->fixed.width = arrow->fixed.height = off, arrow->flags |= SITF_FixedHeight|SITF_FixedWidth;

	switch (sb->arrowType) {
	case SITV_ArrowsTopBottom:
		ref[0].sa_Offset = off;
		ref[2].sa_Offset = off;
		break;
	case SITV_ArrowsBottom:
		ref[2].sa_Offset = 2*off;
		break;
	case SITV_ArrowsTop:
		ref[0].sa_Offset = 2*off;
	}
	return 0;
}

/* handle OnResize of scrollbar */
static int SIT_ScrollBarResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ScrollBar sb = (SIT_ScrollBar) w;
	SIT_Widget thumb = sb->thumb;
	REAL range = sb->oldRange;

	if (sb->checkPos)
	{
		int pos = sb->scrollPos;
		int max = sb->max - sb->pageSize;
		if (pos > max) pos = max;
		if (pos < sb->min) pos = sb->min;
		sb->scrollPos = pos;
		sb->checkPos = 0;
		if (w->layout.pos.width > 0)
		{
			sb->isDragged = 0;
			SIT_ApplyCallback(w, (APTR) pos, SITE_OnScroll);
		}
	}

	if (range == 0)
	{
		range = sb->max - sb->min;
		REAL x = (sb->scrollPos - sb->min) / range;
		REAL sz;
		if (sb->isHoriz)
		{
			sz = w->layout.pos.width;
			thumb->box.left = w->padding[0] + roundf(x * sz);
			thumb->box.right = w->padding[0] + roundf((x+sb->pageSize / range) * sz);
			thumb->box.top = w->padding[0];
			thumb->box.bottom = w->padding[0] + w->layout.pos.height;
		}
		else /* vertical */
		{
			sz = w->layout.pos.height;
			thumb->box.top = w->padding[1] + roundf(x * sz);
			thumb->box.bottom = w->padding[1] + roundf((x+sb->pageSize / range) * sz);
			thumb->box.left = w->padding[0];
			thumb->box.right = w->padding[0] + w->layout.pos.width;
		}
		sit.dirty = 1;
	}
	else /* use position on screen as reference */
	{
		//
	}
	SIT_LayoutCSSSize(thumb);
	return 1;
}

/* handle SITE_OnClick inside scrollbar */
static int SIT_ScrollBarClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ScrollBar sb = (SIT_ScrollBar) w;
	SIT_Widget    thumb = sb->thumb;
	SIT_OnMouse * msg = cd;
	REAL          pos, old, max, step, range;

	sb->isDragged = 0;
	switch (msg->button) {
	case SITOM_ButtonLeft:
		if (msg->state != SITOM_ButtonPressed)
			return 0;
		step = ud ? sb->lineHeight : sb->pageSize;
		break;
	case SITOM_ButtonWheelUp: /* mouse wheel */
		pos = sb->scrollPos - sb->lineHeight * sb->wheelMult;
		if (pos < 0) pos = 0;
		goto case_common;
	case SITOM_ButtonWheelDown:
		pos = sb->scrollPos + sb->lineHeight * sb->wheelMult;
		max = sb->max - sb->pageSize;
		if (pos > max) pos = max;
	case_common:
		if (sb->scrollPos != pos)
		{
			sb->scrollPos = pos;
			SIT_ScrollBarResize(w, NULL, NULL);
			sb->isDragged = 1;
			SIT_ApplyCallback(w, (APTR) lround(pos), SITE_OnScroll);
			sb->isDragged = 0;
		}
		return 1;
	default:
		return 0;
	}

	if (sb->isHoriz)
	{
		pos = old = thumb->box.left;
		max = thumb->box.right - pos;
		range = w->layout.pos.width - max;
		step *= range / (sb->max-sb->min);
		if (! ud && (sit.keyQual & SITK_FlagShift))
			sb->isDragged = max * 0.5,
			pos = msg->x - sb->isDragged;
		if (msg->x < pos)
			/* click before thumb */
			pos -= step;
		else if (msg->x >= thumb->box.right)
			/* click after thumb */
			pos += step;
		else
			/* offset from left pos +1 */
			sb->isDragged = msg->x - (int) pos + 1;
		if (pos > range) pos = range;
		if (pos < 0) pos = 0;
		pos += w->padding[0];
		thumb->box.left = pos;
		thumb->box.right = pos + max;
	}
	else /* vertical */
	{
		pos = old = thumb->box.top;
		max = thumb->box.bottom - pos;
		range = w->layout.pos.height - max;
		step *= range / (sb->max-sb->min);
		if (! ud && (sit.keyQual & SITK_FlagShift))
			sb->isDragged = max * 0.5,
			pos = msg->y - sb->isDragged;
		else if (msg->y < pos)
			pos -= step;
		else if (msg->y >= thumb->box.bottom)
			pos += step;
		else
			/* offset from top +1 */
			sb->isDragged = msg->y - (int) pos + 1;
		if (pos > range) pos = range;
		if (pos < 0) pos = 0;
		pos += w->padding[1];
		thumb->box.top = pos;
		thumb->box.bottom = pos + max;
	}
	if (!ALMOST0(old - pos))
	{
		sit.dirty = 1;
		SIT_LayoutCSSSize(thumb);
	}
	if (range > 0)
		pos = pos / range * (sb->max - sb->min - sb->pageSize) + sb->min;
	else
		pos = sb->min;

	if (pos != sb->scrollPos)
	{
		sb->scrollPos = pos;
		SIT_ApplyCallback(w, (APTR) lround(sb->scrollPos), SITE_OnScroll);
	}
	return 1;
}

/* SITE_OnMouseMove with thumb */
static int SIT_ScrollBarMove(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ScrollBar sb = (SIT_ScrollBar) w;
	SIT_Widget thumb = sb->thumb;
	SIT_OnMouse * msg = cd;

	if (sb->isDragged)
	{
		REAL pos, max, old;
		if (sb->isHoriz)
		{
			old = thumb->box.left;
			thumb->box.right -= old;
			max = w->layout.pos.width - thumb->box.right;
			pos = msg->x - sb->isDragged + 1;
			if (pos < 0) pos = 0;
			if (pos > max) pos = max;
			pos += w->padding[0];
			thumb->box.left = pos;
			thumb->box.right += pos;
		}
		else /* vertical */
		{
			old = thumb->box.top;
			thumb->box.bottom -= old;
			max = w->layout.pos.height - thumb->box.bottom;
			pos = msg->y - sb->isDragged + 1;
			if (pos < 0) pos = 0;
			if (pos > max) pos = max;
			pos += w->padding[1];
			thumb->box.top = pos;
			thumb->box.bottom += pos;
		}
		if (!ALMOST0(old - pos))
		{
			sit.dirty = 1;
			SIT_LayoutCSSSize(thumb);
		}
		if (max > 0)
			pos = pos / max * (sb->max - sb->min - sb->pageSize) + sb->min;
		else
			pos = sb->min;

		if (pos != sb->scrollPos)
		{
			sb->scrollPos = pos;
			SIT_ApplyCallback(w, (APTR) lround(sb->scrollPos), SITE_OnScroll);
		}
	}
	return 1;
}

static int SIT_ScrollBarArrowClick(SIT_Widget w, APTR cd, APTR ud);

static int SIT_ScrollBarAutoScroll(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse msg = {.state = SITOM_ButtonPressed};
	SIT_ScrollBarArrowClick(w, &msg, ud);
	return 50;
}

/* click inside an arrow */
static int SIT_ScrollBarArrowClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	SIT_ScrollBar sb = w->userData;

	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (ud == (APTR) 1) msg->x = msg->y = -1;
		else msg->x = msg->y = 32767;
		SIT_ScrollBarClick(&sb->super, cd, ud);

		if (sb->autoScroll == NULL)
			sb->autoScroll = SIT_ActionAdd(w, sit.curTime + 500, -1, SIT_ScrollBarAutoScroll, ud);
		return 2;

	case SITOM_ButtonReleased:
		if (sb->autoScroll)
			SIT_ActionReschedule(sb->autoScroll, -1, -1), sb->autoScroll = NULL;
	default: break;
	}
	return 1;
}

static int SIT_ScrollBarSetValue(SIT_Widget w, APTR cd, APTR ud)
{
	#define sb     ((SIT_ScrollBar)w)
	switch (((TagList *)cd)->tl_TagID) {
	case SIT_ScrollPos:
		if (sb->scrollPos == ((SIT_Variant *)ud)->integer)
			return 0;
	case SIT_MinValue:
	case SIT_MaxValue:
	case SIT_PageSize:
	case SIT_LineHeight:
		sb->checkPos = 1;
		w->postProcess = SIT_ScrollBarResize;
		// no break;
	default:
		SIT_SetWidgetValue(w, cd, ud);
	}
	return 1;
	#undef sb
}

/* control creation */
Bool SIT_InitScrollBar(SIT_Widget w, va_list args)
{
	static STRPTR classes[] = {"#arrow.up", "#arrow.left", "#arrow.down", "#arrow.right"};
	SIT_ScrollBar sb = (SIT_ScrollBar) w;

	w->optimalWidth = SIT_ScrollBarMeasure;
	w->setValue = SIT_ScrollBarSetValue;
	w->flags |= SITF_PrivateChildren | SITF_RenderChildren;

	/* default values */
	sb->wheelMult  = 3;
	sb->lineHeight = 1;
	sb->max        = 100;
	sb->pageSize   = 20;
	sb->arrowType  = ((SIT_App) sit.root)->defSBArrows;
	SIT_ParseTags(w, args, w->attrs = SBClass);
	if (sb->isHoriz)
		w->tagName = "hscroll";
	layoutCalcBox(w);
	sb->thumb = SIT_CreateWidget("thumb", SIT_HTMLTAG, w, NULL);
	sb->thumb->nextCtrl = NoFocusRing;

	if (sb->arrowType)
	{
		SIT_Attach * ref1;
		SIT_Attach * ref2;
		int8_t       off = sb->isHoriz ? 1 : -1;
		sb->arrowUp   = SIT_CreateWidget(classes[sb->isHoriz],   SIT_BUTTON, w->parent, SIT_NextCtrl, "NONE", NULL);
		sb->arrowDown = SIT_CreateWidget(classes[sb->isHoriz+2], SIT_BUTTON, w->parent, SIT_NextCtrl, "NONE", NULL);
		ref1 = sb->arrowUp->attachment + (1-sb->isHoriz);
		ref2 = sb->arrowDown->attachment + (1-sb->isHoriz);
		switch (sb->arrowType) {
		case SITV_ArrowsTopBottom: /* or left/right */
			ref1[2].sa_Type = SITV_AttachWidget;
			ref1[2].sa_Arg  = (ULONG) w;
			ref2[0].sa_Type = SITV_AttachWidget;
			ref2[0].sa_Arg  = (ULONG) w;
			break;
		case SITV_ArrowsBottom: /* or left */
			ref1[2].sa_Type = SITV_AttachWidget;
			ref1[2].sa_Arg  = (ULONG) sb->arrowUp;
			ref2[2].sa_Type = SITV_AttachWidget;
			ref2[2].sa_Arg  = (ULONG) w;
			break;
		case SITV_ArrowsTop:    /* or right */
			ref1[0].sa_Type = SITV_AttachWidget;
			ref1[0].sa_Arg  = (ULONG) w;
			ref2[0].sa_Type = SITV_AttachWidget;
			ref2[0].sa_Arg  = (ULONG) sb->arrowUp;
		}
		sb->arrowUp->userData = sb->arrowDown->userData = w;
		ref1[off].sa_Type = ref2[off].sa_Type = SITV_AttachOpposite;
		ref1[off].sa_Arg  = ref2[off].sa_Arg  = (ULONG) w;
		SIT_AddCallback(sb->arrowUp,   SITE_OnClick, SIT_ScrollBarArrowClick, (APTR) 1);
		SIT_AddCallback(sb->arrowDown, SITE_OnClick, SIT_ScrollBarArrowClick, (APTR) 2);
	}
	SIT_AddCallback(w, SITE_OnClick,     SIT_ScrollBarClick,  NULL);
	SIT_AddCallback(w, SITE_OnMouseMove, SIT_ScrollBarMove,   NULL);
	SIT_AddCallback(w, SITE_OnResize,    SIT_ScrollBarResize, NULL);

	return True;
}
