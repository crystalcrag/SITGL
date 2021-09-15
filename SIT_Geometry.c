/*
 * SIT_Geometry.c : heart of widget layout management.
 *
 * inspired from XmForm and XmBulletinBoard widget from Motif.
 *
 * written by T.Pierron, june 2006
 */

#include <string.h>
#include <stddef.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

//#define	DEBUG_GEOM     /* warning: verbose */

/* 1 if sa_Arg points to a SIT_Widget according to sa_Type (SITV_Attach*) */
static uint8_t relative[] = {0, 0, 0, 1, 1, 1, 1, 0};

/* check how much the widget <w> will adjust its edge[side] relative to width/height of its container [0-1.0] */
static REAL SIT_PercentMovable(SIT_Widget w, int side)
{
	SIT_Widget sub;
	for (sub = w; ; )
	{
		static char opposite[] = {2,3,0,1};
		SIT_Attach * a = &sub->attachment[side];
		switch (a->sa_Type) {
		case SITV_AttachNone:
			if (w == sub) return 0;
			a += (side >= 2 ? -2 : 2);
			if (a->sa_Type == SITV_AttachNone) return 0;
			side = opposite[side];
			break;
		case SITV_AttachForm:
			return side >= 2 ? 1 : 0;
		case SITV_AttachPosition:
			return a->sa_Arg * (1/65536.);
		case SITV_AttachWidget:
		case SITV_AttachMiddle:
		case SITV_AttachNoOverlap:
			sub = (APTR) a->sa_Arg;
			side = opposite[side];
			break;
		case SITV_AttachOpposite:
			sub = (APTR) a->sa_Arg; /* check same side */
			break;
		case SITV_AttachFixed:
			return 0;
		}
		if (sub == NULL) return 0;
	}
}

/* temporarily reassign attachment because relative widget is hidden */
static SIT_Attach * SIT_ReassignAttachment(SIT_Widget w, int side)
{
	SIT_Attach * a;
	do {
		a = w->attachment + side;
	} while (relative[a->sa_Type] && (w = (SIT_Widget) a->sa_Arg) && ! w->visible);
	return a;
}

static SizeF SIT_GetContentBox(SIT_Widget w)
{
	SizeF sz;

	sz.width  = w->box.right  - w->box.left;
	sz.height = w->box.bottom - w->box.top;

	return sz;
}

/*
 * Core layout engine - compute widget position
 */
int SIT_LayoutWidget(SIT_Widget root, SIT_Widget w, int side /* 0: horiz, 1:vert */, ResizePolicy adjust)
{
	static int sideAttach[] = {LAYF_HasLeftAttach, LAYF_HasTopAttach, LAYF_HasRightAttach, LAYF_HasBottomAttach};
	static int sideOpp[]    = {LAYF_HasRightAttach, LAYF_HasBottomAttach, LAYF_HasLeftAttach, LAYF_HasTopAttach};
	static int sideBoth[]   = {LAYF_HasLeftAttach|LAYF_HasRightAttach, LAYF_HasTopAttach|LAYF_HasBottomAttach};
	SIT_Widget   s; /* sibling */
	SIT_Attach * a;
	REAL *       p; /* positions */
	int          i, inc, hasPos;
	REAL         padTL, padBR, pad, sz, margin;
	REAL         percent[2];
	REAL         chldsz = (adjust == FitUsingOptimalBox ? &w->optimalBox.width : &w->currentBox.width)[side];

	if (adjust == FitUsingInitialBox && (w->flags & (SITF_FixedWidth<<side)))
		chldsz = (&w->fixed.width)[side];

	/* AttachNone and AttachNoOverlap must be computed after any other kind */
	inc = w->attachment[side].sa_Type == SITV_AttachNone ||
	      w->attachment[side].sa_Type == SITV_AttachNoOverlap ? -2 : 2;

	/* margins */
	padTL = root->padding[side];
	padBR = root->padding[side+2];
	pad   = padTL+padBR;
	hasPos = 0;
	sz = (&root->box.right)[side] - (&root->box.left)[side];
	percent[0] = SIT_PercentMovable(w, side);
	percent[1] = SIT_PercentMovable(w, side+2);
	/* handle horizontal and vertical constraints */
	for (i = side + (inc < 0 ? 2 : 0), p = &w->box.left + i, s = NULL; 0 <= i && i < 4; i += inc, p += inc)
	{
		a = w->attachment + i;
		margin = a->sa_Offset;
		if (margin == SITV_NoPad)
			margin = - (&root->layout.padding.top)[(i+3)&3] + a->sa_Arg;
		else if (margin == SITV_BorderBox)
			margin = - root->padding[(i+3)&3] + a->sa_Arg;

		if (relative[a->sa_Type] && (s = (SIT_Widget) a->sa_Arg) && ! s->visible)
		{
			a = SIT_ReassignAttachment(s, i);
			s = (SIT_Widget) a->sa_Arg;
			if (w->attachment[i].sa_Type == SITV_AttachOpposite)
				margin = a->sa_Offset;
		}

		switch (a->sa_Type) {
		case SITV_AttachFixed: /* let it where user set it */
			*p = (&w->fixed.left)[i] + margin + padTL;
			if (i > 1) *p += (&w->box.left)[side];
			break;
		case SITV_AttachForm: /* edge of container */
			root->layout.flags |= sideAttach[i];
			if (i > 1)
			{
				root->flags |= SITF_Style1Changed << side;
				if (margin < 0)
				{
					if (- margin >= padBR) padBR = margin = 0;
					else padBR += margin, margin = 0;
				}
				*p = sz - padBR - margin;
			}
			else
			{
				if (margin < 0)
				{
					if (- margin >= padTL) padTL = margin = 0;
					else padTL += margin, margin = 0;
				}
				*p = padTL + margin;
			}
			break;
		case SITV_AttachPosition: /* % of container */
			hasPos = 1;
			root->flags |= SITF_Style1Changed << side;
			root->layout.flags |= sideBoth[i&1];
			if (margin == SITV_OffsetCenter)
			{
				margin = roundf(- chldsz / 2 + padTL);
			}
			else if (i > 1)
			{
				if (margin < 0)
				{
					if (- margin >= padBR) padBR = margin = 0;
					else padBR += margin, margin = 0;
				}
				margin += padBR;
			}
			else
			{
				if (margin < 0)
				{
					if (- margin >= padTL) padTL = margin = 0;
					else padTL += margin, margin = 0;
				}
				margin += padTL;
			}
			*p = floor(((sz-pad) * (int) a->sa_Arg + 32768) * (1/65536.)) + margin;
			break;
		case SITV_AttachWidget: /* relative to another widget */
			s->layout.flags |= sideOpp[i];
			if (i > 1) *p = (&s->box.left)[i - 2] - margin;
			else       *p = (&s->box.left)[i + 2] + margin;
			break;
		case SITV_AttachOpposite: /* relative to opposite border it used to */
			s = (SIT_Widget) a->sa_Arg;
			s->layout.flags |= sideAttach[i];
			*p = margin + (&s->box.left)[i];
			break;
		case SITV_AttachMiddle: /* relative to middle of another widget */
			s = (SIT_Widget) a->sa_Arg;
			s->layout.flags |= sideBoth[i&1];
			*p = roundf(((&s->box.left)[i&1] + (&s->box.left)[(i&1) + 2] - chldsz) / 2);
			break;
		case SITV_AttachNone: /* let it where it is */
		case SITV_AttachNoOverlap: /* mix between None and Widget */
			if (i > 1) *p = p[-2] + chldsz;
			else       *p = p[ 2] - chldsz;
			if (a->sa_Type != SITV_AttachNoOverlap) break;

			s = (SIT_Widget) a->sa_Arg;
			s->layout.flags |= sideOpp[i];
			if (i > 1)
			{
				margin = (&s->box.left)[i - 2] - margin;
				if (*p > margin) margin = *p - margin, *p -= margin, p[-2] -= margin;
			} else {
				margin = (&s->box.left)[i + 2] + margin;
				if (*p < margin) margin -= *p, *p += margin, p[2] += margin;
			}
		}
		/* round first to integer pos: sharper lines */
	}
	/* do not set width/height below a minimum */
	p = &w->box.left + side;

//	p[0] = roundf(p[0]);
//	p[2] = roundf(p[2]);

	if (HAS_EVT(w, SITE_OnGeometrySet))
	{
		/* custom callback for setting geometry */
		int sz[] = {p[0], p[2], side};
		sz[1] -= sz[0]; /* X/Y, W/H, side */
		if (SIT_ApplyCallback(w, sz, SITE_OnGeometrySet))
		{
			if (sz[2] < side)
			{
				w->flags |= SITF_NoResetSize;
				return -1;
			}
			if (sz[1] != p[2] - p[0])
			{
				chldsz = sz[1];
				(&w->box.left)[side+2] = (&w->box.left)[side] + sz[1];
			}
			p[0] = sz[0];
			p[2] = sz[0] + sz[1];
			w->flags |= SITF_NoResetSize;
			adjust = FitUsingOptimalBox;
		}
	}

	/* check if container box has to be enlarged */
	REAL * cside = &root->box.right + side;
	int    ret   = 1;

	if (p[0] < padTL)
	{
		if (percent[0] > 0 && adjust)
		{
			ret = 0;
			if (p[2] > *cside) *cside = p[2] + padBR;
			*cside += padTL - p[0];
		}
		else p[2] += padTL - p[0], p[0] = padTL;
	}
	if (adjust == KeepDialogSize && ! (root->flags & (side?SITF_ReflowH:SITF_ReflowW)))
		return 1;
	a = &w->attachment[2+side];
	/* check if overlapping */
	if (relative[a->sa_Type] && (s = (SIT_Widget)a->sa_Arg) && s->visible)
		margin = (&s->box.left)[a->sa_Type == SITV_AttachOpposite ? side+2 : side] - a->sa_Offset;
	else
		margin = *cside - padBR - cside[-2];
	if (root->flags & (SITF_FixedWidth << side))
		sz = (&root->fixed.width)[side];
	else if ((sz = (&root->maxBox.width)[side]) <= 0)
		sz = (&sit.scrWidth)[side] * 2;

	/* hasPos: if left/top is set to be relative pos, only check that size fits the control, not position */
	if (hasPos == 0 && p[2] > margin)
		ret = 0, *cside += p[2] - margin;

	if (p[2] - p[0] < chldsz /*(&w->optimalBox.width)[side]*/ && percent[1] > percent[0])
	{
		int newsz = *cside + (p[0] + chldsz - p[2]) / (percent[1] - percent[0]);
		if (newsz > sz)
			newsz = sz;
		if (*cside != newsz)
		{
			ret = newsz - *cside;
			/* check if we can adjust control size instead of container */
			if (ret > 0 && adjust != FitUsingOptimalBox && chldsz - (&w->optimalBox.width)[side] >= ret)
				;
			else
				*cside = newsz, ret = 0;
		}
	}
	return ret;
}

/* children's box have been setup, calc their pos in the container */
static int SIT_LayoutChildren(SIT_Widget root, ResizePolicy mode)
{
	SIT_Widget list;
	int        count, total, i, init, loop;
	SizeF      min = {0, 0};

	/* apply maxWidth constraints */
	for (list = HEAD(root->children), total = count = 0; list; NEXT(list))
	{
		if ((list->flags & SITF_TopLevel) || ! list->visible) continue;
		total++;
		list->layout.flags &= ~LAYF_HasAttach;

		if ((list->max.ln_Prev == NULL && list->max.ln_Next == NULL) ||
		    (list->flags & SITF_Style1Changed)) continue;

		SIT_Widget w, n;
		int box = (mode == FitUsingOptimalBox ?
			offsetof(struct SIT_Widget_t, optimalBox.width) :
			offsetof(struct SIT_Widget_t, currentBox.width));

		#define CXVAL(w)    (* (REAL *) (((STRPTR)w) + box))
		REAL minw;
		for (w = list; w->max.ln_Prev; w = (SIT_Widget) w->max.ln_Prev);
		for (n = w, minw = 0; w; w = (SIT_Widget) w->max.ln_Next)
		{
			if (! w->visible) continue;
			if (minw < CXVAL(w)) minw = CXVAL(w);
			w->flags |= SITF_Style1Changed;
			count ++;
		}
		for (w = n; w; CXVAL(w) = w->childBox.width = minw, w = (SIT_Widget) w->max.ln_Next);
		#undef CXVAL
	}

	/* clear flags */
	if (count > 0)
		for (list = HEAD(root->children); list; list->flags &= ~SITF_Style1Changed, NEXT(list));

	#ifdef DEBUG_GEOM
	fprintf(stderr, "=== %s: %s (%gx%gx%gx%g): min: [%g, %g]", root->name, "Keep\0   Optimal\0Current\0Initial" + mode*8,
		root->box.left, root->box.top, root->box.right, root->box.bottom, root->minBox.width, root->minBox.height);
	#endif

	/* perform 2 steps: one for horizontal constraints, a second for vertical */
	for (i = 0; i < 2; i ++)
	{
		redo_from_start:
		root->flags &= ~SITF_StylesChanged;
		for (init = 0, list = HEAD(root->children); list; NEXT(list))
		{
			int flags = (SITF_FixedX | SITF_FixedWidth) << i;

			if (! list->visible || (list->flags & SITF_TopLevel)) continue;

			/* set by user geom callback */
			if ((list->flags & SITF_NoResetSize) == 0)
			{
				/* already set box if geometry is fixed */
				if (i == 0) list->box.right  = list->fixed.width  + (list->box.left = list->fixed.left);
				else        list->box.bottom = list->fixed.height + (list->box.top  = list->fixed.top);
			}
			list->flags &= ~ (SITF_GeometrySet | SITF_GeometryChanged | SITF_NoResetSize);
			if ((list->flags & flags) == flags)
				list->flags |= SITF_GeometrySet, init ++;
		}

		for (loop = init; loop < total; loop += count)
		{
			for (count = 0, list = HEAD(root->children); list; NEXT(list))
			{
				static STRPTR side[] = {"left", "top", "right", "bottom"};

				SIT_Attach * a = SIT_ReassignAttachment(list, i); /* left / top */
				SIT_Widget   w = (SIT_Widget) a->sa_Arg;

				/* already done */
				if (! list->visible || (list->flags & (SITF_TopLevel|SITF_GeometrySet)))
					continue;

				if (relative[a->sa_Type])
				{
					if (w == NULL)
						SIT_Log(SIT_CRITICAL, "Missing relative %s control for '%s'", side[i], list->name);
					/* there is dependency with a widget, but priority has not yet been determined */
					if ((w->flags & SITF_GeometrySet) == 0 && w->visible) continue;
				}

				a = SIT_ReassignAttachment(list, i + 2); /* Right / bottom */
				w = (SIT_Widget) a->sa_Arg;

				if (relative[a->sa_Type])
				{
					if (w == NULL)
						SIT_Log(SIT_CRITICAL, "Missing relative %s control for '%s'", side[i+2], list->name);
				    if ((w->flags & SITF_GeometrySet) == 0 && w->visible) continue;
				}

				list->flags |= SITF_GeometrySet;
				switch (SIT_LayoutWidget(root, list, i, mode)) {
				case -1: i = 0; goto redo_from_start;
				case  0: if (root->flags & (SITF_Style1Changed << i)) goto redo_from_start; /* container box changed, need to recompute everything */
				/* else: ok */
				}

				/* control that have synchronized width/height (geomtest2) */
				if (min.width  < list->box.right)  min.width  = list->box.right;
				if (min.height < list->box.bottom) min.height = list->box.bottom;

				count ++;
			}

			/* ooops, dependency loop */
			if (count == 0 && total > 0) return 0;
		}
		/* root box gets enlarged only in SIT_LayoutWidget. if total == 0, it will never get here */
//		if (total == 0 && (&root->box.right)[i] <= 0)
//			(&root->box.right)[i] += root->sw_Margins[i] + root->sw_Margins[i + 2];
	}
//	root->box.right  += root->padding[0] + root->padding[2];
//	root->box.bottom += root->padding[1] + root->padding[3];
	#ifdef DEBUG_GEOM
	fprintf(stderr, " box: %g, %g - %g, %g:\n", root->box.left, root->box.top, root->box.right, root->box.bottom);
	for (list = HEAD(root->children); list; NEXT(list))
	{
		if (list->visible && (list->flags & SITF_TopLevel) == 0)
			fprintf(stderr, "    %s: %gx%g - %gx%g (%gx%g)\n",
				list->name, list->box.left, list->box.top, list->box.right,
				list->box.bottom, list->box.right - list->box.left, list->box.bottom - list->box.top);
	}
	#endif

	/* check if width/height can be reduced, otherwise it will only be enlarged */
	if ((root->flags & SITF_CanResizeW) == 0)
	{
		/* note: left padding already added in SIT_LayoutWidget() */
		min.width += root->padding[2];
		if ((root->flags & SITF_FixedWidth) == 0 && min.width < root->box.right - root->box.left)
			root->box.right = min.width + root->box.left;
	}
	if ((root->flags & SITF_CanResizeH) == 0)
	{
		min.height += root->padding[3];
		/* SIT_TAB requirement XXX SITF_AutoHeight check */
		if (root->minBox.height > 0 && min.height < root->minBox.height)
			min.height = root->minBox.height;
		if ((root->flags & SITF_FixedHeight) == 0 && min.height < root->box.bottom - root->box.top)
			root->box.bottom = min.height + root->box.top;
	}

	root->childBox.width = root->box.right  - root->box.left;
	root->childBox.height = root->box.bottom - root->box.top;

	return 1;
}

static SizeF SIT_GeomGetMaxBox(SIT_Widget w)
{
	SIT_Widget list;
	SizeF      ret = {1e6, 1e6};
	for (list = w; list; list = list->parent)
	{
		REAL max = list->maxBox.width;
		if (max > list->minBox.width && max < ret.width) ret.width = max;
		max = list->maxBox.height;
		if (max > list->minBox.height && max < ret.height) ret.height = max;
		if (list->flags & SITF_TopLevel) break;
	}
	if (ret.width  == 1e6) ret.width  = -1;
	if (ret.height == 1e6) ret.height = -1;
	return ret;
}

static Bool SIT_CanChangeContainerSize(SIT_Widget w, SizeF * oldSz, SizeF * newSz, int side)
{
	SIT_Widget   c = w->parent;
	SIT_Attach * a = w->attachment + side;
	REAL       * p = &w->box.left + side;

	if (w->flags & SITF_TopLevel) return False;

	#if 0 /* only bottom/right corner should matter ? */
	if (a->sa_Type == SITV_AttachNone)
	{
		REAL pad = c->padding[side];
		/* top/left set to none: check if get to touch container bbox */
		if (p[0] < pad || p[2] - (&oldSz->width)[side] == pad) return True;
	}
	#endif
	a += 2;
	p += 2;
	if (a->sa_Type == SITV_AttachNone)
	{
		/* bottom/right set to none */
		REAL * csize = &c->box.left + side;
		REAL bottom = csize[2] - csize[0] - c->padding[side+2];
		// XXX might not be set yet
		// (&c->layout.pos.width)[side] + c->padding[side];
		if (p[0] >= bottom || p[-2] + (&oldSz->width)[side] >= bottom) return True;
		if (newSz && (&newSz->width)[side] >= bottom) return True;
	}
	return False;
}

/* compute optimal width and height for dialog : used to constraint resizing */
static int SIT_LayoutOptimal(SIT_Widget root)
{
	SIT_Widget list;

	/* resize capabilities: we need that information very early */
	root->flags &= ~(SITF_CanResizeW | SITF_CanResizeH);
	if (root->parent == NULL)
	{
		/* fixed size no matter what */
		root->box.right  = root->fixed.width;
		root->box.bottom = root->fixed.height;
	}
	else if (root->type == SIT_DIALOG)
	{
		for (list = HEAD(root->children); list; NEXT(list))
		{
			if ((list->flags & SITF_TopLevel) || !list->visible) continue;
			SIT_Attach * a;
			a = SIT_ReassignAttachment(list, 0); if (a->sa_Type == SITV_AttachPosition && a->sa_Arg > 0) root->flags |= SITF_CanResizeW;
			a = SIT_ReassignAttachment(list, 1); if (a->sa_Type == SITV_AttachPosition && a->sa_Arg > 0) root->flags |= SITF_CanResizeH;
			a = SIT_ReassignAttachment(list, 2); if (a->sa_Type == SITV_AttachForm || a->sa_Type == SITV_AttachPosition) root->flags |= SITF_CanResizeW;
			a = SIT_ReassignAttachment(list, 3); if (a->sa_Type == SITV_AttachForm || a->sa_Type == SITV_AttachPosition) root->flags |= SITF_CanResizeH;
		}
		SIT_Widget client = ((SIT_Dialog)root)->clientArea;

		if (client)
		{
			client->optimalBox.width = client->optimalBox.height = -1;
			memset(&client->box, 0, sizeof client->box);
		}
		memset(&root->box, 0, sizeof root->box);
	}
	else /* Check if size of control is constrained */
	{
		if (SIT_PercentMovable(root, 0) < SIT_PercentMovable(root, 2)) root->flags |= SITF_CanResizeW;
		if (SIT_PercentMovable(root, 1) < SIT_PercentMovable(root, 3)) root->flags |= SITF_CanResizeH;
		root->maxBox = SIT_GeomGetMaxBox(root);
		if (IsDef(root->title))
			layoutMeasureWords(root, &root->optimalBox);
		memset(&root->box, 0, sizeof root->box);
	}

	/*
	if (root->flags & SITF_FixedWidth)  root->box.right  = root->fixed.width;
	if (root->flags & SITF_FixedHeight) root->box.bottom = root->fixed.height;
	*/

	/* reset children box dimension */
	for (list = HEAD(root->children); list; NEXT(list))
	{
		if ((list->flags & SITF_TopLevel) || ! list->visible) continue;

		memset(&list->box, 0, sizeof list->box);

		if (list->flags & SITF_FixedX) list->attachment[0].sa_Type = SITV_AttachFixed;
		if (list->flags & SITF_FixedY) list->attachment[1].sa_Type = SITV_AttachFixed;

		if (layoutSizeChanged(list))
			layoutCalcBox(list);

		if ((list->optimalBox.width < 0 || list->optimalBox.height < 0) && list->optimalWidth)
		{
			/* SIT_MinWidth or SIT_MinHeight set */
			SizeF pref = list->minBox;
			if ((list->flags & SITF_FixedWidth)  && pref.width  < 0) pref.width  = list->fixed.width;
			if ((list->flags & SITF_FixedHeight) && pref.height < 0) pref.height = list->fixed.height;
			list->optimalWidth(list, &pref, (APTR) FitUsingOptimalBox);
			list->childBox.width  = roundf(pref.width);
			list->childBox.height = roundf(pref.height);

			if (pref.width  < list->minBox.width)  pref.width  = list->minBox.width;
			if (pref.height < list->minBox.height) pref.height = list->minBox.height;
			SizeF maxBox = SIT_GeomGetMaxBox(list);
			if (maxBox.width  > list->minBox.width  && pref.width  > maxBox.width)  pref.width  = maxBox.width;
			if (maxBox.height > list->minBox.height && pref.height > maxBox.height) pref.height = maxBox.height;
			/* some controls need explicit geometric constraint to have a width/height */
			if (pref.width  < 0) pref.width  = 0;
			if (pref.height < 0) pref.height = 0;
			list->optimalBox = pref;
		}

		/* if frame/dialog is not resizable, optimal box will be like initial box */
		int sz = list->fixed.width;
		if (SIT_PercentMovable(list, 0) >= SIT_PercentMovable(list, 2) && list->minBox.width < 0 &&
			(list->flags & SITF_FixedWidth) && list->optimalBox.width < sz)
			list->optimalBox.width = sz;
		sz = list->fixed.height;
		if (SIT_PercentMovable(list, 1) >= SIT_PercentMovable(list, 3) && list->minBox.height < 0 &&
			(list->flags & SITF_FixedHeight) && list->optimalBox.height < sz)
			list->optimalBox.height = sz;
	}

	/* now that size of controls is known: compute position and size of root */
	SIT_LayoutChildren(root, FitUsingOptimalBox);

	root->optimalBox.width  = root->box.right  - root->box.left;
	root->optimalBox.height = root->box.bottom - root->box.top;

	if (root->type == SIT_DIALOG && ((SIT_Dialog)root)->minSize.width == 0)
	{
		SIT_Dialog d = (APTR) root;
		d->minSize = root->optimalBox;
		d->maxSize.width = (root->flags & SITF_CanResizeW ? 65535 : d->minSize.width);
		d->maxSize.height = (root->flags & SITF_CanResizeH ? 65535 : d->minSize.height);
	}
	root->flags &= ~ (SITF_GeometrySet | SITF_GeometryChanged | SITF_GeomNotified);

	return 0;
}

static int BoxSizeDiffers(SizeF * box1, SizeF * box2)
{
	return ! (ALMOST0(box1->width - box2->width) && ALMOST0(box1->height - box2->height));
}

/*
 * - FitUsingOptimalBox: compute minimal size that dialog can be resized.
 * - KeepDialogSize: used when dialog is being resized by user and/or programmatically.
 *                   Possible reflow due to label/tab constraint.
 * - FitUsingCurrentBox: changes were performed on some controls (geometry, ctrl hidden /
 *                       removed, title, ...) which change its bounding box. Recompute position
 *                       of surrounding controls and adjust dialog size if required.
 * - FitUsingInitialBox: like OptimalBox, but use SIT_Width and SIT_Height information.
 *
 * warning: this function is not trivial. Handle with care.
 */
static int SIT_LayoutInitial(SIT_Widget root, ResizePolicy mode)
{
	SIT_Widget list;
	int        count;

	if (mode != KeepDialogSize)
	{
		if (root->flags & SITF_GeometryChanged)
		{
		#if 0
			offsetx = root->box.left;
			offsety = root->box.top;
			root->box.right  -= offsetx + root->sw_NCMargins[0], root->box.left = 0,
			root->box.bottom -= offsety + root->sw_NCMargins[1], root->box.top = 0;
		#endif
		}
		else if (mode != FitUsingCurrentBox)
		{
			memset(&root->box, 0, sizeof root->box);
		}

		/* user already set the size of container box */
		#if 0
		/* XXX <html> container should not be enlarged more than max size */
		if (root->flags & SITF_FixedWidth)
			root->box.right  = root->box.left+MAX(root->fixed.width, root->optimalBox.width);
		if (root->flags & SITF_FixedHeight)
			root->box.bottom = root->box.top+MAX(root->fixed.height, root->optimalBox.height);
		#else
		if (root->flags & SITF_FixedWidth)  root->box.right  = root->box.left+root->fixed.width;
		if (root->flags & SITF_FixedHeight) root->box.bottom = root->box.top+root->fixed.height;
		#endif
		if (root->type != SIT_DIALOG)
		{
			REAL min = root->minBox.width;
			if (min <= 0)
				min = root->optimalBox.width;
			if (root->box.right - root->box.left < min)
				root->box.right = root->box.left + min;

			min = root->minBox.height;
			if (min <= 0)
				min = root->optimalBox.height;
			if ((root->flags & SITF_AutoHeight) == 0 && root->box.bottom - root->box.top < min)
				root->box.bottom = root->box.top + min;
		}
		else
		{
			/* check if edges are constrainted */
			if (root->attachment[0].sa_Type != SITV_AttachNone &&
			    root->attachment[2].sa_Type != SITV_AttachNone)
			    SIT_LayoutWidget(root->parent, root, 0, mode);

			if (root->attachment[1].sa_Type != SITV_AttachNone &&
			    root->attachment[3].sa_Type != SITV_AttachNone)
			    SIT_LayoutWidget(root->parent, root, 1, mode);
		}
	}

	/* setup initial box if needed */
	for (list = HEAD(root->children); list; NEXT(list))
	{
		if ((list->flags & SITF_TopLevel) || ! list->visible) continue;

		memset(&list->box, 0, sizeof list->box);

		switch (mode) {
		case FitUsingOptimalBox: /* will use optimalBox */ break;
		case FitUsingCurrentBox:
			if (list->currentBox.width > 0 && list->currentBox.height > 0 &&
			    ! (list->flags & SITF_GeomNotified)) break;
			// no break;
		case FitUsingInitialBox:
			if (list->children.lh_Head && (list->flags & SITF_PrivateChildren) == 0)
			{
				if ((list->currentBox.width == 0 || list->currentBox.height == 0) || (list->flags & SITF_GeomNotified))
				{
					if (list->flags & SITF_FixedWidth)  list->box.right = list->fixed.width;
					if (list->flags & SITF_FixedHeight) list->box.bottom = list->fixed.height;
					SIT_LayoutInitial(list, mode);
					list->currentBox = SIT_GetContentBox(list);
					// XXX reflow ? this two lines are for toolbar with children
					REAL min = list->minBox.width;
					if (min <= 0) min = list->optimalBox.width;
					if (min >= 0 && list->currentBox.width < min) list->currentBox.width = min;
					min = list->minBox.height;
					if (min <= 0 && (list->flags & SITF_AutoHeight) == 0) min = list->optimalBox.height;
					if (min >= 0 && list->currentBox.height < min) list->currentBox.height = min;
					if (list->children.lh_Head == NULL)
					{
						/* problem with label and reflow */
						if (list->currentBox.width < list->optimalBox.width)
							list->currentBox.width = list->optimalBox.width;
						if (list->currentBox.height < list->optimalBox.height)
							list->currentBox.height = list->optimalBox.height;
					}
				}
				/* avoid doing the same stuff again */
				list->childBox = list->currentBox;
			}
			else
			{
		case KeepDialogSize: /* yeah, I know, it looks weird, but this is valid C */
				/* fixed height / width */
				if (list->currentBox.width <= 0)
					list->currentBox.width = list->flags & SITF_FixedWidth ? list->fixed.width : list->optimalBox.width;
				if (list->currentBox.height <= 0)
					list->currentBox.height = list->flags & SITF_FixedHeight ? list->fixed.height : list->optimalBox.height;
			}
			if (mode == FitUsingInitialBox)
			{
				SizeF pref = list->childBox;
				if ((list->flags & SITF_FixedWidth) && list->fixed.width > list->currentBox.width)
					list->currentBox.width = list->fixed.width;
				if ((list->flags & SITF_FixedHeight) && list->fixed.height > list->currentBox.height)
					list->currentBox.height = list->fixed.height;
				if ((list->flags & SITF_AutoHeight) && BoxSizeDiffers(&pref, &list->currentBox))
					list->optimalWidth(list, &list->currentBox, (APTR) FitUsingInitialBox);
			}
		}
	}

	for (count = 0; count < 10; count ++)
	{
		int reflow = 0;
		SIT_LayoutChildren(root, mode);
		root->flags &= ~ (SITF_ReflowH|SITF_ReflowW);

		/* also check if children's children need reflow */
		for (list = HEAD(root->children), count = 0; list; NEXT(list))
		{
			if (! list->visible || (list->flags & SITF_TopLevel)) continue;
			list->currentBox = SIT_GetContentBox(list);

			if (((list->flags & SITF_AutoHeight) || (list->children.lh_Head && !(list->flags & SITF_PrivateChildren))) &&
			    BoxSizeDiffers(&list->currentBox, &list->childBox))
			{
				#ifdef DEBUG_GEOM
				fprintf(stderr, "%s: current box: %gx%g, child box: %gx%g\n", list->name, list->currentBox.width,
					list->currentBox.height, list->childBox.width, list->childBox.height);
				#endif
				SizeF old = list->currentBox;
				if (list->optimalWidth(list, &list->currentBox, (APTR) (mode == FitUsingInitialBox ? FitUsingCurrentBox : mode)) == 1)
					list->childBox = list->currentBox;

				/* check if we really need to reflow everything */
				if (old.width != list->currentBox.width)
				{
					if (SIT_CanChangeContainerSize(list, &old, &list->currentBox, 0) || (list->flags & (LAYF_HasLeftAttach|LAYF_HasRightAttach)))
						reflow |= 1;
					else
						list->box.right = list->box.left + list->currentBox.width;
				}
				if (old.height != list->currentBox.height)
				{
					if (SIT_CanChangeContainerSize(list, &old, &list->currentBox, 1) || (list->flags & (LAYF_HasTopAttach|LAYF_HasBottomAttach)))
						reflow |= 2;
					else
						/* XXX wrapped label with middle attach on top need top pos recalc too */
						SIT_LayoutWidget(root, list, 1, mode);
						//list->box.bottom = list->box.top + list->currentBox.height;
				}
			}

			if (list->children.lh_Head == NULL || (list->flags & SITF_PrivateChildren)) continue;

			/*
			 * optimization: some widget will already compute optimal box recursively. This
			 * will avoid numerous steps redoing the same thing all over again. Used by KeepDialogSize policy.
			 */
			if (BoxSizeDiffers(&list->childBox, &list->currentBox))
			{
				// fprintf(stderr, "force recalc for '%S' %ld != %d\n", list->name, list->childBox.width, list->box.right - list->box.left);
				list->flags |= SITF_GeometryChanged;
				if (SIT_LayoutInitial(list, mode)) return 1;
				if (list->childBox.width != list->currentBox.width)
				{
					/* widget size has changed, need to layout children again */
					if (list->flags & SITF_CanResizeW) reflow |= 1;
					/* unless widget size is constrained, keep constrained size instead */
					else list->box.right = list->box.left + list->currentBox.width;
				}
				if (list->childBox.height != list->currentBox.height)
				{
					reflow |= 2;
					if (list->flags & SITF_CanResizeH) reflow |= 2;
					else list->box.bottom = list->box.top + list->currentBox.height;
				}
				list->currentBox = list->childBox = SIT_GetContentBox(list);
			}
		}
		if (! reflow) break;
		if (root->parent)
		{
			/* root widget cannot be resized */
			if (reflow & 1) root->box.left = root->box.right  = 0, root->flags |= SITF_ReflowW;
			if (reflow & 2) root->box.top  = root->box.bottom = 0, root->flags |= SITF_ReflowH;
		}
	}
	root->flags &= ~ (SITF_GeometrySet | SITF_GeometryChanged | SITF_GeomNotified);
	return 0;
}

static Bool SIT_HasNoOptimalValue(SIT_Widget root)
{
	SIT_Widget list;

	if (root->type == SIT_DIALOG && ((SIT_Dialog)root)->minSize.width == 0)
		return True;
	if ((root->flags & SITF_TopLevel) && (root->optimalBox.width < 0 && root->optimalBox.height < 0))
		return True;

	for (list = HEAD(root->children); list; )
	{
		if ((list->flags & SITF_TopLevel) == 0 && list->visible && list->optimalWidth &&
			 list->optimalBox.width < 0 && list->optimalBox.height < 0) return True;
		if (list->children.lh_Head == NULL || (list->flags & SITF_PrivateChildren))
		{
			while (! list->node.ln_Next)
			{
				list = list->parent;
				if (list == root) return False;
			}
			NEXT(list);
		}
		else list = HEAD(list->children);
	}
	return False;
}

/* sigh: SIT uses border-box as for dimension (in <box> field), CSS uses content-box in layout.pos */
void SIT_LayoutCSSSize(SIT_Widget root)
{
	/* in SIT_Geometry we only set <box> */
	SIT_Widget c;
	SizeF      size;
	REAL       pad[4];

	memcpy(pad, root->padding, sizeof pad);
	size.width  = root->layout.pos.width;
	size.height = root->layout.pos.height;
	root->layout.pos.left   = root->box.left + pad[0];
	root->layout.pos.top    = root->box.top  + pad[1];
	root->layout.pos.width  = root->box.right  - pad[2] - root->layout.pos.left;
	root->layout.pos.height = root->box.bottom - pad[3] - root->layout.pos.top;
	c = root->parent;
	if (c && root->type != SIT_DIALOG)
	{
		root->offsetX = c->offsetX + c->box.left;
		root->offsetY = c->offsetY + c->box.top;
	}
	layoutAdjustBorderRadius(root);
	layoutAlignText(root);
	root->style.flags &= ~CSSF_BORDERIMG;

	if (root->type == SIT_DIALOG && (root->attachment[3].sa_Type != SITV_AttachNone || root->attachment[2].sa_Type != SITV_AttachNone))
		SIT_CenterDialog(root);

	/* trigger SITE_OnResize event if needed */
	if (HAS_EVT(root, SITE_OnResize) && ! (ALMOST0(size.width-root->layout.pos.width) && ALMOST0(size.height-root->layout.pos.height)))
		SIT_ApplyCallback(root, &root->layout.pos.width, SITE_OnResize);

	for (c = HEAD(root->children); c; NEXT(c))
	{
		if ((c->flags & SITF_TopLevel) || ! c->visible) continue;
		if (c->children.lh_Head == NULL || (c->flags & SITF_RenderChildren) == 0)
		{
			memcpy(pad, c->padding, sizeof pad);
			size.width  = c->layout.pos.width;
			size.height = c->layout.pos.height;
			c->layout.pos.left   = c->box.left + pad[0];
			c->layout.pos.top    = c->box.top  + pad[1];
			c->layout.pos.width  = c->box.right  - pad[2] - c->layout.pos.left;
			c->layout.pos.height = c->box.bottom - pad[3] - c->layout.pos.top;
			c->offsetX = root->offsetX + root->box.left;
			c->offsetY = root->offsetY + root->box.top;
			c->style.flags &= ~CSSF_BORDERIMG;
			layoutAdjustBorderRadius(c);
			layoutAlignText(c);
			if (HAS_EVT(c, SITE_OnResize) && ! (ALMOST0(size.width-c->layout.pos.width) && ALMOST0(size.height-c->layout.pos.height)))
				SIT_ApplyCallback(c, &c->layout.pos.width, SITE_OnResize);
		}
		else SIT_LayoutCSSSize(c);
	}
}

#if 0
void SIT_DebugCoord(SIT_Widget root)
{
	SIT_Widget c;
	int indent;
	REAL box[4];
	for (indent = 0, c = root->parent; c; indent += 2, c = c->parent);

	memcpy(box, &root->box, sizeof box);
	box[2] -= box[0];
	box[3] -= box[1];
	fprintf(stderr, "%*s%s: %g,%g - %gx%g\n", indent, "", root->name, box[0], box[1], box[2], box[3]);
	for (c = HEAD(root->children); c; NEXT(c))
		SIT_DebugCoord(c);
}
#endif

Bool SIT_LayoutWidgets(SIT_Widget root, ResizePolicy mode)
{
	if (mode != FitUsingOptimalBox)
	{
		REAL box[4];
		memcpy(box, &root->box, sizeof box);
		/* need to compute OptimalBox first */
		if (SIT_HasNoOptimalValue(root))
		{
			SIT_LayoutOptimal(root);
			if (mode == KeepDialogSize && box[2] > box[0] && box[3] > box[1])
				memcpy(&root->box, box, sizeof box);
		}
		SIT_LayoutInitial(root, mode);
		if (mode == FitUsingInitialBox && root->type == SIT_DIALOG)
		{
			root->box.left += box[0]; root->box.right  += box[0];
			root->box.top  += box[1]; root->box.bottom += box[1];
		}
		#ifdef DEBUG_GEOM
		fprintf(stderr, "layout initial %s: %gx%g", root->name, root->box.right-root->box.left, root->box.bottom-root->box.top);
		if (root->type == SIT_DIALOG)
			fprintf(stderr, " min: [%g, %g]\n", ((SIT_Dialog)root)->minSize.width, ((SIT_Dialog)root)->minSize.height);
		else
			fputc('\n', stderr);
		#endif
		#if 0
		if (root->parent == NULL)
			SIT_DebugCoord(root);
		#endif
	}
	else SIT_LayoutOptimal(root);

	root->flags &= ~SITF_StylesChanged;

	if (mode != FitUsingOptimalBox && (root->flags & SITF_TopLevel))
		SIT_LayoutCSSSize(root);

	return True;
}

void SIT_ReflowLayout(SIT_Widget list)
{
	while (list)
	{
		SIT_Widget parent = list->parent;
		SizeF      pref   = {-1, -1};
		REAL       pbox[4];
		REAL       dim[4];
		int        i, flag;

		if (parent == NULL)
		{
			SIT_LayoutWidgets(list, FitUsingCurrentBox);
			list = list->geomChanged;
			continue;
		}
		memcpy(dim, &list->box, sizeof dim);
		memcpy(pbox, &parent->box, sizeof pbox);
		if ((list->flags & SITF_GeomNotified) == 0)
		{
			/* already done */
			list = list->geomChanged;
			continue;
		}
		list->flags &= ~SITF_GeomNotified;
		/* check if an entire reflow is required */
		if (list->flags & SITF_AutoHeight)
		{
			if (list->attachment[0].sa_Type && list->attachment[2].sa_Type)
			{
				/* width constrained */
				SIT_LayoutWidget(parent, list, 0, FitUsingCurrentBox);
				pref.width = list->box.right - list->box.left;
				list->optimalWidth(list, &pref, 0);
				list->optimalBox = pref;
				SIT_LayoutWidget(parent, list, 1, FitUsingOptimalBox);
			}
			else /* not constrained */
			{
				list->optimalWidth(list, &pref, 0);
				list->optimalBox = pref;
				SIT_LayoutWidget(parent, list, 0, FitUsingOptimalBox);
				SIT_LayoutWidget(parent, list, 1, FitUsingOptimalBox);
			}
		}
		else
		{
			int done = 0;
			if (list->visible)
			{
				SizeF oldSz = list->currentBox;
				if (list->attachment[0].sa_Type == SITV_AttachNone || list->attachment[2].sa_Type == SITV_AttachNone)
				{
					done = 1;
					list->optimalWidth(list, &pref, 0);
					list->optimalBox = pref;
					SIT_LayoutWidget(parent, list, 0, FitUsingOptimalBox);
				}
				else
				{
					if (list->flags & SITF_FixedWidth) list->currentBox.width = list->fixed.width;
					SIT_LayoutWidget(parent, list, 0, FitUsingCurrentBox);
				}

				if (list->attachment[1].sa_Type == SITV_AttachNone || list->attachment[3].sa_Type == SITV_AttachNone)
				{
					if (! done) list->optimalWidth(list, &pref, 0), list->optimalBox = pref;
					SIT_LayoutWidget(parent, list, 1, FitUsingOptimalBox);
				}
				else
				{
					if (list->flags & SITF_FixedHeight) list->currentBox.width = list->fixed.height;
					SIT_LayoutWidget(parent, list, 1, FitUsingCurrentBox);
				}
				list->currentBox.width  = list->box.right  - list->box.left;
				list->currentBox.height = list->box.bottom - list->box.top;
				if (BoxSizeDiffers(&list->currentBox, &oldSz))
					SIT_LayoutCSSSize(list);

				#define pwidth 	parent->layout.pos.width
				#define pheight parent->layout.pos.height

				/* size exceeds container or used to be what's defined the container size: need reflow */
				if (SIT_CanChangeContainerSize(list, &oldSz, NULL, 0) ||
				    SIT_CanChangeContainerSize(list, &oldSz, NULL, 1))
				{
					memcpy(&parent->box, pbox, sizeof pbox);
					i = 0; goto reflow;
				}
			}
			else memset(&list->box, 0, sizeof list->box), list->flags &= ~SITF_GeomNotified;

			/* SIT_LayoutWidget will change the size: restore it */
			memcpy(&parent->box, pbox, sizeof pbox);
		}
		if (list->flags & SITF_FixedX)
		{
			list->box.right += list->fixed.left - list->box.left;
			list->box.left   = list->fixed.left;
		}
		if (list->flags & SITF_FixedY)
		{
			list->box.bottom += list->fixed.top - list->box.top;
			list->box.top     = list->fixed.top;
		}
		for (i = 0, flag = LAYF_HasLeftAttach; i < 4; i ++, flag <<= 1)
		{
			if ((list->layout.flags & flag) && ! ALMOST0(dim[i] - (&list->box.left)[i]))
			{
				/* need reflow */
				reflow:
				while ((parent->flags & SITF_TopLevel) == 0)
				{
					memset(&parent->currentBox, 0, sizeof parent->currentBox);
//					memset(&parent->childBox, 0, sizeof parent->childBox);
					parent = parent->parent;
				}
				SIT_LayoutWidgets(parent, FitUsingInitialBox);
				break;
			}
		}
		if (i == 4)
		{
			/* size changed, but only this widget is concerned */
			SIT_LayoutCSSSize(list);
		}
		list = list->geomChanged;
	}
}

void SIT_MeasureWidget(SIT_Widget w)
{
	SizeF pref = {0};
	w->optimalWidth(w, &pref, (APTR) FitUsingOptimalBox);
	w->box.left = w->box.top = 0;
	w->box.right = pref.width;
	w->box.bottom = pref.height;
	SIT_LayoutCSSSize(w);
}

/* widget is about to be removed, remove any reference of this control within attachments */
Bool SIT_ReassignAttachments(SIT_Widget w)
{
	SIT_Attach * a;
	SIT_Widget   c;
	int          i, ret;

	for (c = HEAD(w->parent->children), ret = False; c; NEXT(c))
	{
		if (c == w) continue;
		for (i = 0, a = c->attachment; i < 4; i ++, a ++)
		{
			if (relative[a->sa_Type] == 0 || a->sa_Arg != (ULONG) w) continue;
			SIT_Widget r = (APTR) a->sa_Arg;
			memcpy(a, r->attachment + i, sizeof *a);
			ret = True;
		}
	}
	return ret;
}
