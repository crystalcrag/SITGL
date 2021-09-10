/*
 * SIT_Button.c : push/toggle button, checkbox and radiobutton.
 *
 * written by T.Pierron, apr 2020.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList ButtonClass[] = {
		{ SIT_ButtonType, "buttonType", C__, SIT_INT, OFFSET(SIT_Button, type) },
		{ SIT_CheckState, "checkState", _SG, SIT_INT, OFFSET(SIT_Button, state) },
		{ SIT_RadioGroup, "radioGroup", _SG, SIT_INT, OFFSET(SIT_Button, group) },
		{ SIT_CurValue,   "curValue",   _SG, SIT_PTR, OFFSET(SIT_Button, curValue) },
		{ SIT_RadioID,    "radioID",    _SG, SIT_INT, OFFSET(SIT_Button, radioID) },
		{ SIT_TagEnd }
	};

static int SIT_ButtonMeasure(SIT_Widget w, APTR cd, APTR unused)
{
	SizeF * pref = cd;
	SizeF   ret  = *pref;
	layoutMeasureWords(w, &ret);
	ret.width  += w->padding[0] + w->padding[2];
	ret.height += w->padding[1] + w->padding[3];
	if (pref->width  < ret.width)  pref->width  = ret.width;
	if (pref->height < ret.height) pref->height = ret.height;
	return 1;
}

/* deselect all radio button except <w> in the same radio group */
static void SIT_ButtonDeselectRadio(SIT_Widget w)
{
	SIT_Widget list;
	int        group = ((SIT_Button)w)->group;
	int        type  = ((SIT_Button)w)->type;
	for (list = HEAD(w->parent->children); list; NEXT(list))
	{
		#define pb ((SIT_Button)list)
		if (list == w || list->type != SIT_BUTTON || pb->type != type || pb->group != group || ! pb->state) continue;
		list->oldState = list->state;
		list->state &= type == SITV_ToggleButton ? ~STATE_ACTIVATED : ~STATE_CHECKED;
		list->layout.flags &= ~LAYF_NoChanges;
		pb->state = 0;
		layoutUpdateStyles(list);
		#undef pb
	}
}

static int SIT_ButtonSetValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Button    button = (SIT_Button) w;
	TagList *     tag = cd;
	SIT_Variant * val = ud;

	switch (tag->tl_TagID) {
	case SIT_Title:
		w->title = val->string;
		if ((w->flags & SITF_InitDone) == 0)
			return 1;
		switch (button->type) {
		case SITV_CheckBox:
		case SITV_3StateCB:
		case SITV_RadioButton:
			if (w->style.text.align == TextAlignRight)
				SIT_AddTitle(w, " <box></box>", -1);
			else
				SIT_AddTitle(w, "<box></box> ", 0);
		}

		if (val->string)
		{
			layoutFree(w);
			layoutParseHTML(w, w->title);
		}
		w->flags |= SITF_GeometryChanged;
		break;
	case SIT_CheckState:
		if ((w->flags & SITF_PrivateChildren) == 0)
		{
			/* create-only tags not parsed yet */
			button->state = val->integer;
			return 1;
		}
		if (button->type == SITV_RadioButton)
		{
			if (val->integer) SIT_ButtonDeselectRadio(w);
			else return 0; /* can't be deselected */
		}
		button->state = val->integer;
		w->oldState = w->state;
		if (button->type <= SITV_ToggleButton)
		{
			if (button->group > 0 && val->integer) SIT_ButtonDeselectRadio(w);
			if (val->integer) w->state |=  STATE_ACTIVATED;
			else              w->state &= ~STATE_ACTIVATED;
		}
		else
		{
			if (val->integer) w->state |=  STATE_CHECKED;
			else              w->state &= ~STATE_CHECKED;
		}
		if (w->flags & SITF_InitDone)
		{
			w->layout.flags &= ~LAYF_NoChanges;
			layoutUpdateStyles(w);
			if (button->curValue)
				*button->curValue = button->type == SITV_RadioButton || (button->type == SITV_ToggleButton && button->group > 0) ? button->radioID : button->state;
		}
		break;
	case SIT_RadioGroup:
		button->group = val->integer;
		if (button->type == SITV_ToggleButton)
		{
			if (button->group > 0) w->flags |= SITF_ImmediateActive;
			else w->flags &= ~SITF_ImmediateActive;
		}
		break;
	default:
		SIT_SetWidgetValue(w, cd, ud);
	}
	return 1;
}

/* SITE_OnActivate event handler */
static int SIT_ButtonToggle(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Button button = (SIT_Button) w;
	if (button->type == SITV_RadioButton || (button->type == SITV_ToggleButton && button->group > 0))
	{
		/* can't be deselected if already selected */
		if (button->state) return 0;
		/* deslect other in group */
		SIT_ButtonDeselectRadio(w);
	}
	w->oldState = w->state;
	if (button->type != SITV_ToggleButton)
	{
		w->state ^= STATE_CHECKED;
		w->layout.flags &= ~LAYF_NoChanges;
		button->state = (w->state & STATE_CHECKED) > 0;
	}
	else button->state = (w->state & STATE_ACTIVATED) == STATE_ACTIVATED;
	if (button->curValue)
		*button->curValue = button->type == SITV_RadioButton || (button->type == SITV_ToggleButton && button->group > 0) ? button->radioID : button->state;
	layoutUpdateStyles(w);
	/* do not grab that event */
	return 0;
}

/* SITE_OnFinalize */
static int SIT_ButtonRemPtr(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Button button = (SIT_Button) w;
	SIT_Dialog dialog = ud;
	switch (button->type) {
	case SITV_DefaultButton:
		if (dialog->defButton == w)
			dialog->defButton = NULL;
		break;
	case SITV_CancelButton:
		if (dialog->cancelButton == w)
			dialog->cancelButton = NULL;
	}
	return 1;
}

/* initialize a button, check box or radio button */
Bool SIT_InitButton(SIT_Widget w, va_list args)
{
	SIT_Button button = (SIT_Button) w;

	w->optimalWidth = SIT_ButtonMeasure;
	w->setValue     = SIT_ButtonSetValues;
	button->radioID = 0xdeadbeef;

	SIT_ParseTags(w, args, w->attrs = ButtonClass);
	w->flags |= SITF_PrivateChildren;

	switch (button->type) {
	case SITV_ToggleButton:
		w->flags |= SITF_ToggleButon;
		if (button->group > 0) w->flags |= SITF_ImmediateActive;
		break;
	case SITV_DefaultButton:
	case SITV_CancelButton:
		{
			SIT_Widget diag;
			for (diag = w->parent; diag && diag->type != SIT_DIALOG; diag = diag->parent);
			if (diag)
			{
				if (button->type == SITV_DefaultButton)
					((SIT_Dialog)diag)->defButton = w;
				else
					((SIT_Dialog)diag)->cancelButton = w;
				SIT_AddCallback(w, SITE_OnFinalize, SIT_ButtonRemPtr, diag);
			}
		}
		break;
	case SITV_CheckBox:
	case SITV_3StateCB:
		w->tagName = "checkbox";
		if (button->curValue && *button->curValue && ! button->state)
			SIT_SetValues(w, SIT_CheckState, True, NULL);
		break;
	case SITV_RadioButton:
		w->tagName = "radio";
		/* assign a RadioID automatically if not already */
		if (button->radioID == 0xdeadbeef)
		{
			SIT_Widget prev;
			int        id;
			for (prev = TAIL(w->parent->children), id = 0; prev; PREV(prev))
			{
				#define	pb    ((SIT_Button)prev)
				if (prev->type == SIT_BUTTON && pb->type == SITV_RadioButton &&
				    pb->group == button->group) id ++;
				#undef pb
			}
			button->radioID = id;
			if (w->title) /* do not use this trick when name is used as a shortcut for title */
			{
				/* quick'n dirty hack: useful in serialized form, reduce declaration */
				STRPTR num = strchr(w->name, ':');
				if (num) button->radioID = strtoul(num+1, NULL, 10), *num = 0;
			}
		}
		/* already select radio in the group */
		if (button->curValue && *button->curValue == button->radioID && ! button->state)
			SIT_SetValues(w, SIT_CheckState, True, NULL);
	}
	if (button->type >= SITV_ToggleButton)
	{
		if (button->state > 0) SIT_SetValues(w, SIT_CheckState, button->state, NULL);
		SIT_AddCallback(w, SITE_OnActivate + EVT_PRIORITY(100), SIT_ButtonToggle, NULL);
	}
	layoutCalcBox(w);
	if (w->title || button->type >= SITV_CheckBox)
	{
		switch (button->type) {
		case SITV_CheckBox:
		case SITV_3StateCB:
		case SITV_RadioButton:
			if (w->style.text.align == TextAlignRight)
				SIT_AddTitle(w, " <box></box>", -1);
			else
				SIT_AddTitle(w, "<box></box> ", 0);
		}

		layoutParseHTML(w, w->title);
	}

	return True;
}
