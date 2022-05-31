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
		}
		if (sub == NULL) return 0;
	}
}

/* side [0-3]: left, top, right, bottom */
static Bool SIT_CanReduceContainerSize(SIT_Widget w, int side)
{
	for (;;)
	{
		SIT_Attach * a = w->attachment + side;
		switch (a->sa_Type) {
		case SITV_AttachForm:
		case SITV_AttachPosition:
			if (w->parent == NULL) return False;
			if ((w->flags & SITF_TopLevel) == 0)
			{
				w = w->parent;
				break;
			}
			// else no break;
		case SITV_AttachNone:
			return (w->flags & (side & 1 ? SITF_FixedHeight : SITF_FixedWidth)) == 0;
		default:
			return False;
		}
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
	int          i, j, inc, hasPos;
	REAL         padTL, padBR, pad, sz, margin;
	REAL         percent[2];
	REAL         chldsz = (adjust == FitUsingOptimalBox ? &w->optimalBox.width : &w->currentBox.width)[side];

	if (adjust == FitUsingInitialBox && (w->flags & (SITF_FixedWidth<<side)))
		chldsz = (&w->fixed.width)[side];

	inc = 2;
	i = side;
	/* AttachNone and AttachNoOverlap must be computed after any other kind */
	a = w->attachment + side;
	if (a->sa_Type == SITV_AttachNone ||
	    a->sa_Type == SITV_AttachNoOverlap)
	{
		if (a[2].sa_Type == SITV_AttachNone ||
		    a[2].sa_Type == SITV_AttachNoOverlap)
			;
		else
			inc = -2, i += 2;
	}

	/* margins */
	padTL = root->padding[side];
	padBR = root->padding[side+2];
	pad   = padTL+padBR;
	hasPos = 0;
	sz = (&root->box.right)[side] - (&root->box.left)[side];
	percent[0] = SIT_PercentMovable(w, side);
	percent[1] = SIT_PercentMovable(w, side+2);
	/* handle horizontal and vertical constraints */
	for (p = &w->box.left + i, s = NULL, j = 0; j < 2; j ++, i += inc, p += inc)
	{
		a = w->attachment + i;
		margin = a->sa_Offset;
		if (margin == SITV_NoPad)
			margin = - (&root->layout.padding.top)[(i+3)&3] + a->sa_Arg;

		if (relative[a->sa_Type] && (s = (SIT_Widget) a->sa_Arg) && ! s->visible)
		{
			a = SIT_ReassignAttachment(s, i);
			s = (SIT_Widget) a->sa_Arg;
			if (w->attachment[i].sa_Type == SITV_AttachOpposite)
				margin = a->sa_Offset;
		}

		switch (a->sa_Type) {
		case SITV_AttachForm: /* edge of container */
			root->layout.flags |= sideAttach[i];
			w->layout.flags |= sideAttach[i];
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
			w->layout.flags |= sideBoth[i&1];
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
			*p = floorf(((sz-pad) * (int) a->sa_Arg + 32768) * (1/65536.f)) + margin;
			break;
		case SITV_AttachWidget: /* relative to another widget */
			s->layout.flags |= sideOpp[i];
			/* check if sibling has a pending geometry reflow */
			if (s->flags & SITF_GeomNotified) return 0;
			if (i > 1) *p = (&s->box.left)[i - 2] - margin;
			else       *p = (&s->box.left)[i + 2] + margin;
			break;
		case SITV_AttachOpposite: /* relative to opposite border it used to */
			s = (SIT_Widget) a->sa_Arg;
			if (s->flags & SITF_GeomNotified) return 0;
			s->layout.flags |= sideAttach[i];
			*p = margin + (&s->box.left)[i];
			break;
		case SITV_AttachMiddle: /* relative to middle of another widget */
			s = (SIT_Widget) a->sa_Arg;
			if (s->flags & SITF_GeomNotified) return 0;
			s->layout.flags |= sideBoth[i&1];
			*p = roundf(((&s->box.left)[i&1] + (&s->box.left)[(i&1) + 2] - chldsz) / 2);
			break;
		case SITV_AttachNone: /* let it where it is */
		case SITV_AttachNoOverlap: /* mix between None and Widget */
			if (i > 1)  *p = p[-2] + chldsz;
			else if (j) *p = p[ 2] - chldsz;
			else        *p = padTL, p[2] += padTL;
			if (a->sa_Type != SITV_AttachNoOverlap) break;

			s = (SIT_Widget) a->sa_Arg;
			if (s->flags & SITF_GeomNotified) return 0;
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

	if (HAS_EVT(w, SITE_OnGeometrySet))
	{
		/* custom callback for setting geometry */
		int size[] = {p[0], p[2], side};
		size[1] -= size[0]; /* X/Y, W/H, side */
		if (SIT_ApplyCallback(w, size, SITE_OnGeometrySet))
		{
			if (size[2] < side)
			{
				w->flags |= SITF_NoResetSize;
				return -1;
			}
			if (size[1] != p[2] - p[0])
			{
				chldsz = size[1];
				(&w->box.left)[side+2] = (&w->box.left)[side] + size[1];
			}
			p[0] = size[0];
			p[2] = size[0] + size[1];
			w->flags |= SITF_NoResetSize;
			adjust = FitUsingOptimalBox;
		}
	}

	if (root->flags & (SITF_FixedWidth << side))
		return 1;

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
		else if (w->attachment[side+2].sa_Type == SITV_AttachNone)
		{
			p[2] += padTL - p[0], p[0] = padTL;
		}
		else
		{
			*cside += padTL - p[0];
			return 0;
		}
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
	if (hasPos == 0 && p[2] > margin) // && reduce)
		ret = 0, *cside += p[2] - margin;

	if (p[2] - p[0] < chldsz && percent[1] > percent[0])
	{
		/* try to keep dialog size if we can */
		if (adjust != FitUsingOptimalBox && p[2] - p[0] >= (&w->optimalBox.width)[side])
			return 1;

		int newsz = *cside + (p[0] + chldsz - p[2]) / (percent[1] - percent[0]);
		if (newsz > sz)
			newsz = sz;
		if (*cside != newsz)
			*cside = newsz, ret = 0;
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
	root->layout.flags &= ~LAYF_HasAttach;
	for (list = HEAD(root->children), total = count = 0; list; NEXT(list))
	{
		if ((list->flags & SITF_TopLevel) || ! list->visible) continue;
		total++;

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
	fprintf(stderr, "=== %s: %s (%dx%dx%dx%d): min: [%d, %d]", root->name, "Keep\0   Optimal\0Current\0Initial" + mode*8,
		(int) root->box.left, (int) root->box.top, (int) root->box.right, (int) root->box.bottom, (int) root->minBox.width, (int) root->minBox.height);
	#endif

	/* perform 2 steps: one for horizontal constraints, a second for vertical */
	root->layout.flags &= ~LAYF_HasAttach;
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
	}
	#ifdef DEBUG_GEOM
	fprintf(stderr, " box: %d, %d - %d, %d:\n", (int) root->box.left, (int) root->box.top, (int) root->box.right, (int) root->box.bottom);
	for (list = HEAD(root->children); list; NEXT(list))
	{
		if (list->visible && (list->flags & SITF_TopLevel) == 0)
			fprintf(stderr, "    %s: %dx%d - %dx%d (%dx%d)\n",
				list->name, (int) list->box.left, (int) list->box.top, (int) list->box.right,
				(int) list->box.bottom, (int) (list->box.right - list->box.left), (int) (list->box.bottom - list->box.top));
	}
	#endif

	root->childBox.width = root->box.right  - root->box.left;
	root->childBox.height = root->box.bottom - root->box.top;

	if (root->flags & SITF_Container)
	{
		/* XXX not good with negative margins */
		root->optimalBox.width  = min.width  + root->padding[2];
		root->optimalBox.height = min.height + root->padding[3];
	}
	return 1;
}

/* check if changing size of widget <w> will reflow other controls (side: [0-1]) */
static Bool SIT_CanReflow(SIT_Widget w, float oldSz, int side)
{
	SIT_Widget   c = w->parent;
	SIT_Attach * a = w->attachment + side;
	REAL       * p = &w->box.left + side;

	if (w->flags & SITF_TopLevel) return False;

	if (a->sa_Type == SITV_AttachNone && a[2].sa_Type != SITV_AttachNone && (&w->box.left)[side] <= c->padding[side])
		return True;

	if (a->sa_Type == SITV_AttachForm && a[2].sa_Type == SITV_AttachForm)
		return True;

	if (a->sa_Type == SITV_AttachPosition || a[2].sa_Type == SITV_AttachPosition)
	{
		REAL * pad = w->padding + side;
		if (oldSz >= p[2] - p[0] - pad[0] - pad[2])
			return True;
	}

	a += 2;
	p += 2;
	if (w->layout.flags & (LAYF_HasRightAttach << side))
		return True;

	if (a->sa_Type == SITV_AttachNone)
	{
		/* bottom/right set to none */
		REAL * csize = &c->box.left + side;
		REAL bottom = csize[2] - csize[0] - c->padding[side+2];
		if (p[0] >= bottom || p[-2] + oldSz >= bottom) return True;
	}
	return False;
}

static int BoxSizeDiffers(SIT_Widget w, SizeF * box)
{
	if (w->layout.flags & LAYF_HasRightAttach)
	{
		if (! (ALMOST0(w->currentBox.width - box->width)))
			return True;
	}
	else if (w->currentBox.width < box->width)
	{
		return True;
	}
	if (w->layout.flags & LAYF_HasBottomAttach)
	{
		return ! ALMOST0(w->currentBox.height - box->height);
	}
	else return w->currentBox.height < box->height;
}

/* sigh: SIT uses border-box as for dimension (in <box> field), CSS uses content-box in layout.pos */
void SIT_LayoutCSSSize(SIT_Widget root)
{
	/* in SIT_Geometry we only set <box> */
	SIT_Widget c;
	SizeF      size;

	size.width  = root->layout.pos.width;
	size.height = root->layout.pos.height;
	if (root->layout.format.width > 0)
		/* relative size for padding/border */
		layoutCalcPadding(root);
	root->layout.pos.left   = root->box.left + root->padding[0];
	root->layout.pos.top    = root->box.top  + root->padding[1];
	root->layout.pos.width  = root->box.right  - root->padding[2] - root->layout.pos.left;
	root->layout.pos.height = root->box.bottom - root->padding[3] - root->layout.pos.top;

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
			size.width  = c->layout.pos.width;
			size.height = c->layout.pos.height;
			if (c->layout.format.width > 0)
				layoutCalcPadding(c);
			c->layout.pos.left   = c->box.left + c->padding[0];
			c->layout.pos.top    = c->box.top  + c->padding[1];
			c->layout.pos.width  = c->box.right  - c->padding[2] - c->layout.pos.left;
			c->layout.pos.height = c->box.bottom - c->padding[3] - c->layout.pos.top;
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

int SIT_AdjustContainer(SIT_Widget list)
{
	int reflow = 0;
	/* check if we can reduce size of container */
	if (list->box.right - list->box.left > list->optimalBox.width && (list->flags & SITF_FixedWidth) == 0 &&
		list->optimalBox.width > list->minBox.width && (SIT_CanReduceContainerSize(list, 0) || SIT_CanReduceContainerSize(list, 2)))
	{
		list->box.right = list->box.left + list->optimalBox.width;
		list->currentBox.width = list->childBox.width = list->optimalBox.width;
		reflow = 1;
	}
	if (list->box.bottom - list->box.top > list->optimalBox.height && (list->flags & SITF_FixedHeight) == 0 &&
		list->optimalBox.height > list->minBox.height && (SIT_CanReduceContainerSize(list, 1) || SIT_CanReduceContainerSize(list, 3)))
	{
		list->box.bottom = list->box.top + list->optimalBox.height;
		list->currentBox.height = list->childBox.height = list->optimalBox.height;
		reflow = 1;
	}
	return reflow;
}

Bool SIT_LayoutWidgets(SIT_Widget root, ResizePolicy mode)
{
	SIT_Widget list;
	int        count;

	root->flags &= ~(SITF_GeomNotified | SITF_GeometryChanged);
	if (mode != KeepDialogSize)
	{
		if (root->flags & SITF_FixedWidth)  root->box.right  = root->box.left + root->fixed.width;
		if (root->flags & SITF_FixedHeight) root->box.bottom = root->box.top  + root->fixed.height;
	}

	/* setup initial box if needed */
	for (list = HEAD(root->children); list; NEXT(list))
	{
		if ((list->flags & SITF_TopLevel) || ! list->visible) continue;

		if (list->optimalBox.width < 0)
		{
			if (list->optimalWidth)
				list->optimalWidth(list, &list->optimalBox, (APTR) FitUsingOptimalBox);
			if (list->minBox.width > 0 && list->optimalBox.width < list->minBox.width)
				list->optimalBox.width = list->minBox.width;
			if (list->minBox.height > 0 && list->optimalBox.height < list->minBox.height)
				list->optimalBox.height = list->minBox.height;
			if (list->optimalBox.width  < 0) list->optimalBox.width  = 0;
			if (list->optimalBox.height < 0) list->optimalBox.height = 0;
			list->currentBox = list->optimalBox;
			if ((list->flags & SITF_FixedWidth) && list->currentBox.width < list->fixed.width)
				list->currentBox.width = list->fixed.width;
			if ((list->flags & SITF_FixedHeight) /*&& list->currentBox.height < list->fixed.height*/)
				list->currentBox.height = list->fixed.height;
		}
		memset(&list->box, 0, sizeof list->box);
		if (list->flags & SITF_GeomNotified)
		{
			/* in sit.geomList, need to be removed */
			SIT_Widget * prev;
			SIT_Widget   scan;
			for (prev = &sit.geomList, scan = *prev; scan && scan != list; prev = &scan->geomChanged, scan = *prev);
			if (scan) *prev = scan->geomChanged;
			list->flags &= ~SITF_GeomNotified;
		}
	}

	for (count = 0; count < 10; count ++)
	{
		int reflow = 0;
		SIT_LayoutChildren(root, FitUsingCurrentBox);

		/* also check if children's children need reflow */
		for (list = HEAD(root->children), count = 0; list; NEXT(list))
		{
			if (! list->visible || (list->flags & SITF_TopLevel)) continue;
			list->currentBox = SIT_GetContentBox(list);

			if ((list->children.lh_Head && !(list->flags & SITF_PrivateChildren)) &&
			    BoxSizeDiffers(list, &list->childBox))
			{
				#ifdef DEBUG_GEOM
				fprintf(stderr, "%s: current box: %dx%d, child box: %dx%d\n", list->name, (int) list->currentBox.width,
					(int) list->currentBox.height, (int) list->childBox.width, (int) list->childBox.height);
				#endif
				SizeF old = list->currentBox;
				SIT_LayoutWidgets(list, KeepDialogSize);
				list->childBox = list->currentBox = SIT_GetContentBox(list);

				if ((list->flags & SITF_Container))
				{
					/* check if we can reduce the size of container */
					reflow |= SIT_AdjustContainer(list);
				}
				if ((old.width  != list->currentBox.width  && (root->flags & SITF_FixedWidth) == 0) ||
				    (old.height != list->currentBox.height && (root->flags & SITF_FixedHeight) == 0))
				{
					reflow |= 1;
				}
			}
		}
		if (reflow)
		{
			float minWidth = root->minBox.width;
			float minHeight = root->minBox.height;
			if (root->flags & SITF_FixedWidth)  minWidth  = root->fixed.width;
			if (root->flags & SITF_FixedHeight) minHeight = root->fixed.height;
			if (minWidth  < 0) minWidth  = 0;
			if (minHeight < 0) minHeight = 0;
			root->box.right  = root->box.left + minWidth;
			root->box.bottom = root->box.top  + minHeight;
		}
		else break;
	}
	if (root->flags & SITF_TopLevel)
		SIT_AdjustContainer(root);
	root->flags &= ~ (SITF_GeometrySet | SITF_StylesChanged);
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

		if (list->flags & SITF_FixedWidth)  pref.width  = list->fixed.width;
		if (list->flags & SITF_FixedHeight) pref.height = list->fixed.height;

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
				list->optimalBox = list->currentBox = pref;
				SIT_LayoutWidget(parent, list, 1, FitUsingOptimalBox);
			}
			else /* not constrained */
			{
				list->optimalWidth(list, &pref, 0);
				list->optimalBox = list->currentBox = pref;
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
				if (list->optimalWidth == NULL)
				{
					parent = list;
					i = 0;
					goto reflow;
				}
				if (list->attachment[0].sa_Type == SITV_AttachNone || list->attachment[2].sa_Type == SITV_AttachNone)
				{
					done = 1;
					list->optimalWidth(list, &pref, 0);
					list->optimalBox = pref;
					if (! SIT_LayoutWidget(parent, list, 0, FitUsingOptimalBox))
						goto reflow;
				}
				else if (! SIT_CanReflow(list, oldSz.width, 0))
				{
					if (list->flags & SITF_FixedWidth) list->currentBox.width = list->fixed.width;
					if (! SIT_LayoutWidget(parent, list, 0, FitUsingCurrentBox))
						goto reflow;
				}
				else goto reflow;

				if (list->attachment[1].sa_Type == SITV_AttachNone || list->attachment[3].sa_Type == SITV_AttachNone)
				{
					if (! done) list->optimalWidth(list, &pref, 0), list->optimalBox = pref;
					if (! SIT_LayoutWidget(parent, list, 1, FitUsingOptimalBox))
						goto reflow;
				}
				else if (! SIT_CanReflow(list, oldSz.height, 1))
				{
					if (list->flags & SITF_FixedHeight) list->currentBox.width = list->fixed.height;
					if (! SIT_LayoutWidget(parent, list, 1, FitUsingCurrentBox))
						goto reflow;
				}
				else goto reflow;

				list->currentBox.width  = list->box.right  - list->box.left;
				list->currentBox.height = list->box.bottom - list->box.top;
				if (BoxSizeDiffers(list, &oldSz))
					SIT_LayoutCSSSize(list);
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
				list->currentBox = list->optimalBox;
				i = 0;
				while ((parent->flags & SITF_TopLevel) == 0)
				{
					//memset(&parent->currentBox, 0, sizeof parent->currentBox);
					memset(&parent->childBox, 0, sizeof parent->childBox);
					parent = parent->parent;
				}
				SIT_LayoutWidgets(parent, KeepDialogSize);
				SIT_GeomRemoveChildrenOf(&list, parent);
				if (list == NULL) return;
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
	SIT_ProcessMouseMove(sit.mouseX, sit.mouseY);
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
