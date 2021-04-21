/*
 * SIT_Gradient.c : linear and radial gradient rasterization from CSS parameters.
 *
 * written by T.Pierron, jan 2020.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSParser.h"

/* private datatype */
typedef struct Iter_t
{
	int x, y, xe, dy;
	int dx, err, sx, sy;
}	Iter;

/* uncomment to use sine curves for transition between color stops (linear-gradient only) */
//#define SINE_CURVES

/* Q'n'D digital differential analyzer */
static void ddaInit(Iter * iter, int xe, int ys, int ye)
{
	/* pre-condition: xe > xs >= 0 */
	div_t q = div(ye - ys, xe);
	iter->y   = ys;
	iter->x   = 0;
	iter->xe  = xe;
	iter->err = xe >> 1;
	iter->dx  = abs(q.rem);
	iter->sx  = q.quot;
	iter->sy  = (ys < ye ? 1 : -1);
	iter->dy  = ye - ys;
}

#define ISDDAEND(iter) ((iter).x >= (iter).xe)

static inline void ddaIter(Iter * iter)
{
	iter->x ++;
	iter->y += iter->sx;
	iter->err -= iter->dx;
	if (iter->err < 0)
		iter->y += iter->sy, iter->err += iter->xe;
}

static void gradientInitColorStops(DATA8 c1, DATA8 c2, DATA8 dest)
{
	memcpy(dest,   c1, 4);
	memcpy(dest+4, c2, 4);
	/* if one of the color's alpha channel is 0, do not modify RGB component of other color */
	if (memcmp(c1, "\0\0\0", 4) == 0) dest[0] = dest[4], dest[1] = dest[5], dest[2] = dest[6]; else
	if (memcmp(c2, "\0\0\0", 4) == 0) dest[4] = dest[0], dest[5] = dest[1], dest[6] = dest[2];
}

static int gradientToPx(int ref, ULONG fixed, REAL ratio)
{
	uint16_t num  = fixed >> 16;
	int      frac = (fixed & 0xfffc) >> 2;
	REAL     res  = (num&0x7fff) + frac / 16384.;

	if (num & 0x8000) res = -res;

	switch (fixed & 3) {
	case 0: return res; /* px */
	case 1: return res * ratio; /* em */
	case 2: return res * ref / 100; /* % */
	}
	return 0;
}

/* main entry point for linear-gradient rasterization */
Bool gradientDrawLinear(CSSImage img, Gradient * grad, REAL ratio)
{
	if (img->width == 0 || grad->colorStop < 2)
		return False;

	ColorStop cs, colors, c;
	int       i, hyp, distrib, count, dist;

	count  = grad->colorStop;
	colors = alloca(sizeof *colors * (count+2));
	hyp    = img->width;

	#ifdef SINE_CURVES
	static uint8_t sinLUT[255];
	if (sinLUT[255] == 0)
	{
		for (i = 0; i < 256; i ++)
			sinLUT[i] = (sin(i * M_PI / 255 - M_PI_2) / 2 + 0.5) * 255;
	}
	#endif

	/* parse input gradient */
	for (i = 0, distrib = -1, cs = grad->colors, c = colors, dist = 0; i < count; i ++, c ++, cs ++)
	{
		memcpy(c->rgba, cs->rgba, 4);

		switch (cs->pos & 3) {
		case 0:
		case 1:
		case 2: dist = gradientToPx(hyp, cs->pos, ratio); break;
		case 3: /* position not set */
			if (i == count-1) { dist = hyp; break; }
			if (i == 0)       { dist = 0; break; }
			if (distrib < 0) distrib = c - colors; continue;
		}
		if (dist < 0) dist = -dist;
		c->pos = dist;
		if (distrib >= 0)
		{
			ColorStop d;
			int       j, diff, num;
			for (d = colors + distrib, num = d[-1].pos, distrib = c - d + 1, j = 1, diff = c->pos - num; d != c; d ++, j ++)
				d->pos = num + (diff * j / distrib);
			distrib = -1;
		}
	}

	if (! grad->repeat)
	{
		if (colors->pos > 0)
		{
			/* need to start at 0 (not necessarily end at <hyp> */
			memmove(colors+1, colors, count * sizeof *colors);
			colors->pos = 0;
		}

		if (c[-1].pos < hyp)
		{
			/* need to cover the full distance */
			memcpy(c, c - 1, sizeof *c);
			c->pos = hyp;
			count ++;
		}
	}

	/* only need one scanline, nanovg will repeat and rotate the gradient after that */
	hyp *= 4;

	DATA8 p = img->bitmap = malloc(hyp), end = p + hyp;
	if (! p) return False;

	/* draw gradient */
	for (c = colors+1, i = count-1; i > 0; )
	{
		Iter r, g, b, a;
		#ifdef SINE_CURVES
		Iter L;
		#endif

		dist = c->pos - c[-1].pos;
		if (dist > 0)
		{
			uint8_t cs[8];
			gradientInitColorStops(c[-1].rgba, c->rgba, cs);
			ddaInit(&r, dist, cs[0], cs[4]);
			ddaInit(&g, dist, cs[1], cs[5]);
			ddaInit(&b, dist, cs[2], cs[6]);
			ddaInit(&a, dist, cs[3], cs[7]);
			#ifdef SINE_CURVES
			ddaInit(&L, dist, 0, 255);
			#endif
			if (p > img->bitmap)
			{
				ddaIter(&r);
				ddaIter(&g);
				ddaIter(&b);
				ddaIter(&a);
				#ifdef SINE_CURVES
				ddaIter(&L);
				#endif
			}

			while (dist > 0)
			{
				#ifdef SINE_CURVES
				p[0] = cs[0] + (r.dy * sinLUT[L.y] >> 8); ddaIter(&r);
				p[1] = cs[1] + (g.dy * sinLUT[L.y] >> 8); ddaIter(&g);
				p[2] = cs[2] + (b.dy * sinLUT[L.y] >> 8); ddaIter(&b);
				p[3] = cs[3] + (a.dy * sinLUT[L.y] >> 8); ddaIter(&a);
				ddaIter(&L);
				#else
				p[0] = r.y; ddaIter(&r);
				p[1] = g.y; ddaIter(&g);
				p[2] = b.y; ddaIter(&b);
				p[3] = a.y, ddaIter(&a);
				#endif
				p += 4; dist --;
				if (p == end) return True;
			}
		}
		i --; c ++;
		if (i == 0 && grad->repeat)
		{
			c = colors;
			i = count;
		}
	}
	return True;
}

typedef uint16_t * DATA16;

static void memcpy_repeat(DATA8 dst, DATA8 rgba, int len, int maxlen)
{
	while (len > maxlen)
		memcpy(dst, rgba, maxlen), len -= maxlen, dst += maxlen;
	memcpy(dst, rgba, len);
}

/* quick'n'dirty radial rasterization function: draw a bunch of circles using midpoint elliptical algorithm */
Bool gradientDrawRadial(CSSImage img, Gradient * grad, REAL ratio)
{
	ColorStop colors, c;
	int       i, j, stride = img->width * 4, h = img->height;
	DATA16    scany;
	DATA8     s, e;
	Iter      r, g, b, a;
	uint8_t   rgba[40];
	int       maxlen = 0, distrib;
	int       xc, yc, rx, ry;

	i      = grad->colorStop;
	colors = alloca(sizeof *colors * i);

	img->bitmap = malloc(stride * h);

	xc = gradientToPx(img->width,  grad->cx, ratio);
	yc = gradientToPx(img->height, grad->cy, ratio);

	/* normalize CSS input */
	if (grad->ry == 0xff)
	{
		int shape  = (grad->rx >> 2) & 3;
		int corner = (grad->rx >> 4);
		int hw     = img->width / 2;
		int hh     = h / 2;

		/* closest-side,closest-corner,farthest-side,farthest-corner */
		if (corner < 2)
		{
			/* closest */
			rx = (xc <= hw ? xc : img->width - xc);
			ry = (yc <= hh ? yc : h - yc);
		} else { /* farthest */
			rx = (xc > hw ? xc : img->width - xc);
			ry = (yc > hh ? yc : h - yc);
		}
		if (corner & 1) /* corner */
		{
			int hyp = sqrt(rx * rx + ry * ry);

			if (rx > ry)
			{
				ry = hyp * ry / rx;
				rx = hyp;
			}
			else
			{
				rx = hyp * rx / ry;
				ry = hyp;
			}
		}
		if (shape == 0) /* circle */
		{
			if (corner < 2)
				rx = ry = MIN(rx, ry);
			else
				rx = ry = MAX(rx, ry);
		}
	}
	else /* direct value */
	{
		rx = gradientToPx(img->width,  grad->rx, ratio);
		ry = gradientToPx(img->height, grad->ry, ratio);
	}

	if (rx < 0 || rx > img->width)  return False;
	if (ry < 0 || ry > h) return False;

	/* convert colors stops */
	memcpy(colors, grad->colors, sizeof *colors * i);
	for (j = 0, c = colors, distrib = -1; j < i; j ++, c ++)
	{
		int dist = 0;
		switch (c->pos & 3) {
		case 0:
		case 1:
		case 2: dist = gradientToPx(rx, c->pos, ratio); break;
		case 3:
			if (j == i-1) { dist = rx; break; }
			if (j == 0)   { dist = 0; break; }
			if (distrib < 0) distrib = c - colors; continue;
		}
		c->pos = dist;
		if (distrib >= 0)
		{
			ColorStop d;
			int       k, diff, num;
			for (d = colors + distrib, num = d[-1].pos, distrib = c - d + 1, k = 1, diff = c->pos - num; d != c; d ++, k ++)
				d->pos = num + (diff * k / distrib);
			distrib = -1;
		}
	}

	i = ry*2+2;
	scany = (i <= 2048 ? alloca(i) : malloc(i));
	if (! scany) return False;
	memset(scany, 0, i);

	/* this is the rasterization part: not the most optimized version, but at least it works... */
	for (i = 1; i <= rx; i ++)
	{
		DATA8 d, d2;
		int   ryi = i * ry / rx;
		int   rxSq = i * i;
		int   rySq = ryi * ryi;
		int   x = 0, y = ryi;
		int   px = 0, py = 2 * rxSq * y, p;
		int   top = yc - y;
		int   bottom = yc + y;
		int   out = 0;

		if (i == 1 || ISDDAEND(r))
		{
			if (i > 1) colors ++;
			out = colors[1].pos - colors[0].pos;
			if (out == 0) out = 1;
			ddaInit(&r, out, colors->rgba[0], colors[1].rgba[0]);
			ddaInit(&g, out, colors->rgba[1], colors[1].rgba[1]);
			ddaInit(&b, out, colors->rgba[2], colors[1].rgba[2]);
			ddaInit(&a, out, colors->rgba[3], colors[1].rgba[3]);
		}

		for (x = 0, d = rgba; x < 10; x ++)
		{
			*d++ = r.y;
			*d++ = g.y;
			*d++ = b.y;
			*d++ = a.y;
		}
		maxlen = d - rgba;

		/* the hardest part is actually handling the clipping correctly :-/ */
		x  = 0;
		s  = &img->bitmap[stride * top];
		d2 = &img->bitmap[stride * bottom] + xc * 4;
		d  = s + xc * 4;
		e  = s + stride;
		p  = rySq - (rxSq * ryi) + (rxSq/4);
		out = 0;
		if (top < 0 || top >= h) out = 1;
		if (bottom < 0 || bottom >= h) out |= 2;
		/* draw ellipse from 90 to 45deg (trigo) */
		while (px < py)
		{
			x++; d += 4; d2 += 4;
			px = px + 2 * rySq;
			if (p >= 0)
			{
				y--; d += stride; e += stride; s += stride; top ++;
				d2 -= stride; bottom --; out = 0;
				if (top < 0 || top >= h) out = 1;
				if (bottom < 0 || bottom >= h) out |= 2;
				py = py - 2 * rxSq;
				p = p + rySq + px  - py;
			}
			else p = p + rySq + px;
			int bytes = (x - scany[y]) * 4;
			if (bytes > 0)
			{
				DATA8 dst  = d - bytes;
				DATA8 dst2 = d2 - bytes;
				int   len  = bytes;
				scany[y] = x;
				/* right part */
				if (dst     < s) len -= s - dst, dst2 += s-dst, dst = s;
				if (dst+len > e) len = e - dst;
				if (len > 0)
				{
					if ((out & 1) == 0) memcpy_repeat(dst,  rgba, len, maxlen);
					if ((out & 2) == 0) memcpy_repeat(dst2, rgba, len, maxlen);
				}
				/* left part */
				len = bytes;
				dst = d - 2 * x * 4;
				dst2 = d2 - 2 * x * 4;
				if (dst     < s) len -= s - dst, dst2 += s-dst, dst = s;
				if (dst+len > e) len = e - dst;
				if (len > 0)
				{
					if ((out & 1) == 0) memcpy_repeat(dst,  rgba, len, maxlen);
					if ((out & 2) == 0) memcpy_repeat(dst2, rgba, len, maxlen);
				}
			}
		}
		/* this line can overflow very quickly with 32bit integer... */
		p = rySq*(x+0.5)*(x+0.5) + rxSq*(y-1.0)*(y-1) - (double) rxSq*rySq;
		/* draw ellipse from 45 to 0deg (trigo) */
		while (y > 0)
		{
			y--; d += stride; e += stride; s += stride; top ++;
			d2 -= stride; bottom --; out = 0;
			if (top < 0 || top >= h) out = 1;
			if (bottom < 0 || bottom >= h) out |= 2;
			py = py - 2 * rxSq;
			if (p <= 0)
			{
				x++; d += 4; d2 += 4;
				px = px + 2 * rySq;
				p = p + rxSq - py + px;
			}
			else p = p + rxSq - py;
			int bytes = (x - scany[y]) * 4;
			if (bytes > 0)
			{
				DATA8 dst  = d - bytes;
				DATA8 dst2 = d2 - bytes;
				int   len  = bytes;
				scany[y] = x;
				if (dst     < s) len -= s - dst, dst2 += s-dst, dst = s;
				if (dst+len > e) len = e - dst;
				if (len > 0)
				{
					if ((out & 1) == 0) memcpy_repeat(dst,  rgba, len, maxlen);
					if ((out & 2) == 0) memcpy_repeat(dst2, rgba, len, maxlen);
				}
				len = bytes;
				dst = d - 2 * x * 4;
				dst2 = d2 - 2 * x * 4;
				if (dst     < s) len -= s - dst, dst2 += s-dst, dst = s;
				if (dst+len > e) len = e - dst;
				if (len > 0)
				{
					if ((out & 1) == 0) memcpy_repeat(dst,  rgba, len, maxlen);
					if ((out & 2) == 0) memcpy_repeat(dst2, rgba, len, maxlen);
				}
			}
		}
		ddaIter(&r);
		ddaIter(&g);
		ddaIter(&b);
		ddaIter(&a);
	}
	/* fill outside */
	for (i = 0, s = img->bitmap; i < h; i ++, s += stride)
	{
		j = i < yc ? yc - i : i - yc;
		if (j >= 0 && j < ry)
		{
			int right = (xc + scany[j]) * 4;
			int sz = (xc - scany[j]) * 4;
			if (sz > 0) memcpy_repeat(s, rgba, sz, maxlen);
			sz = img->width * 4 - right;
			if (sz > 0) memcpy_repeat(s + right, rgba, sz, maxlen);
		}
		else memcpy_repeat(s, rgba, stride, maxlen);
	}

	if (ry > 1023) free(scany);
	return True;
}

static void gradientGetVector(REAL * v, uint16_t orient, int w, int h)
{
	int ang = orient & 32767;
	float hyp, a, sa;

	if (ang < 16384)
		hyp = tan((16384-ang) * M_PI / 32768) * w / 2 - h/2, a = ang * M_PI / 32768;
	else
		hyp = tan((ang-16384) * M_PI / 32768) * w / 2 - h/2, a = (32768-ang) * M_PI / 32768;

	sa  = sin(a);
	hyp *= sa;
	float offx = cos(a) * hyp;
	float offy = sa * hyp;

	switch (orient >> 14) {
	case 0: v[0] = offx, v[1] = h+offy;   v[2] = w-offx, v[3] = -offy;  break; // [0-90]
	case 1: v[0] = offx, v[1] = -offy;    v[2] = w-offx, v[3] = h+offy; break;  // [90-180]
	case 2: v[0] = w-offx, v[1] = -offy;  v[2] = offx, v[3] = h+offy;   break; // [180-270]
	case 3: v[0] = w-offx, v[1] = h+offy; v[2] = offx, v[3] = -offy;    break; // [270-360]
	}
}

/* See https://developer.mozilla.org/en-US/docs/Web/CSS/linear-gradient for the math */
void gradientGetParam(CSSImage img, Gradient * grad)
{
	int w = img->width;
	int h = img->height;
	int ang = grad->corner;

	img->angle = 0;
	img->stretch = 0;
	memset(img->rect, 0, sizeof img->rect);
	if (ang == 255)
	{
		/* radial gradient */
		img->rect[2] = w;
		img->rect[3] = h;
	}
	else /* linear gradient */
	{
		/* not mentioned anywhere, but that's what browser seems to do. seems more eye-appealing too... */
		if ((ang & (~ang + 1)) != ang)
		{
			/*
			 * when drawing a corner gradient limit the drawing to a square and
			 * stretch it to cover the area (ie: background-size: 100% 100%).
			 */
			if (w < h) img->rect[3] = h / (float) w, img->stretch = 2, h = w, img->height = h;
			else       img->rect[3] = w / (float) h, img->stretch = 1, w = h, img->width  = w;
		}

		REAL vector[4];
		REAL normal[4];
		int  imgw = 0, imgh = 0;

		enum {
			Left   = 1,
			Top    = 2,
			Right  = 4,
			Bottom = 8
		};
		switch (grad->corner) {
		case Left:
			img->angle = M_PI;
			img->rect[2] = w;
			img->rect[3] = h;
			imgw = w;
			break;
		case Top:
			img->angle = -M_PI_2;
			img->rect[2] = h;
			img->rect[3] = w;
			imgh = h;
			break;
		case Top|Left:
			img->angle = -(M_PI_4+M_PI_2);
			img->rect[2] = w * M_SQRT2;
			img->rect[0] = w;
			img->rect[1] = h;
			/* no break; */
		case Right:
			imgw = w;
			break;
		case Top|Right:
			img->angle = -M_PI_4;
			img->rect[2] = w * M_SQRT2;
			img->rect[1] = h;
			imgw = w;
			break;
		case Bottom:
			img->angle = M_PI_2;
			img->rect[2] = h;
			img->rect[3] = w;
			imgh = h;
			break;
		case Bottom|Left:
			img->angle = M_PI_4+M_PI_2;
			img->rect[2] = w * M_SQRT2;
			img->rect[3] = w;
			imgw = w;
			break;
		case Bottom|Right:
			img->angle = M_PI_4;
			img->rect[2] = w * M_SQRT2;
			imgw = w;
			break;
		default: /* use angle */
			gradientGetVector(vector, grad->orient, w, h);
			gradientGetVector(normal, grad->orient + 16384, w, h);

			/* gradient orientation in CSS are shifted by -90 from trigo : 0 in CSS gradient is at -90 on trigo circle (assuming top down Y axis) */
			img->angle = (grad->orient-16384) * 2*M_PI / 65536;
			img->rect[0] = vector[0] + (normal[0] - w/2);
			img->rect[1] = vector[1] + (normal[1] - h/2);
			normal[3] -= normal[1];
			normal[2] -= normal[0];
			img->rect[3] = sqrt(normal[2] * normal[2] + normal[3] * normal[3]);
			normal[2]  = vector[2] - vector[0];
			normal[3]  = vector[3] - vector[1];
			img->rect[2] = sqrt(normal[2] * normal[2] + normal[3] * normal[3]);

			imgw = vector[0] - vector[2];
			imgh = vector[1] - vector[3];
		}
		img->width  = roundf(sqrt(imgw*imgw + imgh*imgh));
		img->height = 1;
	}
}
