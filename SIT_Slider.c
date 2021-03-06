/*
 * SIT_Slider.c : control that enable to choose among a range of a discrete value
 *
 * Written by T.Pierron, july 2020
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	struct TagList_t SliderClass[] = {
		{ "minValue",     SIT_MinValue,     _SG, SIT_INT,  OFFSET(SIT_Slider, minValue) },
		{ "maxValue",     SIT_MaxValue,     _SG, SIT_INT,  OFFSET(SIT_Slider, maxValue) },
		{ "pageSize",     SIT_PageSize,     _SG, SIT_INT,  OFFSET(SIT_Slider, pageSize) },
		{ "sliderPos",    SIT_SliderPos,    _SG, SIT_INT,  OFFSET(SIT_Slider, sliderPos) },
		{ "horizScroll",  SIT_HorizScroll,  C__, SIT_BOOL, OFFSET(SIT_Slider, isHoriz) },
		{ "thumbThick",   SIT_ThumbThick,   _SG, SIT_UNIT, OFFSET(SIT_Slider, thumbThick) },
		{ "thumbHeight",  SIT_ThumbHeight,  _SG, SIT_UNIT, OFFSET(SIT_Slider, thumbHeight) },
		{ "gaugePadding", SIT_GaugePadding, _SG, SIT_UNIT, OFFSET(SIT_Slider, gaugePadding) },
		{ "buddyEdit",    SIT_BuddyEdit,    _SG, SIT_CTRL, OFFSET(SIT_Slider, buddy) },
		{ "curValue",     SIT_CurValue,     _SG, SIT_PTR,  OFFSET(SIT_Slider, curValue) },
		{ "dragNotify",   SIT_DragNotify,   _SG, SIT_BOOL, OFFSET(SIT_Slider, dragNotify) },
		{ NULL,           SIT_TagEnd }
	};

/* set minimal width/height */
static int SIT_SliderMeasure(SIT_Widget w, APTR call_data, APTR resizePolicy)
{
	SIT_Slider s = (SIT_Slider) w;

	if (resizePolicy != AdjustRenderRect)
	{
		SizeF * ret = call_data;

		if (s->isHoriz) ret->height = s->thumbHeight;
		else            ret->width  = s->thumbHeight;
	}
	else
	{
		RectF * rect = call_data;
		REAL    pad  = s->gaugePadding;

		rect->top += pad;
		rect->height -= pad*2;
	}
	return 0;
}

#define SEND_EVENT    ((APTR)1)

/* SITE_OnResize event handler */
static int SIT_SliderResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Slider s     = (SIT_Slider) w;
	SIT_Widget gauge = s->gauge;
	SIT_Widget thumb = s->thumb;

	int range = s->maxValue - s->minValue;
	int pos = s->sliderPos - s->minValue;
	int sz = roundf(s->thumbThick / 2);
	REAL pad = s->gaugePadding;

	pos = (w->box.right - w->box.left - pad) * pos / range + pad * 0.5f;

	thumb->box.left = pos - sz;
	thumb->box.right = sz * 2 + thumb->box.left;
	thumb->box.top = 0;
	thumb->box.bottom = w->box.bottom - w->box.top;

	SIT_LayoutCSSSize(thumb);

	gauge->box.left = w->padding[0];
	gauge->box.right = pos;
	gauge->box.top = pad + w->padding[1];
	gauge->box.bottom = w->box.bottom - w->box.top - pad - w->padding[3];

	SIT_LayoutCSSSize(gauge);

	if (ud == SEND_EVENT)
	{
		if (s->buddy)
		{
			TEXT value[16];
			sprintf(value, "%d", s->sliderPos);
			SIT_SetValues(s->buddy, SIT_Title, value, NULL);
		}
		if (s->dragNotify || s->isDragged == 0)
			SIT_ApplyCallback(w, (APTR) s->sliderPos, SITE_OnChange);
	}

	return 1;
}

static int SIT_SliderSync(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Slider s = ud;
	STRPTR value;
	int    pos;
	SIT_GetValues(w, SIT_Title, &value, NULL);
	pos = atoi(value);
	if (pos != s->sliderPos)
	{
		SIT_SetValues(ud, SIT_SliderPos, pos, NULL);
		SIT_ApplyCallback(&s->super, (APTR) s->sliderPos, SITE_OnChange);
	}
	return 1;
}

static int SIT_SliderSetValues(SIT_Widget w, APTR call_data, APTR user_data)
{
	SIT_Variant value = user_data;
	TagList     tag   = call_data;
	SIT_Slider  s     = (SIT_Slider) w;

	switch (tag->tl_TagID) {
	case SIT_SliderPos:
	{	int pos = value->integer;
		if (pos < s->minValue) pos = s->minValue;
		if (pos > s->maxValue) pos = s->maxValue;
		if (pos != s->sliderPos)
		{
			s->sliderPos = pos;
			if (s->curValue)
				s->curValue[0] = pos;
			sit.dirty = 1;
			if (w->flags & SITF_InitDone)
				SIT_SliderResize(w, NULL, NULL);
			if ((w = s->buddy))
				goto reset_buddy;
		}
	}	break;
	case SIT_BuddyEdit:
		if (s->buddy)
			SIT_DelCallback(s->buddy, SITE_OnChange, SIT_SliderSync, s);
		w = s->buddy = value->pointer;
		if (w)
		{
			TEXT buffer[16];
			reset_buddy:
			sprintf(buffer, "%d", s->sliderPos);
			SIT_SetValues(w, SIT_Title, buffer, NULL);
			if (w->type == SIT_EDITBOX) /* it will trigger event back to TB */
			{
				SIT_SetValues(w, SIT_MinValue, (double) s->minValue, SIT_MaxValue, (double) s->maxValue, NULL);
				SIT_AddCallback(w, SITE_OnChange, SIT_SliderSync, s);
			}
		}
		break;
	default:
		return SIT_SetWidgetValue(w, call_data, user_data);
	}

	return 0;
}

/* SITE_OnMouseMove event handler of thumb */
static int SIT_SliderMove(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Slider    s   = (SIT_Slider) w;
	SIT_OnMouse * msg = cd;

	if (msg->state == SITOM_CaptureMove && s->isDragged)
	{
		REAL pad = s->gaugePadding;
		REAL sz  = w->box.right - w->box.left - pad;

		int pos = roundf((msg->x - pad * 0.5f) * (s->maxValue - s->minValue) / sz + s->minValue);

		if (pos < s->minValue) pos = s->minValue;
		if (pos > s->maxValue) pos = s->maxValue;

		if (pos != s->sliderPos)
		{
			s->sliderPos = pos;
			if (s->curValue)
				s->curValue[0] = pos;
			SIT_SliderResize(w, NULL, SEND_EVENT);
			sit.dirty = 1;
		}
	}

	return 1;
}

/* SITE_OnClick event handler inside slider */
static int SIT_SliderClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Slider    s     = (SIT_Slider) w;
	SIT_Widget    thumb = s->thumb;
	SIT_OnMouse * msg   = cd;
	int           pos   = s->sliderPos;

	switch (msg->button) {
	case SITOM_ButtonWheelUp:   msg->x = 32767; goto case_click;
	case SITOM_ButtonWheelDown: msg->x = 0; // no break;
	case SITOM_ButtonLeft:
		if (msg->state != SITOM_ButtonPressed)
		{
			if (s->dragNotify == 0 && s->isDragged)
				SIT_ApplyCallback(w, (APTR) s->sliderPos, SITE_OnChange);
			s->isDragged = 0;
			return 0;
		}
	case_click:
		if (msg->flags & SITK_FlagShift)
		{
			s->isDragged = 1;
			msg->state = SITOM_CaptureMove;
			SIT_SliderMove(w, cd, ud);
			return 1;
		}
		if (msg->x < thumb->box.left)
		{
			pos -= s->pageSize;
			if (pos < s->minValue) pos = s->minValue;
		}
		else if (msg->x > thumb->box.right)
		{
			pos += s->pageSize;
			if (pos > s->maxValue) pos = s->maxValue;
		}
		else { s->isDragged = 1; return 2; }

		if (pos != s->sliderPos)
		{
			s->sliderPos = pos;
			if (s->curValue)
				s->curValue[0] = pos;
			SIT_SliderResize(w, NULL, SEND_EVENT);
			sit.dirty = 1;
		}
	default: break;
	}
	return 1;
}

/* keyboard handling while focus is on slider */
static int SIT_SliderKbd(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Slider  s   = (SIT_Slider) w;
	SIT_OnKey * msg = cd;
	int         pos  = s->sliderPos;

	switch (msg->keycode) {
	case SITK_PrevPage: msg->flags |= SITK_FlagShift;
	case SITK_Left:
	case SITK_Up:
		pos -= msg->flags & SITK_FlagShift ? s->pageSize : 1;
		if (pos < s->minValue) pos = s->minValue;
		goto case_resize;
	case SITK_NextPage: msg->flags |= SITK_FlagShift;
	case SITK_Right:
	case SITK_Down:
		pos += msg->flags & SITK_FlagShift ? s->pageSize : 1;
		if (pos > s->maxValue) pos = s->maxValue;
		goto case_resize;
	case SITK_Home: pos = s->minValue; goto case_resize;
	case SITK_End:  pos = s->maxValue;
	case_resize:
		if (pos != s->sliderPos)
		{
			s->sliderPos = pos;
			if (s->curValue)
				s->curValue[0] = pos;
			SIT_SliderResize(w, NULL, SEND_EVENT);
			sit.dirty = 1;
		}
	}
	return 0;
}

Bool SIT_InitSlider(SIT_Widget w, va_list args)
{
	SIT_Slider s = (SIT_Slider) w;

	w->setValue = SIT_SliderSetValues;
	w->optimalWidth = SIT_SliderMeasure;
	w->flags |= SITF_PrivateChildren | SITF_RenderChildren;
	w->layout.flags |= LAYF_AdjustRect;

	/* default values */
	s->isHoriz  = True;
	s->dragNotify = True;
	s->maxValue = 100;
	s->pageSize = 10;
	s->thumbHeight = 1e6;
	s->thumbThick = 1e6;
	s->gaugePadding = 1e6;
	SIT_ParseTags(w, args, w->attrs = SliderClass);
	int pos = s->curValue ? *s->curValue : s->sliderPos;
	if (pos < s->minValue) pos = s->minValue;
	if (pos > s->maxValue) pos = s->maxValue;
	if (pos != s->sliderPos)
	{
		s->sliderPos = pos;
		if (s->curValue)
			s->curValue[0] = pos;
		if (s->buddy)
		{
			TEXT value[16];
			sprintf(value, "%d", pos);
			SIT_SetValues(s->buddy, SIT_Title, value, NULL);
		}
	}

	layoutCalcBox(w);

	if (s->thumbHeight == 1e6f)  s->thumbHeight  = SIT_EmToReal(w, SITV_Em(1.5));
	if (s->thumbThick == 1e6f)   s->thumbThick   = SIT_EmToReal(w, SITV_Em(0.5));
	if (s->gaugePadding == 1e6f) s->gaugePadding = SIT_EmToReal(w, SITV_Em(0.5));

	s->gauge = SIT_CreateWidget("gauge", SIT_HTMLTAG, w, NULL);
	s->thumb = SIT_CreateWidget("thumb", SIT_HTMLTAG, w, NULL);

	SIT_AddCallback(w, SITE_OnClick,     SIT_SliderClick,  NULL);
	SIT_AddCallback(w, SITE_OnMouseMove, SIT_SliderMove,   NULL);
	SIT_AddCallback(w, SITE_OnResize,    SIT_SliderResize, NULL);
	SIT_AddCallback(w, SITE_OnRawKey,    SIT_SliderKbd,    NULL);

	return True;
}
