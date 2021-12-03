/*
 * SIT_Label.c : chunk of inline text/image
 *
 * written by T.Pierron, may 2020.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <ctype.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList LabelClass[] = {
		{ SIT_ImagePath,  "imagePath",  CSG, SIT_PTR, OFFSET(SIT_Label, image) },
		{ SIT_CurrentDir, "currentDir", CSG, SIT_INT, OFFSET(SIT_Label, currentDir) },
		{ SIT_LabelSize,  "labelSize",  CSG, SIT_INT, OFFSET(SIT_Label, labelSize) },
		{ SIT_Overflow,   "overflow",   CSG, SIT_INT, OFFSET(SIT_Label, overflow) },
		{ SIT_TagEnd }
	};

/*
 * considering the width of the label is fixed, compute how many lines is required to display the text:
 * this is used to constraint long labels.
 */
int SIT_MeasureLabel(SIT_Widget w, APTR cd, APTR unused)
{
	SIT_Label label = (SIT_Label) w;
	SizeF * pref = cd;

	if ((w->layout.flags & LAYF_HasImg) == 0 && (w->title || label->labelSize > 0))
	{
		SizeF ret = *pref;
		if (label->labelSize > 0)
		{
			/* want a specific inner size */
			ret.width  = label->labelSize & 0xffff;
			ret.height = label->labelSize >> 16;
		}
		else layoutMeasureWords(w, &ret);
		ret.width  += w->padding[0] + w->padding[2];
		ret.height += w->padding[1] + w->padding[3];
		if (label->overflow == 0 && pref->width < ret.width) pref->width = ret.width;

		static uint8_t hasAttach[] = {0, 1, 1, 1, 1, 1, 0, 1};
		/* If label has no vertical constraints, force it to use space occupied by text */
		if (pref->height < ret.height || !(hasAttach[w->attachment[1].sa_Type] && hasAttach[w->attachment[3].sa_Type]))
			pref->height = ret.height;
	}
	return 1;
}

/* keep image size proportionnal (SITE_OnGeometrySet) */
static int SIT_ImageProp(SIT_Widget w, APTR cd, APTR ud)
{
	int * sz = cd; /* = [X/Y, W/H, side] */

	if (sz[2] == 1)
	{
		int constraint = 0;

		if (w->attachment[0].sa_Type != SITV_AttachNone && w->attachment[2].sa_Type != SITV_AttachNone) constraint |= 1;
		if (w->attachment[1].sa_Type != SITV_AttachNone && w->attachment[3].sa_Type != SITV_AttachNone) constraint |= 2;
		if (w->flags & SITF_FixedWidth)  constraint |= 1;
		if (w->flags & SITF_FixedHeight) constraint |= 2;

		/* nothing can be done: dimension will be reset anyway */
		if (constraint == 3) return 0;

		REAL curw = w->box.right  - w->box.left;
		REAL curh = w->box.bottom - w->box.top;

		if (constraint & 2)
		{
			/* width must be proportionnal */
			REAL minw = curh * w->minBox.width / w->minBox.height;
			if (! ALMOST0(minw - curw))
			{
				w->optimalBox.width  = minw;
				w->optimalBox.height = curh;
				w->currentBox = w->optimalBox;
				sz[2] = 0;
				return 1;
			}
		}
		else /* adjust height */
		{
			REAL minh = curw * w->minBox.height / w->minBox.width;
			if (! ALMOST0(minh - curh))
			{
				w->optimalBox.width  = curw;
				w->optimalBox.height = minh;
				w->currentBox = w->optimalBox;
				sz[2] = 0;
				return 1;
			}
		}
	}
	return 0;
}

static int SIT_SetLabelValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Label     label = (SIT_Label) w;
	TagList *     tag = cd;
	SIT_Variant * val = ud;
	CSSImage      img;

	switch (tag->tl_TagID) {
	case SIT_CurrentDir:
		if (val->integer == label->currentDir)
			break;
		label->currentDir = val->integer;
		if ((w->layout.flags & LAYF_HasImg) == 0)
			break;
	case SIT_ImagePath:
		/* only display an image: set label to the size of the image */
		img = cssAddImage(val->pointer, False, ! label->currentDir);
		if (img)
		{
			w->minBox.width  = img->width  + w->padding[0] + w->padding[2];
			w->minBox.height = img->height + w->padding[1] + w->padding[3];
			w->layout.flags |= LAYF_HasImg;
			if (label->image)
				SIT_UnloadImg(label->image);
			layoutFree(w);
			label->image = img;
			SIT_AddCallback(w, SITE_OnGeometrySet, SIT_ImageProp, NULL);
			sit.dirty = True;
		}
		else if (label->image)
		{
			w->layout.flags &= ~LAYF_HasImg;
			SIT_UnloadImg(label->image);
			label->image = NULL;
		}
		w->flags |= SITF_GeometryChanged;
		break;
	case SIT_Title:
		if ((w->flags & SITF_InitDone) == 0)
		{
			w->title = val->string;
			break;
		}
		if (w->layout.flags & LAYF_HasImg)
		{
			SIT_DelCallback(w, SITE_OnGeometrySet, SIT_ImageProp, NULL);
			w->layout.flags &= ~LAYF_HasImg;
		}
		if (w->title)
			free(w->title), w->title = NULL;
		if (val->string)
		{
			/* cancel image */
			if (label->image)
				SIT_UnloadImg(label->image);
			layoutFree(w);
			layoutParseHTML(w, w->title = val->string);
			w->optimalBox.width = w->optimalBox.height = -1;
			w->currentBox = w->optimalBox;
			sit.dirty = True;
		}
		else
		{
			free(w->layout.wordwrap.buffer);
			vector_init(w->layout.wordwrap, sizeof (struct WordWrap_t));
		}
		w->flags |= SITF_GeometryChanged;
		break;
	case SIT_Overflow:
		w->style.overflow = label->overflow = val->integer;
		sit.dirty = True;
		break;
	default:
		return SIT_SetWidgetValue(w, cd, ud);
	}
	return 1;
}

Bool SIT_InitLabel(SIT_Widget w, va_list args)
{
	w->optimalWidth = SIT_MeasureLabel;
	w->setValue     = SIT_SetLabelValues;
	w->flags       |= SITF_AutoHeight|SITF_PrivateChildren;
	w->attrs        = LabelClass;

	SIT_ParseTags(w, args, LabelClass);

	layoutCalcBox(w);
	w->flags |= SITF_InitDone;

	if (w->title)
	{
		SIT_Variant value = {.string = w->title};
		w->title = NULL;
		SIT_SetLabelValues(w, WidgetClass, &value);
	}
	/* unset until layoutCalcBox() */
	if (w->layout.flags & LAYF_HasImg)
	{
		w->minBox.width  += w->padding[0] + w->padding[2];
		w->minBox.height += w->padding[1] + w->padding[3];
	}
	return True;
}

/* EXTREMELY common use case: add a label to the left of a widget */
void SIT_CreateBuddyLabel(SIT_Widget buddy, STRPTR text, SIT_Widget * max)
{
	STRPTR name = alloca(strlen(buddy->name) + 1);

	name[0] = 'b';
	strcpy(name + 1, buddy->name);

	SIT_Widget label = SIT_CreateWidget(name, SIT_LABEL, buddy->parent,
		SIT_Title,      text,
		SIT_Top,        SITV_AttachMiddle, buddy, 0,
		SIT_MaxWidth,   max ? *max : NULL,
		NULL
	);
	SIT_SetValues(buddy, SIT_Left, SITV_AttachWidget, label, SITV_Em(0.5), NULL);
	if (max) *max = label;
}
