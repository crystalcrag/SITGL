/*
 * SIT_Render.c : now that layout/geometry has done its job (getting coordinates of every nodes),
 *                split rendering using simple drawing operations.
 *
 * written by T.Pierron, mar 2010.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "SIT_CSSParser.h"
#include "nanovg.h"

/* optimize if border is fully opaque */
static Bool renderIsFullyOpaqueBorder(Border * borders, int side)
{
	static uint8_t sides[] = {
		CSS_TOP,    CSS_LEFT, CSS_RIGHT,
		CSS_RIGHT,  CSS_TOP,  CSS_BOTTOM,
		CSS_BOTTOM, CSS_LEFT, CSS_RIGHT,
		CSS_LEFT,   CSS_TOP,  CSS_BOTTOM
	};
	static uint8_t isBorderOpaque[] = {
		1 /* none */, 0 /* hidden */, 0 /* dotted */, 0 /* dashed */, 1, 1, 1, 1, 1, 1
	};
	int i;

	for (i = 0, side *= 3; i < 3; side ++, i ++)
	{
		Border * b = borders + sides[side];
		if (! isBorderOpaque[b->style] || b->color.rgba[3] < 255) return False;
	}
	return True;
}

static void renderRect(RectF * rect)
{
	nvgPathWinding(sit.nvgCtx, NVG_CCW);
	nvgBeginPath(sit.nvgCtx);
	nvgRect(sit.nvgCtx, rect->left, rect->top, rect->width, rect->height);
}

/* generate a path of a round rect with potentially different round corner radius */
static void renderRoundRect(REAL * rectF, BoxF * major, BoxF * minor, int sides)
{
	REAL rect[4];

	rect[0] = rectF[0];
	rect[2] = rectF[2] + rectF[0];
	rect[1] = rectF[1];
	rect[3] = rectF[3] + rectF[1];

	/* rect is drawn in CSS side order (starting with top edge) */
	if (sides & 1)
		nvgMoveTo(sit.nvgCtx, rect[0] + major->top,   rect[1]),
		nvgLineTo(sit.nvgCtx, rect[2] - major->right, rect[1]);
	else
		nvgMoveTo(sit.nvgCtx, rect[2] - major->right, rect[1]);

	/* top right corner */
	if (major->right > 0)
	{
		if (sides & 3)
			nvgEllipseArc(sit.nvgCtx, rect[2] - major->right, rect[1] + minor->right, major->right, minor->right, -M_PI_2, 0, NVG_CW);
		else
			nvgMoveTo(sit.nvgCtx, rect[2] - major->right, rect[1] + minor->right);
	}
	if (sides & 2)
		nvgLineTo(sit.nvgCtx, rect[2], rect[3] - minor->bottom);
	else
		nvgMoveTo(sit.nvgCtx, rect[2], rect[3] - minor->bottom);

	/* bottom right corner */
	if (major->bottom > 0)
	{
		if (sides & 6)
			nvgEllipseArc(sit.nvgCtx, rect[2] - major->bottom, rect[3] - minor->bottom, major->bottom, minor->bottom, 0, M_PI_2, NVG_CW);
		else
			nvgMoveTo(sit.nvgCtx, rect[2] - major->bottom, rect[3]);
	}
	if (sides & 4)
		nvgLineTo(sit.nvgCtx, rect[0] + major->left, rect[3]);
	else
		nvgMoveTo(sit.nvgCtx, rect[0] + major->left, rect[3]);

	/* bottom left */
	if (major->left > 0)
	{
		if (sides & 12)
			nvgEllipseArc(sit.nvgCtx, rect[0] + major->left, rect[3] - minor->left, major->left, minor->left, M_PI_2, M_PI, NVG_CW);
		else
			nvgMoveTo(sit.nvgCtx, rect[0], rect[3] - minor->left);
	}
	if (sides & 8)
		nvgLineTo(sit.nvgCtx, rect[0], rect[1] + minor->top);
	else
		nvgMoveTo(sit.nvgCtx, rect[0], rect[1] + minor->top);

	/* top left */
	if (major->top > 0 && (sides & 9))
		nvgEllipseArc(sit.nvgCtx, rect[0] + major->top, rect[1] + minor->top, major->top, minor->top, M_PI, -M_PI_2, NVG_CW);
}

void renderInitBox(SIT_Widget node, RectF * out, Bool init, int flag)
{
	/* flag: bit 1 = top, 2 = right, 4 = bottom, 8 = left */
	if (init)
	{
		out->left   = node->box.left + node->padding[0];
		out->top    = node->box.top  + node->padding[1];
		out->width  = node->box.right  - node->padding[2] - out->left;
		out->height = node->box.bottom - node->padding[3] - out->top;
		out->left  += node->offsetX + node->layout.left;
		out->top   += node->offsetY + node->layout.top;
	}
	if (flag & 1) out->top    -= node->layout.padding.top,  out->height += node->layout.padding.top;
	if (flag & 8) out->left   -= node->layout.padding.left, out->width  += node->layout.padding.left;
	if (flag & 2) out->width  += node->layout.padding.right;
	if (flag & 4) out->height += node->layout.padding.bottom;
}

static REAL renderToPoints(RectF * box, ULONG fixed, int side, REAL ratio)
{
	uint16_t num  = fixed >> 16;
	int      frac = (fixed & 0xfffc) >> 2;
	REAL     res  = (num&0x7fff) + frac / 16384.;

	if (num & 0x8000) res = -res;

	switch (fixed & 3) {
	case 0: return res; /* direct value (pt, px) */
	case 1: return res * ratio; /* em or ex */
	case 2: return (&box->width)[side] * res / 100.0f;
	}
	return 0;
}


/* parse and apply transform matrixes */
static Bool renderParseTransform(SIT_Widget node, STRPTR transform, float ret[6])
{
	STRPTR  p, n;
	REAL    matrix[6], r;
	RectF   rect;
	int     mflags;

	/* transform: it is applied to the border box */
	renderInitBox(node, &rect, True, 15);
	rect.left -= node->layout.border.left;
	rect.top  -= node->layout.border.top;
	rect.width += node->layout.border.left + node->layout.border.right;
	rect.height += node->layout.border.top + node->layout.border.bottom;
	r = node->style.font.size;

	nvgTransformIdentity(ret);

	/* parse transformation functions: do not modify css value (space separated list of transformation) */
	for (p = n = transform, mflags = 1; p; p = n)
	{
		static uint16_t params[] = {0x555,10,2,2,5,1,1,3,15,3,3};
		while (*n && *n != '(') n ++;
		int func = FindInList("matrix,translate,translateX,translateY,scale,scaleX,scaleY,rotate,skew,skewX,skewY", p, n-p);
		if (func >= 0 && *n == '(')
		{
			int param = params[func], i;

			memset(matrix, 0, sizeof matrix);
			/* parse */
			for (n ++, i = 0; param > 0; param >>= 2, i ++)
			{
				uint16_t a;
				ULONG    v;
				switch (param&3) {
				case 1: matrix[i] = strtod(n, &n); break;
				case 2: if (! cssParsePos(&n, &v, 1)) return False;
				        matrix[i] = renderToPoints(&rect, v, i&1, r); break;
				case 3: if (! cssParseAngle(n, &n, &a)) return False;
						matrix[i] = - a * (2*M_PI) / 65536.;
				}
				n = skipspace(n);
				if (*n == ',') n = skipspace(n+1); else param = 0;
			}
			/* build matrix */
			if (i == 0) return False;
			param = 2;
			switch (func) {
			case  0: param = 0; break; // direct matrix
			case  1: matrix[4] = matrix[0]; matrix[5] = matrix[1]; // tranlate
			         matrix[1] = 0; param = 1; break;
			case  2: matrix[4] = matrix[0]; param = 1; break; // translatex
			case  3: matrix[5] = matrix[1]; param = 1; break; // translatey
			case  4: matrix[3] = matrix[1]; matrix[1] = 0; break; // scale
			case  5: matrix[3] = 1; break; // scalex
			case  6: matrix[3] = matrix[0]; matrix[0] = 1; break; // scaley
			case  7: matrix[1] = sinf(matrix[0]); matrix[2] = -matrix[1];
			         matrix[0] = matrix[3] = cosf(matrix[0]); break; // rotate
			case  8: matrix[2] = tanf(matrix[0]); matrix[3] = 1;
			         matrix[1] = tanf(matrix[1]); matrix[0] = 1; break; // skew
			case  9: matrix[2] = tanf(matrix[0]); param = 3; break; // skewx
			case 10: matrix[1] = tanf(matrix[0]); param = 3; break; // skewy
			}
			/* apply matrix */
			if (param & 1)
				matrix[0] = matrix[3] = 1;
			if (param & 2)
			{
				/* add center of transformation - only for the one that requires it (all but translation) */
//				if (origin.x > 0 || origin.y > 0) mflags |= 2;
				matrix[4] = (rect.left);
				matrix[5] = (rect.top);
				if (mflags & 1)
					memcpy(ret, matrix, sizeof matrix);
				else
					nvgTransformPremultiply(ret, matrix);
				matrix[0] = matrix[3] = 1;
				matrix[1] = matrix[2] = 0;
				matrix[4] = -matrix[4]; //origin.x = 0;
				matrix[5] = -matrix[5]; //origin.y = 0;
				nvgTransformPremultiply(ret, matrix);
			}
			else if (mflags & 1)
				memcpy(ret, matrix, sizeof matrix);
			else
				nvgTransformMultiply(ret, matrix);
			mflags = 0;
		}
		else return False;
		while (*n && *n != ')') n ++;
		if (*n == ')') n = skipspace(n+1);
		if (*n == 0) break;
	}
	return True;
}

/* compute "some" values for border-image rendering */
static void renderCacheBorderImg(SIT_Widget node, RectF * box)
{
	BImgCache cache;
	BorderImg bimg  = node->style.borderImg;
	CSSImage  img   = bimg->image;
	Box       slice = bimg->slice;
	RectF     rect  = *box;
	int       i;

	rect.left = 0;
	rect.top  = 0;
	if ((node->layout.flags & LAYF_BImgLoaded) == 0 && ! img)
	{
		img = bimg->image = cssAddImage(bimg->source, False);
		node->layout.flags |= LAYF_BImgLoaded;
		if (img)
		{
			if (slice.left   > img->width)  slice.left   = img->width;
			if (slice.right  > img->width)  slice.right  = img->width;
			if (slice.top    > img->height) slice.top    = img->height;
			if (slice.bottom > img->height) slice.bottom = img->height;
			bimg->slice = slice;
		}
	}
	/* even if it fails, don't do this until something has changed */
	node->style.flags |= CSSF_BORDERIMG;
	if (! img) return;

	cache.bdsz  = node->layout.border;
	rect.width  += bimg->offFloat.right  + rect.left + cache.bdsz.right;
	rect.height += bimg->offFloat.bottom + rect.top  + cache.bdsz.bottom;
	rect.left   -= bimg->offFloat.left + cache.bdsz.left;
	rect.top    -= bimg->offFloat.top  + cache.bdsz.top;

	/* cache some values */
	REAL max[2];
	max[0] = (rect.width - rect.left) * 0.5f;
	max[1] = (rect.height - rect.top) * 0.5f;
	if (max[0] > max[1]) max[0] = max[1];

	cache.width = bimg->widthFloat;
	/* cache for corners */
	for (i = 0; i < 4; i ++)
	{
		REAL * width = &cache.width.top + i;
		REAL * bdsz  = &cache.bdsz.top + i;

		if (*width == 0) *width = *bdsz;
		if (*width > max[0]) *width = max[0];
		*bdsz = *width / (&slice.top)[i];
	}

	/* repeat sections: T, R, B, L */
	max[0] = img->width - slice.left - slice.right;
	max[1] = img->height - slice.top - slice.bottom;
	cache.pos.left   = rect.left   + cache.width.left;
	cache.pos.width  = rect.width  - cache.width.right - cache.pos.left;
	cache.pos.top    = rect.top    + cache.width.top;
	cache.pos.height = rect.height - cache.width.bottom - cache.pos.top;
	/* 4 borders can have a different width, therefore different count */
	for (i = 0; i < 4; i ++)
	{
		uint8_t WorH = i & 1;
		short   count;
		REAL    range, pos1, pos2;

		range = (&cache.range.top)[i] = max[WorH] * (&cache.bdsz.top)[i];
		pos1  = (&cache.pos.left)[WorH];
		pos2  = (&cache.pos.width)[WorH];

		/* damn, is this tedious */
		if (range == 0)
			cache.count[i] = 0;
		else switch ((&bimg->repeatH)[WorH]) {
		case BorderImageStretch:
			cache.count[i] = 1;
			continue;
		case BorderImageRepeat:
			cache.count[i] = count = ceil(pos2 / range);
			cache.start[i] = pos1 + (pos2 - count * range) * 0.5f;
			cache.space[i] = 0;
			break;
		case BorderImageRound:
			cache.count[i] = count = ceil(pos2 / range);
			cache.start[i] = pos1;
			cache.space[i] = 0;
			(&cache.range.top)[i] = pos2 / count;
			break;
		case BorderImageSpace:
			cache.count[i] = count = floor(pos2 / range);
			cache.space[i] = (pos2 - count * range) / (count + 1);
			cache.start[i] = pos1 + (pos2 - count * (range + cache.space[i]) + cache.space[i]) * 0.5f;
		}
	}
	bimg->cache = cache;
}

/* render background-image (not to be confused with img.src) */
static void renderBackground(SIT_Widget node, RectF * alt, int sides)
{
	Background bg = node->style.background;
	NVGCTX     vg = sit.nvgCtx;
	BoxF       border = node->layout.border;
	RectF      rect;
	int        count;

	if (node->parent == NULL)
	{
		/* root node: background covers whole page */
		rect.left = rect.top = 0;
		rect.width = node->fixed.width;
		rect.height = node->fixed.height;
	}
	else memcpy(&rect, alt ? alt : &node->layout.pos, sizeof rect);
	if ((sides & 1) == 0) border.top = 0;
	if ((sides & 2) == 0) border.right = 0;
	if ((sides & 4) == 0) border.bottom = 0;
	if ((sides & 8) == 0) border.left = 0;

	if (node->style.borderImg)
	{
		if ((node->style.flags & CSSF_BORDERIMG) == 0)
			renderCacheBorderImg(node, &rect);
		if (node->style.borderImg->fill == 2)
		{
			/*
			 * extension "content-fill": only draw bg up to border-image edge
			 * i.e: including padding, but excluding border.
			 * without this it is impossible to use "background:" properties and border-image
			 * with transparent borders (because background will cover the entire content-box).
			 */
			REAL * width = &node->style.borderImg->cache.width.top;
			for (count = 0; count < 4; count ++)
				(&border.top)[count] = (&border.top)[count] - width[count];
		}
	}
	else if (node->layout.flags & LAYF_NoRoundBorder)
	{
		if (renderIsFullyOpaqueBorder(&node->style.borderTop, CSS_LEFT))   border.left   = 0;
		if (renderIsFullyOpaqueBorder(&node->style.borderTop, CSS_RIGHT))  border.right  = 0;
		if (renderIsFullyOpaqueBorder(&node->style.borderTop, CSS_TOP))    border.top    = 0;
		if (renderIsFullyOpaqueBorder(&node->style.borderTop, CSS_BOTTOM)) border.bottom = 0;
	}

	/* background(-color|-image) must include the border area (in case border-color is transparent) */
	rect.left   -= border.left;
	rect.top    -= border.top;
	rect.width  += border.left + border.right;
	rect.height += border.top  + border.bottom;

	if ((node->layout.flags & LAYF_NoRoundBorder) == 0)
	{
		REAL box[4] = {rect.left, rect.top, rect.width, rect.height};
		BoxF major = node->layout.majorRadius;
		BoxF minor = node->layout.minorRadius;
		/* inlines can cut the border on left and right side, page breaks on top or bottom side */
		if ((sides & 9) != 9) major.top = minor.top = 0;
		if ((sides & 3) != 3) major.right = minor.right = 0;
		if ((sides & 6) != 6) major.bottom = minor.bottom = 0;
		if ((sides & 12) != 12) major.left = minor.left = 0;
		nvgPathWinding(vg, NVG_CCW);
		nvgBeginPath(vg);
		renderRoundRect(box, &major, &minor, 0xff);
	}
	else renderRect(&rect);

	/* multiple background declarations - ordered bottom to top */
	for (count = node->style.bgCount; count > 0; count --, bg ++)
	{
		CSSImage img = bg->image;

		if ((bg->bgw > 0 || bg->bgh > 0) && img)
		{
			if (bg->bgw == 7 || bg->bgw == 11) /* cover || contain */
			{
				Bool coverFromHeight = rect.width * img->height < rect.height * img->width;
				if (bg->bgw == 11) coverFromHeight = ! coverFromHeight;
				if (coverFromHeight)
					bg->dim.width = rect.height * img->width / img->height, bg->dim.height = rect.height;
				else
					bg->dim.height = rect.width * img->height / img->width, bg->dim.width = rect.width;
			}
			else /* values */
			{
				bg->dim.width  = ToPoints(node, node, bg->bgw, CSS_LEFT | CSS_ADDPAD);
				bg->dim.height = ToPoints(node, node, bg->bgh, CSS_TOP  | CSS_ADDPAD);
				if (bg->dim.width == 0)
					bg->dim.width = img->width * bg->dim.height / img->height;
				else if (bg->dim.height == 0)
					bg->dim.height = img->height * bg->dim.width / img->width;
			}
		}

		if (bg->color.val != 0 && rect.height > 0 && rect.width > 0)
		{
			nvgFillColorRGBA8(vg, bg->color.rgba);
			nvgFill(vg);
		}

		sides = (int) rect.width | ((int) rect.height << 16);
		if (bg->gradient.colorStop == 2 && bg->gradient.corner == 255)
		{
			/* radial-gradient with 2 color stops: use nanovg to render them directly; save quite a bit of memory */
			int   info[4];
			DATA8 col1  = bg->gradient.colors[0].rgba;
			DATA8 col2  = bg->gradient.colors[1].rgba;
			gradientGetCenter(&bg->gradient, info, rect.width, rect.height, node->style.font.size);

			REAL scale = info[2] / (float) info[3];
			nvgSave(vg);
			nvgTranslate(vg, rect.left, rect.top);
			nvgScale(vg, 1, 1 / scale);
			nvgFillPaint(vg, nvgRadialGradient(vg, info[0], info[1] * scale, 0, info[2] * 1.1,
				nvgRGBA(col1[0], col1[1], col1[2], col1[3]), nvgRGBA(col2[0], col2[1], col2[2], col2[3])));
			nvgFill(vg);
			nvgRestore(vg);
		}
		else if (bg->gradient.colorStop >= 2 && (! img || bg->gradient.wxh != sides))
		{
			/* rasterize the gradient and keep it into a cache */
			RectF sub = rect;
			if (bg->bgw > 0 && (bg->bgw&3) < 3)
				sub.width = bg->dim.width = ToPoints(node, node, bg->bgw, CSS_LEFT | CSS_ADDPAD);
			else if (node->parent == NULL)
				bg->dim.width = sub.width; /* cover whole page */
			else /* stretch to fill content */
				bg->dim.width = sub.width = ToPoints(node, node, cssFromUnit(Percentage, 100), CSS_LEFT | CSS_ADDPAD);

			if (bg->bgh > 0 && (bg->bgh&3) < 3)
				sub.height = bg->dim.height = ToPoints(node, node, bg->bgh, CSS_TOP | CSS_ADDPAD);
			else if (node->parent == NULL)
				bg->dim.height = sub.height;
			else
				bg->dim.height = sub.height = node->layout.pos.height + node->layout.padding.top + node->layout.padding.bottom;
			/* background-size: too small */
			if (bg->dim.width  < EPSILON) bg->dim.width  = 1;
			if (bg->dim.height < EPSILON) bg->dim.height = 1;
			bg->gradient.wxh = sides;
			if (img)
				SIT_UnloadImg(img);
			img = bg->image = cssAddGradient(&bg->gradient, sub.width, sub.height, node->style.font.size);
		}

		if (img && img->handle)
		{
			REAL x = bg->dim.left + border.left, w = bg->dim.width;
			REAL y = bg->dim.top  + border.top,  h = bg->dim.height;
			if (w == 0) w = img->width;
			if (h == 0) h = img->height;

			if (bg->attachment == BackgroundAttachmentFixed)
				x -= rect.left, y -= rect.top;

			if ((bg->x & 3) == 2) /* % unit */
				x = (rect.width - w - border.left - border.right) * ToPoints(NULL, node, bg->x & ~3, 0) * 0.01f + border.left;

			if ((bg->y & 3) == 2)
				y = (rect.height - h - border.top - border.bottom) * ToPoints(NULL, node, bg->y & ~3, 0) * 0.01f + border.top;

			x += rect.left;
			y += rect.top;
			/* will prevent image bleeding when set to no-repeat */
			x  = roundf(x);
			y  = roundf(y);
			w  = roundf(w);
			h  = roundf(h);
			switch (bg->repeat) {
			case 3: /* no-repeat */
				nvgSave(vg);
				nvgIntersectScissor(vg, x, y, w, h);
				break;
			case 2: /* repeat-y only */
				nvgSave(vg);
				nvgIntersectScissor(vg, x, y, w, sit.scrHeight);
				break;
			case 1: /* repeat-x only */
				nvgSave(vg);
				nvgIntersectScissor(vg, x, y, sit.scrWidth, h);
			}
			if (img->stretch) /* gradient */
			{
				/* corner linear-gradient: need to do transformation on our own */
				nvgTranslate(vg, x+img->rect[0], y+img->rect[1]);
				if (img->stretch == 1) nvgScale(vg, img->rect[3], 1);
				else                   nvgScale(vg, 1, img->rect[3]);
				nvgRotate(vg, img->angle);
				/* they are square, but need to be stretched to fill content */
				nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, img->rect[2], img->rect[2], 0, img->handle, 1));
				nvgResetTransform(vg);
			}
			else if (img->angle != 0)
			{
				/* linear-gradient */
				nvgFillPaint(vg, nvgImagePattern(vg, x+img->rect[0], y+img->rect[1], img->rect[2], img->rect[3], img->angle, img->handle, 1));
			}
			else /* regular bitmap */
			{
				if (bg->transform)
				{
					float m[6];
					if (renderParseTransform(node, bg->transform, m))
						nvgTransform(vg, m[0], m[1], m[2], m[3], x, y), x = 0, y = 0;
				}
				NVGpaint paint = nvgImagePattern(vg, x, y, w, h, img->angle, img->handle, 1);
				if (img->bpp == 8)
				{
					/* modulate bitmap with font color */
					DATA8 col = bg->gradient.colors[0].rgba[3] == 0 ? node->style.color.rgba : bg->gradient.colors[0].rgba /* -bg-mod-color */;
					paint.innerColor = nvgRGBA(col[0], col[1], col[2], col[3]);
				}
				nvgFillPaint(vg, paint);
				nvgResetTransform(vg);
			}
			nvgFill(vg);
			if (bg->repeat)
				nvgRestore(vg);
		}
	}
}

static void renderBoxShadow(SIT_Widget node, RectF * box, Bool inset)
{
	BoxShadow shadow;
	BoxF border = node->layout.border;
	BoxF major  = node->layout.majorRadius;
	BoxF minor  = node->layout.minorRadius;
	float rect[4];
	int i;

	nvgPathWinding(sit.nvgCtx, NVG_CCW);
	nvgBeginPath(sit.nvgCtx);
	rect[0] = box->left; rect[2] = box->width;
	rect[1] = box->top;  rect[3] = box->height;
	if (! inset)
	{
		nvgRect(sit.nvgCtx, 0, 0, sit.scrWidth, sit.scrHeight);
		nvgPathWinding(sit.nvgCtx, NVG_CW);
		/* XXX nanovg render shadow 1 pixel inward more than it should :-/ */
		rect[0] -= border.left+1; rect[2] += border.right+border.left+2;
		rect[1] -= border.top+1;  rect[3] += border.bottom+border.top+2;
	}
	else
	{
		/* border-radius is radius of outer border, we need to clip inner border */
		major.top    -= border.left;  if (major.top    < 0) major.top    = 0;
		major.right  -= border.right; if (major.right  < 0) major.right  = 0;
		major.bottom -= border.right; if (major.bottom < 0) major.bottom = 0;
		major.left   -= border.left;  if (major.left   < 0) major.left   = 0;

		minor.top    -= border.top;    if (minor.top    < 0) minor.top    = 0;
		minor.right  -= border.top;    if (minor.right  < 0) minor.right  = 0;
		minor.bottom -= border.bottom; if (minor.bottom < 0) minor.bottom = 0;
		minor.left   -= border.bottom; if (minor.left   < 0) minor.left   = 0;
	}

	/* clip background area */
	renderRoundRect(rect, &major, &minor, 0xff);
	nvgPathWinding(sit.nvgCtx, NVG_CCW);

	/* shadows are ordered top to bottom in CSS (like multiple background) */
	for (i = node->style.boxShadowCount, shadow = node->style.boxShadow; i > 0; i --, shadow ++)
	{
		if (shadow->inset != inset) continue;
		float bbox[6];
		float spreadX = shadow->XYSfloat[2];
		float spreadY = spreadX;
		memcpy(bbox, rect, sizeof rect);
		bbox[0] += shadow->XYSfloat[0] - spreadX;   bbox[2] += spreadX*2;
		bbox[1] += shadow->XYSfloat[1] - spreadY;   bbox[3] += spreadY*2;
		/* radius */
		bbox[4] = node->layout.majorRadius.top; spreadX = shadow->XYSfloat[2] + shadow->blurFloat * (0.5f+inset);
		if (shadow->blurFloat > 0 && bbox[4] < spreadX)
			bbox[4] = spreadX;
		/* feather: yeah, way bigger than what's asked :-/ */
		bbox[5] = shadow->blurFloat * 3;

		uint8_t col[4];
		memcpy(col, shadow->color.rgba, 4);
		if (inset)
			nvgFillPaint(sit.nvgCtx, nvgBoxGradient(sit.nvgCtx, bbox[0], bbox[1],
				bbox[2], bbox[3], bbox[4], bbox[5], nvgRGBA(0,0,0,0), nvgRGBA(col[0], col[1], col[2], col[3])));
		else
			nvgFillPaint(sit.nvgCtx, nvgBoxGradient(sit.nvgCtx, bbox[0], bbox[1],
				bbox[2], bbox[3], bbox[4], bbox[5], nvgRGBA(col[0], col[1], col[2], col[3]), nvgRGBA(0,0,0,0)));
		nvgFill(sit.nvgCtx);
	}
}

static void colorFromBorderStyle(CSSColor * col, CSSColor * ret, int side, int style)
{
	memcpy(ret, col, sizeof *ret);
	ULONG coef = 0xff000000;
	switch (style) {
	case BorderStyleGroove:
		if (side > 3) side = (side + 2) & 3;
		// no break;
	case BorderStyleInset:
		if (side == CSS_BOTTOM || side == CSS_RIGHT) coef = 0xffffffff;
		break;
	case BorderStyleRidge:
		if (side > 3) side ^= 1, side &= 3;
		// no break;
	case BorderStyleOutset:
		if (side == CSS_TOP || side == CSS_LEFT) coef = 0xffffffff;
		break;
	default:
		return;
	}

	/* blend colors with <coef> at a ratio of RATIO/256 */
	int i, m;
	#define RATIO  128
	for (i = 0; i < 4; i ++, coef >>= 8)
	{
		m = (col->rgba[i] * RATIO >> 8) + ((coef&0xff) * (256-RATIO) >> 8);
		if (m < 0) m = 0;
		if (m > 255) m = 255;
		ret->rgba[i] = m;
	}
	#undef RATIO
}

/* inward polygon offsetting */
static void renderPushInward(SIT_Widget node, BoxF * border, RectF * orig, REAL shape[16], REAL fact, int sides)
{
	REAL * p;
	RectF  box;
	int    i;
	memcpy(&box, orig, sizeof box);

	REAL opp = 1-fact;
	box.left   -= (border->left) * opp;
	box.top    -= (border->top) * opp;
	box.width  += (border->left + border->right) * opp;
	box.height += (border->top  + border->bottom) * opp;

	for (i = 0, p = shape; i < 4; i ++, p += 3, sides = (sides >> 1) | ((sides & 1) << 3))
	{
		static uint8_t offsetx[] = {3,1,1,3};
		static uint8_t offsety[] = {0,0,2,2};
		p[0] = (&node->layout.majorRadius.top)[i] - (&border->top)[offsetx[i]] * fact;
		p[1] = (&node->layout.minorRadius.top)[i] - (&border->top)[offsety[i]] * fact;
		if ((sides & 9) != 9) p[0] = p[1] = 0;
		p[2] = (&box.width)[i&1];
		if (p[0] < 0 || p[1] < 0) p[0] = p[1] = 0;
	}
	p[0] = box.left;
	p[1] = box.top;
	for (i = 0; i < 12; i += 3)
	{
		static uint8_t pts[] = {2,0,3,   5,4,7,   8,6,9,   11,1,10};
		REAL * d, * p2;
		d  = shape + pts[i];
		p  = shape + pts[i+1];
		p2 = shape + pts[i+2];

		if (*d + EPSILON < *p + *p2)
			*p = *p2 = 0;
		*d -= *p + *p2;
		if (*d < EPSILON) *d = 0;
	}
	shape[14] = box.width;
	shape[15] = box.height;
}

static void renderGetSegment(REAL * out, REAL * in, int side)
{
	out[0] = in[12];
	out[1] = in[13];
	switch (side) {
	case 0:
		out[2] = out[0] += in[0];
		out[3] = out[1];
		out[2] += in[2];
		break;
	case 1:
		out[2] = out[0] += in[14];
		out[3] = out[1] += in[4];
		out[3] += in[5];
		break;
	case 2:
		out[2] = out[0] += in[14] - in[6];
		out[3] = out[1] += in[15];
		out[2] -= in[8];
		break;
	case 3:
		out[2] = out[0];
		out[3] = out[1] += in[1];
		out[1] += in[11];
	}
}

enum
{
	FLAG_REVERSE  = 1,
	FLAG_45DEG    = 2,
	FLAG_OPPOSITE = 4
};

static Bool renderGetArc(REAL * out, REAL * in, int side /*TL,TR,BR,BL*/, int flag)
{
	REAL * p = in + side * 3, as, ae;
	if (*p == 0) return False;
	if (side == 1 || side == 2)
		out[2] = in[12] + in[14], out[0] = out[2] - p[0] * 2;
	else
		out[0] = in[12], out[2] = in[12] + p[0] * 2;

	if (side < 2)
		out[1] = in[13], out[3] = in[13] + p[1] * 2;
	else
		out[3] = in[13] + in[15], out[1] = out[3] - p[1] * 2;

	as = ((side-1)&3) * 90;
	ae = as + 90;

	if (flag & FLAG_45DEG) {
		if (flag&FLAG_OPPOSITE) ae -= 45; else as += 45;
	}
	if (flag & FLAG_REVERSE)
		out[4] = ae, out[5] = as;
	else
		out[4] = as, out[5] = ae;
	return True;
}

static void renderArc(REAL * pts)
{
	/* pts: first 4 entries are bbox of ellipse, pts[4] is starting angle (in CCW deg [0 - 360], 0 is top of center), pts[5] is ending angle */
	REAL xray = (pts[2] - pts[0]) / 2;
	REAL yray = (pts[3] - pts[1]) / 2;
	nvgEllipseArc(sit.nvgCtx, pts[0] + xray, pts[1] + yray, fabs(xray), fabs(yray), nvgDegToRad(pts[4]-90), nvgDegToRad(pts[5]-90),
		pts[4] < pts[5] ? NVG_CW : NVG_CCW);
}

/*
 * border rendering: used by inline and block elements
 * note: dotted and dashed are not supported, use border-image instead
 */
void renderBorder(SIT_Widget node, RectF * box, int flag)
{
	static uint8_t styles[] = { /* indexed by border->style */
		0, 0, 0, 0, 0, 1, 2, 2, 0, 0
	};
	static uint8_t passesPerStyle[] = {
		0, 0, 1, 1, 1, 2, 2, 2, 1, 1
	};
	Border * border;
	BoxF     borders;
	REAL     shape[16], inner[16], poly[6];
	int      i, j, merge, pass, passes;

	if (box == NULL) box = &node->layout.pos;
	borders = node->layout.border;
	/* flag < 15 is used by inlines */
	if ((flag & 1) == 0) borders.top    = 0;
	if ((flag & 2) == 0) borders.right  = 0;
	if ((flag & 4) == 0) borders.bottom = 0;
	if ((flag & 8) == 0) borders.left   = 0;

	for (i = 0, border = &node->style.borderTop, passes = 0; i < 4; i ++, border ++)
	{
		pass = passesPerStyle[border->style];
		if (passes < pass) passes = pass;
	}

	/* check doc/appendix D for a quick explanation of how this part works, otherwise here be dragons */
	for (pass = 0; pass < passes; pass ++)
	{
		/* drawn in the order: top, right, bottom, left (CSS order) */
		for (i = 0, merge = (flag^0xf)<<8, border = &node->style.borderTop; i < 4; i ++, border ++)
		{
			if ((&borders.top)[i] <= 0 || border->color.val == 0 || border->style <= BorderStyleHidden || (merge & (0x100<<i))) continue;

			switch (border->style) {
			case BorderStyleDouble:
				renderPushInward(node, &borders, box, shape, pass == 0 ? 0 : 2/3., flag);
				renderPushInward(node, &borders, box, inner, pass == 0 ? 1/3. : 1, flag);
				break;
			case BorderStyleGroove:
			case BorderStyleRidge:
				renderPushInward(node, &borders, box, shape, pass == 0 ? 0 : 0.5, flag);
				renderPushInward(node, &borders, box, inner, pass == 0 ? 0.5 : 1, flag);
				break;
			default:
				if (pass > 0) continue;
				renderPushInward(node, &borders, box, shape, 0, flag);
				renderPushInward(node, &borders, box, inner, 1, flag);
			}

			/*
			 * When you are rendering polygons with shared edges and anti-aliasing, you can see a thin line
			 * along these edges. To eliminate this problem, parts of the border that are the same color has
			 * to be rendered as one polygon.
			 */
			CSSColor ref;
			int      style = styles[border->style];
			uint8_t  oldcnt, count, arc, opp, flg;

			colorFromBorderStyle(&border->color, &ref, i+(pass<<2), border->style);
			merge |= 0x111 << i;
			count  = 1;
			/* get all the borders that are contiguous */
			for (j = i+1; j < 4; j ++)
			{
				CSSColor col;
				Border * b = &node->style.borderTop + j;

				if (b->color.val == 0 || (&borders.top)[j] <= 0 || border->style <= BorderStyleHidden || (merge & (0x100<<j)) || (merge & (5<<(j-1))) == 0)
					continue;

				colorFromBorderStyle(&b->color, &col, j+(pass<<2), b->style);
				if (col.val == ref.val && styles[b->style] == style)
					merge |= 0x111 << j, count ++;
			}

			/* there is at least one border to paint at this point */
			nvgFillColorRGBA8(sit.nvgCtx, ref.rgba);
			nvgPathWinding(sit.nvgCtx, NVG_CCW);
			nvgBeginPath(sit.nvgCtx);

			/* where to start */
			j = i;
			flg = 0;
			if ((merge & 15) < 15)
			{
				static uint8_t prevSide[] = {8,1,2,4};
				while (merge & prevSide[j]) j --, j &= 3;
			}

			/* draw inner path */
			if (inner[14] > 0 && inner[15] > 0)
			{
				for (oldcnt = count; oldcnt > 0; j ++, j &= 3, oldcnt --)
				{
					renderGetSegment(poly, inner, j);
					if (oldcnt == count)
						nvgMoveTo(sit.nvgCtx, poly[0], poly[1]);
					nvgLineTo(sit.nvgCtx, poly[2], poly[3]);
					opp = (j+1) & 3;
					flg = FLAG_OPPOSITE;
					if ((&borders.top)[opp] != 0 && (merge & (1<<opp)) == 0) flg |= FLAG_45DEG;
					if (! renderGetArc(poly, inner, opp, flg))
					{
						REAL seg[4];
						/* edge case: border.html#div7 and #div8 */
						renderGetSegment(seg, inner, opp);
						if (seg[0] != poly[2] || seg[1] != poly[3])
							nvgLineTo(sit.nvgCtx, seg[0], seg[1]);
					}
					else renderArc(poly);
				}
			}
			else if (count < 4)
			{
				/* only initiate path for outer border */
				j += count;
				j &= 3;
				renderGetSegment(poly, inner, j);
				nvgMoveTo(sit.nvgCtx, poly[0], poly[1]);
			}
			opp = j;

			/* outer path */
			if (count == 4)
			{
				nvgPathWinding(sit.nvgCtx, NVG_CW);
				/* need to break previous path */
				nvgMoveTo(sit.nvgCtx, shape[12] + shape[0], shape[13]);
			}
			oldcnt = count;
			if (shape[14] > 0 && shape[15] > 0)
			for (j --, j &= 3; count > 0; count --)
			{
				if ((arc = renderGetArc(poly, shape, opp, flg|FLAG_REVERSE)))
				{
					renderArc(poly);
				}

				renderGetSegment(poly, shape, j);

				if (! arc)
				{
					if (flg & FLAG_45DEG)
					{
						nvgLineTo(sit.nvgCtx, poly[2], poly[3]);
					}
					else if (count == oldcnt)
					{
						if (count == 4)
							nvgMoveTo(sit.nvgCtx, poly[2], poly[3]);
						else
							nvgLineTo(sit.nvgCtx, poly[2], poly[3]);
					}
				}

				nvgLineTo(sit.nvgCtx, poly[0], poly[1]);

				opp = j;
				flg = 0;
				j = (opp-1)&3;
				if ((&borders.top)[j] != 0 && (merge & (1<<j)) == 0)
					flg |= FLAG_45DEG;
			}

			/* end cap (unless entire border is uniform) */
			if (oldcnt != 4)
			{
				if ((arc = renderGetArc(poly, shape, opp, flg|FLAG_REVERSE)))
					renderArc(poly);

				if (! renderGetArc(poly, inner, opp, flg))
				{
					renderGetSegment(poly, inner, opp);
					nvgLineTo(sit.nvgCtx, poly[0], poly[1]);
				}
				else renderArc(poly);
			}
			#if 0
			/* DEBUG: stroke instead of fill */
			nvgClosePath(sit.nvgCtx);
			nvgStrokeColorRGBA8(sit.nvgCtx, ref.rgba);
			nvgStroke(sit.nvgCtx);
			#else
			nvgFill(sit.nvgCtx);
			#endif
			merge &= ~0xff;
		}
	}
}

/* handle border-image */
static void renderBorderImg(SIT_Widget node, RectF * box, int flag)
{
	BImgCache cache;
	CSSImage  img;
	BorderImg bimg  = node->style.borderImg;
	Box       slice = bimg->slice;
	RectF     rect  = *box;
	int       i;

	if ((node->style.flags & CSSF_BORDERIMG) == 0)
		renderCacheBorderImg(node, box);

	img = bimg->image;
	if (img == NULL)
		return;

	cache = bimg->cache;
	{
		BoxF border = node->layout.border;
		rect.width  += bimg->offFloat.right  + rect.left + border.right;
		rect.height += bimg->offFloat.bottom + rect.top  + border.bottom;
		rect.left   -= bimg->offFloat.left + border.left;
		rect.top    -= bimg->offFloat.top  + border.top;
	}

	/* 4 corners */
	for (i = 0; i < 4; i ++)
	{
		REAL xr, yr, wr, hr; /* rect */
		REAL xi, yi, wi, hi; /* image */
		if (i & 1)
		{
			wr = cache.width.right;             xr = rect.width - wr;
			wi = cache.bdsz.right * img->width; xi = rect.width - wi;
		}
		else
		{
			wi = cache.bdsz.left * img->width; xr = xi = rect.left;
			wr = cache.width.left;
		}
		if (i >= 2)
		{
			hr = cache.width.bottom;              yr = rect.height - hr;
			hi = cache.bdsz.bottom * img->height; yi = rect.height - hi;
		}
		else
		{
			hi = cache.bdsz.top * img->height; yr = yi = rect.top;
			hr = cache.width.top;
		}
		nvgBeginPath(sit.nvgCtx);
		nvgRect(sit.nvgCtx, xr, yr, wr, hr);
		nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, xi, yi, wi, hi, 0, img->handle, 1));
		nvgFill(sit.nvgCtx);
	}

	/* repeat sections: top & bottom */
	for (i = 0; i < 4; i += 2)
	{
		REAL xr, yr, wr, hr;
		REAL yi, hi;
		REAL max = img->width - slice.left - slice.right;

		if (cache.count[i] == 0) continue;
		wr = cache.pos.width;
		hr = (&cache.width.top)[i];
		xr = cache.pos.left + box->left;
		hi = (&cache.bdsz.top)[i] * img->height;
		if (i == 0) yi = yr = rect.top;
		else        yi = rect.height-hi, yr = rect.height-cache.width.bottom;

		if (bimg->repeatH == BorderImageStretch)
		{
			REAL wi = img->width * wr / max;

			nvgBeginPath(sit.nvgCtx);
			nvgRect(sit.nvgCtx, xr, yr, wr, hr);
			nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, xr + (wr - wi) * 0.5f, yi, wi, hi, 0, img->handle, 1));
			nvgFill(sit.nvgCtx);
		}
		else /* repeat/space/round */
		{
			REAL xs    = cache.start[i] + box->left;
			REAL range = (&cache.range.top)[i];
			REAL space = cache.space[i] + range;
			REAL wi    = (&cache.bdsz.top)[i] * img->width * range / max;
			REAL xoff  = slice.left * range / max;
			int  count = cache.count[i], j;
			nvgScissor(sit.nvgCtx, xr, yr, wr, hr);
			for (j = 0; j < count; j ++, xs += space)
			{
				/* has to draw slices one at a time :-/ */
				nvgBeginPath(sit.nvgCtx);
				nvgRect(sit.nvgCtx, xs, yr, range, (&cache.width.top)[i]);
				nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, xs - xoff, yi, wi, hi, 0, img->handle, 1));
				nvgFill(sit.nvgCtx);
			}
			nvgResetScissor(sit.nvgCtx);
		}
	}

	/* repeat sections: left & right */
	for (i = 1; i < 4; i += 2)
	{
		REAL xr, yr, wr, hr;
		REAL xi, wi;
		REAL max = img->height - slice.top - slice.bottom;

		if (cache.count[i] == 0) continue;
		hr = cache.pos.height;
		wr = (&cache.width.top)[i];
		yr = cache.pos.top+box->top;
		wi = (&cache.bdsz.top)[i] * img->width;
		if (i == 3) xi = xr = rect.left;
		else        xi = rect.width-wi, xr = rect.width-cache.width.right;

		if (bimg->repeatH == BorderImageStretch)
		{
			REAL hi = img->width * hr / max;

			nvgBeginPath(sit.nvgCtx);
			nvgRect(sit.nvgCtx, xr, yr, wr, hr);
			nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, xi, yr + (hr - hi) * 0.5f, wi, hi, 0, img->handle, 1));
			nvgFill(sit.nvgCtx);
		}
		else /* repeat/space/round */
		{
			REAL ys    = cache.start[i] + box->top;
			REAL range = (&cache.range.top)[i];
			REAL space = cache.space[i] + range;
			REAL hi    = (&cache.bdsz.top)[i] * img->height * range / max;
			REAL yoff  = slice.top * range / max;
			int  count = cache.count[i], j;
			nvgScissor(sit.nvgCtx, xr, yr, wr, hr);
			for (j = 0; j < count; j ++, ys += space)
			{
				nvgBeginPath(sit.nvgCtx);
				nvgRect(sit.nvgCtx, xr, ys, (&cache.width.top)[i], range);
				nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, xi, ys - yoff, wi, hi, 0, img->handle, 1));
				nvgFill(sit.nvgCtx);
			}
			nvgResetScissor(sit.nvgCtx);
		}
	}
}

/* will use renderBorder() */
static void renderOutline(SIT_Widget node, RectF * rect)
{
	Border oldborder[4];
	BoxF   oldradius[3]; /* border, minor, major */
	RectF  box = *rect;

	if (node->layout.outlineWidth < EPSILON || node->style.outline.style == BorderStyleNone)
		return;

	memcpy(oldborder, &node->style.borderTop, sizeof oldborder);
	memcpy(oldradius, &node->layout.border, sizeof oldradius);

	REAL off = node->layout.outlineOffset;
	box.left   -= oldradius->left + off;
	box.top    -= oldradius->top  + off;  off *= 2;
	box.width  += oldradius->left + oldradius->right + off;
	box.height += oldradius->top + oldradius->bottom + off;

	node->style.borderTop = node->style.borderBottom = node->style.borderLeft = node->style.borderRight = node->style.outline;
	node->layout.border.top = node->layout.border.bottom = node->layout.border.left = node->layout.border.right = node->layout.outlineWidth;

	if ((node->layout.flags & LAYF_NoRoundBorder) == 0)
	{
		REAL * minor;
		REAL * major;
		int    i;
		off = node->layout.outlineWidth + node->layout.outlineOffset;
		for (i = 0, major = &node->layout.majorRadius.top, minor = &node->layout.minorRadius.top; i < 4; i ++, major ++, minor ++)
		{
			if (*minor > 0) *minor += off;
			if (*major > 0) *major += off;
		}
	}

	renderBorder(node, &box, 15);

	memcpy(&node->style.borderTop, oldborder, sizeof oldborder);
	memcpy(&node->layout.border, oldradius, sizeof oldradius);
}

#if 0
/* check if word <w> is the last in the <node>, who is an inline item within <parent> */
static Bool renderIsNth(SIT_Widget parent, WordWrap w, SIT_Widget node, int nth)
{
	SIT_Widget c;
	int index = w - (WordWrap) parent->wordwrap.buffer;
	if ((nth > 0 && index == parent->wordwrap.count - 1) || (nth < 0 && index == 0)) return True;
	for (w += nth, c = w->node; c && c != node && c != parent; c = c->parent);
	return c != node;
}
#endif

/* render background and border of inline elements. XXX this function looks like black magic :-( */
static void renderInline(SIT_Widget offset, WordWrap words, int index, int max, Bool first, REAL x, REAL y)
{
	SIT_Widget   n;
	SIT_Widget * stack;
	int          flag, i, len, last;

	for (n = words->node, i = 0; n != offset; ++i,            n = n->parent); stack = alloca(sizeof *stack * i);
	for (n = words->node, i = 0; n != offset; stack[i++] = n, n = n->parent);
	for (i--; i >= 0; i --)
	{
		//SIT_Widget margin;
		WordWrap   w;
		RectF      box;
		REAL       old;
		int        ws;
		n = stack[i];
		flag = 5;
		/* several words may point to this node, but it must only be rendered once */
		if (n->layout.startword == index || first) flag |= 8;
		else if (! first) continue; /* already drawn */

		/* nothing to render? skip rendering */
		if (! HasBg(n)) continue;
		len = n->layout.nbword - (index - n->layout.startword);
		if (len > max)
		{
			len = max; /* <span> split on several lines */
		}
		else flag |= 2;
		last = max - len;

		/* need to be rendered from bottom to top */
		for (box.height = box.width = old = 0, box.top = 1e6, w = words, ws = 0; len > 0; w ++, len --)
		{
			//for (margin = w->node, ws = margin->style.whiteSpace; margin && margin != n; margin = margin->parent);
			//if (margin == NULL) { flag |= 2; break; }
			//if (w->word == NULL)
				/* inline with no text: w->width include all margins for word wrapping, rendering must have inner width */
			//	flag |= 16;
			box.width += w->width + old + w->space; old = w->marginR;
			if (w != words) box.width += w->marginL;
			REAL top = w->y;
			//if (top == VATOP) top = w->h - hdr->baseLine - hdr->paddingTop;
			//else if (top == VABOTTOM)
			//	top = hdr->lineHeight - hdr->baseLine - hdr->paddingTop;
			//else if (top == VAMIDDLE)
			//	top = (w->h - hdr->lineHeight) / 2 + (hdr->lineHeight - hdr->baseLine);
			top += y;
			if (box.top > top) box.top = top; top += w->h;
			if (box.height < top) box.height = top;
		}
		/* must not render last space of line (unless it is part of white-space: pre) */
		if (len == 0 && last == 0 && ws != WhiteSpacePre)
			box.width -= w[-1].space;
		/* first and last word can have fused margins, unfuse them */
		box.left = x;
//		for (-- w, margin = w->node; margin != n; margin = margin->parent)
//			if (renderIsNth(offset, w, margin, 1)) box.width += layoutNCSize(margin, CSS_RIGHT);
//		if (flag & 8)
//		for (margin = words->node; margin != n; margin = margin->parent)
//			if (renderIsNth(offset, words, margin, -1)) box.width += old = layoutNCSize(margin, CSS_LEFT), box.left -= old;

		box.height -= box.top;
		if (n->layout.startword != index) box.width += words->marginL, box.left -= words->marginL;

		if (flag & 16)
		{
			box.left -= n->offsetX;
			box.top  -= n->offsetY - layoutNCSize(n, CSS_TOP);
		}

		//if (n->style.position == PositionRelative)
		//{
		//	box.left += n->layout.rel.left;
		//	box.top  += n->layout.rel.top;
		//}

		renderInitBox(n, &box, False, flag); len = 0;
		// this line is for border-radius on inline; the CM_IMG is for ACID2 eyes line :-/
//		if (n->style.display == DisplayInline && (n->flag & CM_IMG) == 0)
//			n->layout.pos.width = box.width, n->layout.pos.height = box.height, n->style.flags &= ~CSSF_INLINE, len = 1;

		if (HasBg(n)) layoutAdjustBorderRadius(n);
		if (n->layout.flags & LAYF_HasBoxShadow)  renderBoxShadow(n, &box, False);
		if (n->style.background)                  renderBackground(n, &box, flag);
		if (n->layout.flags & LAYF_HasInset)      renderBoxShadow(n, &box, True);
		if (n->style.borderImg)                   renderBorderImg(n, &box, flag); else
		if (! (n->layout.flags & LAYF_NoBorders)) renderBorder(n, &box, flag);
		if (! (n->layout.flags & LAYF_NoOutline)) renderOutline(n, &box);
//		if (len) n->style.flags |= CSSF_INLINE;
	}
}

struct Decoration_t
{
	REAL     startX;
	REAL     lineY, height;
	REAL     thick, baseline;
	uint8_t  type,  style, bold;
	CSSColor color;
};

typedef struct Decoration_t     Decor;

/* handle text-decoration, text-decoration-style and text-decoration-color */
static void renderFlushDeco(Decor * deco, REAL endX)
{
	RectF line;
	REAL  thick = deco->thick * (deco->bold ? 1/8.f : 1/16.f);

	if (deco->startX >= endX) return;
	line.top = deco->lineY;
	line.left = deco->startX;
	line.width = endX - line.left;
	line.height = thick;

	switch (deco->type) { /* order: under / over / strike */
	case 1: line.top += deco->height - thick * 1.2f; break;
	case 2: break;
	case 3: line.top += (deco->height - thick) * 0.5f;
	}

	/* otherwise, it will a blurry mess */
	line.top = roundf(line.top);

	switch (deco->style) {
	case TextDecoDouble:
		nvgFillColorRGBA8(sit.nvgCtx, deco->color.rgba);
		if (deco->type == 3) line.top -= thick;
		renderRect(&line);
		nvgFill(sit.nvgCtx);
		endX = 2.5f * thick;
		if (deco->type == 1) line.top -= endX;
		else line.top += endX;
		/* no break; */
	case TextDecoSolid:
		nvgFillColorRGBA8(sit.nvgCtx, deco->color.rgba);
		renderRect(&line);
		nvgFill(sit.nvgCtx);
		break;
	case TextDecoWavy: /* render a sine curve using the bezier control points: (0, 0) - (0.5, 0.5) - (1, 1) - (pi/2, 1) */
	{	int  i, n;
		REAL x, y, h, hy;

		/* we will overflow a bit, but no big deal */
		hy = h = thick;
		n = ceil((endX - deco->startX) / h);
		x = deco->startX;
		y = line.top + thick;

		nvgStrokeColorRGBA8(sit.nvgCtx, deco->color.rgba);
		nvgStrokeWidth(sit.nvgCtx, thick);
		nvgBeginPath(sit.nvgCtx);
		nvgMoveTo(sit.nvgCtx, x, y);

		for (i = 0; i < n; i ++, x += h)
		{
			#define INVPI    ((float)(1/M_PI))
			switch (i&1) {
			case 0: nvgBezierTo(sit.nvgCtx, x+h*INVPI, y+hy*0.5f, x+2*h*INVPI, y+hy, x+h, y+hy); break;
			case 1: nvgBezierTo(sit.nvgCtx, x+h-2*h*INVPI, y+hy, x+h-h*INVPI, y+hy*0.5f, x+h, y); hy = -hy;
			}
		}
		nvgStroke(sit.nvgCtx);
	}	break;
	}
	nvgStrokeWidth(sit.nvgCtx, 1);
	deco->startX = endX;
}

static SIT_Widget renderGetDeco(SIT_Widget node)
{
	SIT_Widget d;
	for (d = node; (d->style.flags & CSSF_INLINE); d = d->parent)
		if (d->style.font.decoration) return d;
	return d->style.font.decoration ? d : node;
}

#define IMGLABEL(w)    ((SIT_Label)w)->image

static TextShadow renderGetNthShadow(SIT_Widget node, int layer, Bool * more)
{
	TextShadow shadow = node->style.shadow;
	int        max    = node->style.shadowCount;
	if (shadow == NULL && node->style.shadowInherit)
	{
		SIT_Widget parent = node->style.shadowInherit;
		shadow = parent->style.shadow;
		max = parent->style.shadowCount;
	}
	*more = layer < max;
	return layer <= max ? shadow + max - layer : NULL;
}

enum /* overflow state machine */
{
	OVERFLOW_NONE,
	OVERFLOW_LEFT,
	OVERFLOW_RIGHT,
	OVERFLOW_MIDDLE,
	OVERFLOW_RWORDELIPSE,
	OVERFLOW_RWORDSKIP,
	OVERFLOW_LWORDELIPSE,
	OVERFLOW_LWORDSKIP,
	OVERFLOW_MWORDSKIP
};

typedef struct Overflow_t     Overflow;

struct Overflow_t
{
	REAL    width;
	REAL    ellipsis;
	REAL    skip;
	uint8_t state;
	uint8_t init;
	uint8_t dec;
};

/* main function for rendering text from inlines */
Bool renderWords(SIT_Widget node, RectF * box, int shadowLayer)
{
	static TEXT textOverflow[] = {'.', '.', '.'}; /* don't want final 0 */

	NVGcontext * vg = sit.nvgCtx;
	SIT_Widget   old;
	Overflow     overflow = {0};
	Decor        deco;
	Bool         ret = False;
	uint8_t      renderBg = (node->layout.flags & LAYF_RenderWordBg) > 0; /* need to be rendered only once */
	int          count = node->layout.wordwrap.count;

	switch (node->style.overflow) {
	case SITV_EllipsisRight:
		overflow.width = node->layout.pos.width;
		overflow.state = overflow.init = OVERFLOW_RIGHT;
		break;
	case SITV_EllipsisMiddle:
		overflow.state = overflow.init = OVERFLOW_MIDDLE;
		break;
	case SITV_EllipsisLeft:
		/* need to compute .width for each line */
		overflow.state = overflow.init = OVERFLOW_LEFT;
	}

	if (node->title || count > 0)
	{
		struct WordWrap_t single, word, ellipse;

		WordWrap w;
		REAL     cx = box->left + node->layout.padding.left;
		REAL     cy = box->top  + node->layout.padding.top;
		REAL     x, y, h, offX, offY;
		int      i, first;

		nvgTextAlign(sit.nvgCtx, NVG_ALIGN_TOP);
		memset(&deco, 0, sizeof deco);
		ellipse.n = 0;

		/* screw CSS vertical centering */
		if (node->style.verticalAlign == VerticalAlignMiddle)
		{
			/* vertical-align: middle on container box will vertically center text box in container */
			cy += (node->layout.pos.height - node->layout.textarea.height) * 0.5f;
		}

		if (count == 0)
		{
			w = &single;
			count = 1;
			memset(&single, 0, sizeof single);
			single.width   = node->layout.textarea.width;
			single.h       = node->layout.textarea.height;
			single.node    = node;
			single.word    = node->title;
			single.n       = strlen(single.word);
			single.marginL = node->layout.wordSpacing;
		}
		else w = vector_first(node->layout.wordwrap);

		if (overflow.state)
		{
			x = layoutLineWidth(w, count);
			if (x <= overflow.width) overflow.state = OVERFLOW_NONE;
		}

		for (old = NULL, x = cx, y = cy, h = 0, i = 1, first = 1, offX = offY = 0; count > 0; w ++, count --, i ++)
		{
			if (old != w->node)
			{
				SIT_Widget curDeco;
				CSSColor   colDeco;
				CSSColor   color;
				REAL       thick;

				curDeco = renderGetDeco(w->node);
				old = w->node;
				if (shadowLayer > 0)
				{
					/* PITA: shadow needs to be rendered layer per layer and can differs from node to node :-/ */
					TextShadow shadow = renderGetNthShadow(w->node, shadowLayer, &ret);
					if (shadow == NULL)
					{
						/* skip all nodes with the same parent */
						for (; ; w ++, count --, i ++)
						{
							if (h < w->h) h = w->h;
							if (w->nl)
							{
								x = cx; cy += h; y = cy;
								h = 0; first = 1;
							}
							else x += w->width + w->space + w->marginL + w->marginR, first = 0;
							if (count == 1 || w[1].node != old) break;
						}
						continue;
					}
					offX = shadow->pos.XYfloat[0];
					offY = shadow->pos.XYfloat[1];
					colDeco = color = shadow->color;
					nvgFontBlur(vg, shadow->blurFloat);
				}
				else colDeco = curDeco->style.decoColor, color = old->style.color, offX = offY = 0;

				thick = old->style.font.size;

				if (colDeco.val == 0)
					colDeco = old->style.color;

				if (fabsf(thick - deco.thick) > EPSILON ||
				    colDeco.val != deco.color.val ||
				    old->style.font.decoration != deco.type ||
				    old->style.decoStyle != deco.style)
				{
					if (deco.type) renderFlushDeco(&deco, x + offX);
					deco.color  = colDeco;
					deco.type   = old->style.font.decoration;
					deco.style  = old->style.decoStyle;
					deco.startX = x + offX;
					deco.thick  = thick;
					deco.bold   = old->style.font.weight >= 700;
					if (w->marginL < 0) deco.startX -= w->marginL;
				}

				old = w->node;
				nvgFillColorRGBA8(vg, color.rgba);
				nvgTextLetterSpacing(vg, old->layout.letterSpacing);
				nvgFontFaceId(vg, old->style.font.handle);
				nvgFontSize(vg, old->style.font.size);
				if (overflow.state)
					overflow.ellipsis = nvgTextBounds(vg, 0, 0, textOverflow, EOT(textOverflow), NULL);
			}
			REAL xf, yf;
			word = *w;
			/* handle overflow on the fly XXX won't work with text-align though */

			if (overflow.state)
			/* yeah, this switch is as scary as it looks :-/ */
			switch (overflow.state) {
			case OVERFLOW_MWORDSKIP:
				overflow.skip = x - cx;
				overflow.width += overflow.ellipsis;
				goto case_OVERFLOW_LEFT;
			case OVERFLOW_MIDDLE:
				overflow.skip = 0;
				overflow.width = node->layout.pos.width * 0.5f;
				goto case_OVERFLOW_LEFT;
			case OVERFLOW_LEFT:
				overflow.width = node->layout.pos.width;
				overflow.skip = 0;
			case_OVERFLOW_LEFT:
				{
					WordWrap p = w, eol;
					REAL     totalw;
					for (eol = w + count, totalw = 0; p < eol; p ++)
					{
						totalw += p->width + p->marginR + p->marginL + p->space;
						if (p->nl) { totalw -= p->space; break; }
					}
					if (overflow.state != OVERFLOW_MWORDSKIP && totalw <= node->layout.pos.width)
						overflow.width = -1;
					else
						overflow.width = totalw - (overflow.width - overflow.ellipsis);
				}
				if (overflow.width > 0)
				{
					if (overflow.state != OVERFLOW_MIDDLE)
					{
						word.word  = textOverflow;
						word.n     = DIM(textOverflow);
						word.width = ellipse.width = overflow.ellipsis;
						word.space = 0;
						overflow.state = OVERFLOW_LWORDSKIP;
						overflow.skip += ellipse.width;
						break;
					}
					else
					{
						overflow.state = OVERFLOW_RIGHT;
						overflow.width = node->layout.pos.width * 0.5f;
					}
				}
				else { overflow.state = OVERFLOW_NONE; break; }
				// no break;
			case OVERFLOW_RIGHT:
				yf = word.width + word.marginR + word.marginL;
				xf = x - cx + yf;
				if (xf <= overflow.width - overflow.ellipsis)
					break;
				word.n = nvgTextFit(vg, word.word, word.word + word.n, overflow.width - overflow.ellipsis - (xf - yf), &word.width);
				word.space = 0;
				ellipse = word;
				ellipse.word   = textOverflow;
				ellipse.n      = DIM(textOverflow);
				ellipse.width  = overflow.ellipsis;
				overflow.state = node->style.overflow == SITV_EllipsisMiddle ? OVERFLOW_MWORDSKIP : OVERFLOW_RWORDELIPSE;
				break;
			case OVERFLOW_LWORDSKIP:
				yf = word.width + word.marginR + word.marginL + word.space;
				xf = x - cx + yf - overflow.skip;
				if (xf > overflow.width)
				{
					/* truncate this word */
					REAL width;
					xf = overflow.width - (xf - yf);
					int n = nvgTextFit(vg, word.word, word.word + word.n, xf, &width);
					if (xf - width > EPSILON)
					{
						if (n < word.n)
							width += nvgTextBounds(vg, 0, 0, word.word+n, word.word+n+1, NULL), n ++;
						else
							word.space = 0;
					}
					word.word += n;
					word.width -= width;
					word.n -= n;
					x = cx + overflow.skip;
					overflow.state = OVERFLOW_NONE;
				}
				else word.n = 0; /* skip word */
				break;
			case OVERFLOW_RWORDELIPSE:
				/* we just clipped the text, now render ellipsis */
				word = ellipse;
				overflow.state = OVERFLOW_RWORDSKIP;
				break;
			case OVERFLOW_RWORDSKIP:
				/* skip until next line */
				word.n = 0;
			}
			yf = word.h + word.y;
			if (h < yf) h = yf;
			x += fabsf(word.marginL);
			xf = x + offX;
			yf = y + offY;
			deco.lineY = word.y + yf;
			deco.height = word.h;
			deco.baseline = word.bl;
			if (renderBg)
			{
				renderInline(node, w, i, count, first, x, y);
				if (old->layout.flags & LAYF_HasImg)
				{
					/* img.src not be confused with background-image */
					CSSImage src = (APTR) old->manage;
					nvgBeginPath(vg);
					nvgRect(vg, xf, yf + word.y, word.width, word.h);
					nvgFillPaint(vg, nvgImagePattern(vg, xf, yf + word.y, word.width, word.h, 0, src->handle, 1));
					nvgFill(vg);
				}
			}
			if (word.n < 0)
			{
				/* symbol image */
				CSSImage img   = (CSSImage) word.word;
				NVGpaint paint = nvgImagePattern(vg, xf, yf + word.y, word.width, word.bl, 0, img->handle, 1);
				nvgGetCurTextColor(vg, &paint.innerColor);
				nvgFillPaint(vg, paint);
				nvgBeginPath(vg);
				nvgRect(vg, xf, yf + word.y, word.width, word.bl);
				nvgFill(vg);
			}
			else if (word.n > 0)
			{
				nvgText(vg, xf, yf + word.y, word.word, word.word + word.n);
			}

			if (word.nl)
			{
				if (deco.type)
					renderFlushDeco(&deco, x + word.width);
				x = cx; cy += h; y = cy;
				if (deco.type)
				{
					deco.startX = x;
					if (count > 0 && w[1].marginL < 0) deco.startX -= w[1].marginL;
				}
				h = 0;
				first = 1;
				overflow.state = overflow.init;
			}
			else x += word.width + word.space + word.marginR, first = 0;
			if (word.word == textOverflow || overflow.state == OVERFLOW_MWORDSKIP)
				w --, i ++, count ++;
		}
		if (overflow.state == OVERFLOW_RWORDELIPSE)
		{
			nvgText(vg, x + offX, y + offY + ellipse.y, ellipse.word, ellipse.word + ellipse.n);
		}
		if (deco.type)
			renderFlushDeco(&deco, x);
	}
	return ret;
}

void renderTextShadow(SIT_Widget node, RectF * box)
{
	int i;

	/* shadow are ordered top to bottom in CSS (like multiple background) */
	for (i = 1; renderWords(node, box, i); node->layout.flags &= ~LAYF_RenderWordBg, i ++);

	nvgFontBlur(sit.nvgCtx, 0);
}

#if 0
/* handle overflow: hidden */
static Bool renderInitClip(SIT_Widget node, RectF * out)
{
	out->top = out->left = 0;
	out->width = out->height = 1e6;

	while (node && node->parent) /* stop at <HTML> */
	{
		if (node->style.overflow == OverflowHidden)
		{
			REAL cx = node->layout.pos.left + node->layout.offsetX;
			REAL cy = node->layout.pos.top  + node->layout.offsetY;

			/* intersect all clipping rectangles */
			if (cx > out->left) out->left = cx; cx += node->layout.pos.width;
			if (cy > out->top)  out->top  = cy; cy += node->layout.pos.height;
			if (cx < out->width)  out->width  = cx;
			if (cy < out->height) out->height = cy;
		}
		node = node->layout.offset;
	}
	if (out->width == 1e6) return False;
	out->width  -= out->left; if (out->width  < 0) out->width  = 0;
	out->height -= out->top;  if (out->height < 0) out->height = 0;
	return True;
}
#endif

/* words and border need special processing */
int SIT_FrameRender(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Frame frame = (SIT_Frame) w;
	RectF     box;

	box.left   = w->box.left + frame->padLeft;
	box.top    = w->box.top;
	box.width  = w->box.right  - w->padding[2] - box.left;
	box.height = w->box.bottom - w->padding[3] - box.top;
	box.left   = roundf(box.left + w->offsetX - w->layout.padding.left + w->padding[0] * 0.25f);
	box.top   += w->offsetY - w->layout.padding.top;

	renderWords(w, &box, 0);

	/* render border with text clipped */
	if (w->title)
	{
		REAL offX = 0;
		if (w->style.text.align > 0)
		{
			if (w->layout.wordwrap.count > 0)
			{
				WordWrap word = vector_first(w->layout.wordwrap);
				offX = fabsf(word->marginL);
			}
			else offX = w->layout.wordSpacing;
		}

		box.left   = w->box.left + w->layout.border.left;
		box.top    = roundf(w->box.top + frame->title.height * 0.6f);
		box.width  = w->box.right  - box.left - w->layout.border.right;
		box.height = w->box.bottom - box.top - w->layout.border.bottom;
		box.left  += w->offsetX;
		box.top   += w->offsetY;

		/* would be nice to have a way to invert scissor, but we need to intersect with current clipping rect anyway */
		nvgSave(sit.nvgCtx);
		nvgIntersectScissor(sit.nvgCtx, w->box.left + w->offsetX, w->box.top + w->offsetY, frame->padLeft + offX, w->box.bottom);
		renderBorder(w, &box, 1+4+8);
		nvgRestore(sit.nvgCtx);

		nvgSave(sit.nvgCtx);
		nvgIntersectScissor(sit.nvgCtx, w->box.left + frame->padLeft + w->offsetX, box.top, frame->title.width, w->box.bottom);
		renderBorder(w, &box, 4);
		nvgRestore(sit.nvgCtx);

		nvgSave(sit.nvgCtx);
		nvgIntersectScissor(sit.nvgCtx, w->box.left + frame->padLeft + frame->title.width + w->offsetX + offX, w->box.top + w->offsetY, box.width, w->box.bottom);
		renderBorder(w, &box, 1+2+4);
		nvgRestore(sit.nvgCtx);
	}
	return 1;
}

/* main rendering function: dispatching stub */
static void renderNode(SIT_Widget node)
{
	#if 0
	if (node->style.transform)
	{
		if (trans != node)
		{
			if (trans) renderRemTransform(&trans, node, a);
			renderAddTransform(&trans, node, a);
		}
	}
	else if (trans)
	{
		renderRemTransform(&trans, node, a);
	}
	#endif

	RectF box;
	REAL  pad[4];

	memcpy(pad, node->padding, sizeof pad);
	if (node->type == SIT_DIALOG && (((SIT_Dialog)node)->customStyles & SITV_Modal))
	{
		/* XXX should be done through CSS */
		nvgBeginPath(sit.nvgCtx);
		nvgRect(sit.nvgCtx, 0, 0, sit.scrWidth, sit.scrHeight);
		nvgFillColor(sit.nvgCtx, nvgRGBA(0, 0, 0, 128));
		nvgFill(sit.nvgCtx);
	}

//	if (renderInitClip(render->type == DNT_TAG ? node->offset : node, &box))
//		a->cliprect(box.left, box.top, box.width, box.height), off = node;

	renderInitBox(node, &box, True, 15);
	nvgGlobalAlpha(sit.nvgCtx, node->style.opacity);

	if (node->layout.flags & LAYF_AdjustRect)
		node->optimalWidth(node, &box, AdjustRenderRect);


	/* some area need to be updated outside of SITGL and must not be overdrawn yet */
	if ((node->flags & SITF_TopLevel) && sit.compoCount > 0)
	{
		/* check that this node does not draw over those areas */
		REAL area[] = {
			box.left - pad[0], box.left + box.width  + pad[2],
			box.top  - pad[1], box.top  + box.height + pad[3]
		};
		REAL * rect;
		int    count;
		for (count = sit.compoCount, rect = sit.compoArea; count > 0; count --, rect += 4)
		{
			if (rect[0] < area[1] && rect[2] > area[0] && rect[1] < area[3] && rect[3] > area[2])
			{
				/* overlap with a composited area: need to delay rendering for this node and its children */
				node->geomChanged = sit.composited;
				sit.composited = node;
				return;
			}
		}
	}

	if (node->composited && sit.compoCount < MAXCOMPO)
	{
		/* add to list of areas not to draw over */
		REAL * rect = sit.compoArea + sit.compoCount * 4;
		rect[0] = box.left - pad[0];
		rect[1] = box.top  - pad[1];
		rect[2] = box.left + box.width  + pad[2];
		rect[3] = box.top  + box.height + pad[3];
		sit.compoIds[++ sit.compoIds[0]] = node->composited;
		sit.compoCount ++;
	}

	if (node->layout.flags & LAYF_HasBoxShadow)  renderBoxShadow(node, &box, False);
	if (node->style.background)                  renderBackground(node, &box, 15);
	if (node->layout.flags & LAYF_HasInset)      renderBoxShadow(node, &box, True);
	if (node->style.borderImg)                   renderBorderImg(node, &box, 15); else
	if (! (node->layout.flags & LAYF_NoBorders)) renderBorder(node, &box, 15);
	if (! (node->layout.flags & LAYF_NoOutline)) renderOutline(node, &box);

	if (node->type == SIT_DIALOG)
	{
		box.left += node->style.font.size * 0.3f;
		box.top  -= (pad[1] + node->style.font.size) * 0.5f;
	}
	if (node->layout.flags & LAYF_HasImg)
	{
		/* img.src not be confused with background-image */
		CSSImage src = IMGLABEL(node);
		nvgBeginPath(sit.nvgCtx);
		pad[2] += pad[0];
		pad[3] += pad[1];
		nvgRect(sit.nvgCtx, box.left + pad[0], box.top + pad[1], box.width-pad[2], box.height-pad[3]);
		nvgFillPaint(sit.nvgCtx, nvgImagePattern(sit.nvgCtx, box.left+pad[0], box.top+pad[1], box.width-pad[2], box.height-pad[3], 0, src->handle, 1));
		nvgFill(sit.nvgCtx);
	}

	if ((node->layout.flags & LAYF_IgnoreWords) == 0)
	{
		node->layout.flags |= LAYF_RenderWordBg;
		if (node->style.shadowTotal > 0)
			renderTextShadow(node, &box);
		renderWords(node, &box, 0);
	}

	if (node->render)
		node->render(node, &box, NULL);

	if (HAS_EVT(node, SITE_OnPaint))
	{
		SIT_OnPaint paint = {
			.x = box.left, .w = box.width, .nvg = sit.nvgCtx, .fontId = node->style.font.handle,
			.y = box.top,  .h = box.height, .fontSize = node->style.font.size
		};
		/* retrieve text-shadow in case cb want to handle it */
		paint.shadow = (APTR) node->style.shadow;
		paint.shadowCount = node->style.shadowCount;
		if (paint.shadow == NULL && node->style.shadowInherit)
		{
			SIT_Widget parent = node->style.shadowInherit;
			paint.shadow = (APTR) parent->style.shadow;
			paint.shadowCount = parent->style.shadowCount;
		}
		nvgSave(paint.nvg);
		nvgFillColorRGBA8(paint.nvg, node->style.color.rgba);
		nvgTextLetterSpacing(paint.nvg, node->layout.letterSpacing);
		nvgFontFaceId(paint.nvg, node->style.font.handle);
		nvgFontSize(paint.nvg, node->style.font.size);
		SIT_ApplyCallback(node, &paint, SITE_OnPaint);
		nvgRestore(paint.nvg);
	}
}

DLLIMP void SIT_RenderNode(SIT_Widget root)
{
	SIT_Widget c;

	renderNode(root);

	/* children first */
	for (c = HEAD(root->children); c; NEXT(c))
	{
		if ((c->flags & SITF_TopLevel) || ! c->visible) continue;
		if ((c->flags & SITF_RenderChildren) && c->children.lh_Head)
			SIT_RenderNode(c);
		else
			renderNode(c);
	}

	/* top level next */
	for (c = HEAD(root->children); c; NEXT(c))
	{
		if ((c->flags & SITF_TopLevel) && c->visible)
			SIT_RenderNode(c);
	}
}

void SIT_ClearGL(void);

/* render all visible nodes in the hierarchy */
DLLIMP SIT_RENDER SIT_RenderNodes(double time)
{
	sit.compoCount = 0;
	sit.compoIds[0] = 0;
	/* the widget tree defines the drawing order */
	if (sit.composited)
	{
		/* multi-pass rendering for composited nodes */
		SIT_Widget node;
		nvgBeginFrame(sit.nvgCtx, sit.scrWidth, sit.scrHeight, 1);
		for (node = sit.composited, sit.composited = NULL; node; node = node->geomChanged)
			SIT_RenderNode(node);
		nvgEndFrame(sit.nvgCtx);
		return sit.composited ? SIT_RenderComposite : SIT_RenderDone;
	}

	if (sit.nextAction <= time)
		SIT_ActionDispatch(time);

	if (sit.geomList)
	{
		SIT_Widget list = sit.geomList;
		sit.geomList = NULL;
		SIT_ReflowLayout(list);
		sit.dirty = 1;
	}

	if (sit.root->flags & SITF_GeometryChanged)
		SIT_LayoutWidgets(sit.root, FitUsingCurrentBox);

	while (sit.pendingDel.lh_Head)
	{
		SIT_Widget w = (SIT_Widget) ListRemHead(&sit.pendingDel);
		w->flags &= ~ SITF_IsLocked;
		SIT_DestroyWidget(w);
	}

	sit.root->visible = 1;
	sit.curTime = time;

	if (sit.refreshMode == SITV_RefreshAsNeeded && ! sit.dirty)
		return SIT_RenderNothing;

	#ifdef DEBUG_SIT
//	static int render = 1;
//	fprintf(stderr, "frame render = %d\r", render ++);
	#endif
	sit.dirty = False;

	SIT_ClearGL();
	nvgBeginFrame(sit.nvgCtx, sit.scrWidth, sit.scrHeight, 1);

	SIT_RenderNode(sit.root);

	nvgEndFrame(sit.nvgCtx);

	return sit.composited ? SIT_RenderComposite : SIT_RenderDone;
}
