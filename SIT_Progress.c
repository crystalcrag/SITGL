/*
 * SIT_Progress.c: simple control that shows a bar to indicate progress
 *
 * written by T.Pierron, may 2020.
 */

#include <stdio.h>
#include <string.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	TagList ProgressClass[] = {
		{ SIT_MinValue,     "minValue",     _SG, SIT_INT,  OFFSET(SIT_Progress, min) },
		{ SIT_MaxValue,     "maxValue",     _SG, SIT_INT,  OFFSET(SIT_Progress, max) },
		{ SIT_ProgressPos,  "progressPos",  _SG, SIT_INT,  OFFSET(SIT_Progress, progressPos) },
		{ SIT_HorizScroll,  "horizScroll",  C__, SIT_BOOL, OFFSET(SIT_Progress, isHoriz) },
		{ SIT_TagEnd }
	};

/* set minimal width/height */
static int SIT_ProgressMeasure(SIT_Widget w, APTR call_data, APTR unused)
{
	SIT_Progress pb  = (SIT_Progress) w;
	SizeF *      ret = call_data;

	if (! pb->isHoriz)
		ret->width = sit.defFontHeight + 4;
	else
		ret->height = sit.defFontHeight + 4;
	return 0;
}

/* draw text progress */
static int SIT_ProgressDraw(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Progress pb = (SIT_Progress) w->parent;
	RectF *      box = cd;

	w->title = pb->super.title;
	w->layout.textarea = pb->super.layout.textarea;
	w->layout.wordSpacing = pb->super.layout.wordSpacing;
	/* we don't use inline so scissor won't be reset by background */
	nvgScissor(sit.nvgCtx, box->left, box->top, box->width, box->height);
	renderWords(w, box, 0);
	nvgResetScissor(sit.nvgCtx);
	w->title = NULL;

	return 1;
}

/* SITE_OnResize */
static int SIT_ProgressResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Progress pb  = (SIT_Progress) w;
	SIT_Widget   bar = pb->bar;
	int          pos = pb->max - pb->min;

	if (pos <= EPSILON || bar == NULL) return 0;
	REAL x = w->layout.pos.width * (pb->progressPos - pb->min) / pos;

	bar->box.left = w->padding[0];
	bar->box.top  = w->padding[1];
	bar->box.right = w->padding[0] + x;
	bar->box.bottom = w->padding[1] + w->layout.pos.height;

	SIT_LayoutCSSSize(bar);

	return 1;
}

static int SIT_ProgressPostProcess(SIT_Widget w, APTR cd, APTR ud)
{
	/* cannot process this while setting attribute: might be in a temporary invalid state */
	SIT_Progress pb = (SIT_Progress) w;
	if (w->title)
	{
		int range = pb->max - pb->min;
		if (range > 0 && range != 100 && strstr(pb->super.title, "%%"))
			/* typical use case: have a higher precision progress bar, but only display progress as [0-100] */
			range = (pb->progressPos - pb->min) * 100 / range;
		else
			range = pb->progressPos;

		snprintf(pb->textCache, sizeof pb->textCache, pb->origTitle, pb->posCache = range);
	}
	else pb->textCache[0] = 0;
	SizeF dummy;
	layoutMeasureWords(w, &dummy);
	SIT_ProgressResize(w, cd, ud);
	sit.dirty = 1;
	return 1;
}

static int SIT_ProgressSetValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Progress pb = (SIT_Progress) w;
	switch (((TagList *)cd)->tl_TagID) {
	case SIT_Title:
		if (pb->origTitle)
			w->title = pb->origTitle;
		SIT_SetWidgetValue(w, cd, ud);
		/* this will have to be restored in finalize cb */
		pb->origTitle = w->title;
		w->title = pb->textCache;
		w->postProcess = SIT_ProgressPostProcess;
		break;
	case SIT_MinValue:
	case SIT_MaxValue:
	case SIT_ProgressPos:
		if (pb->progressPos == ((SIT_Variant *)ud)->integer)
			return 0;
		w->postProcess = SIT_ProgressPostProcess;
		pb->progressPos = ((SIT_Variant *)ud)->integer;
		break;
	default:
		SIT_SetWidgetValue(w, cd, ud);
	}

	return 1;
}

static int SIT_ProgressFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Progress pb = (SIT_Progress) w;
	w->title = pb->origTitle;
	return 1;
}

Bool SIT_InitProgress(SIT_Widget w, va_list args)
{
	SIT_Progress pb = (SIT_Progress) w;

	w->optimalWidth = SIT_ProgressMeasure;
	w->setValue     = SIT_ProgressSetValues;
	w->finalize     = SIT_ProgressFinalize;
	w->flags       |= SITF_PrivateChildren | SITF_RenderChildren;
	w->layout.flags = LAYF_CompactText;

	/* default values */
	pb->isHoriz  = True;
	pb->max      = 100;
	pb->posCache = -1;
	SIT_ParseTags(w, args, w->attrs = ProgressClass);

	layoutCalcBox(w);

	if (w->postProcess)
		w->postProcess(w, NULL, NULL);

	pb->bar = SIT_CreateWidget(pb->isHoriz ? "#bar.horiz" : "#bar.vert", SIT_HTMLTAG, w, NULL);
	pb->bar->render = SIT_ProgressDraw;

	SIT_AddCallback(w, SITE_OnResize, SIT_ProgressResize, NULL);

	return True;
}
