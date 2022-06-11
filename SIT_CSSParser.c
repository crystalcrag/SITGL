/*
 * SIT_CSSParser.c : parse CSS stylesheet and gather styles into a flat list.
 *
 * written by T.Pierron, may 2020.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <malloc.h>
#include "SIT_P.h"
#include "SIT_CSSParser.h"

static char spaces[] = {0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};

/* some pre-defined inline tags: everything else must be provided by user */
static char defCSS[] =
"b, strong			{ font-weight: bolder }\n"
"i, cite, em		{ font-style: italic }\n"
"pre, tt, code		{ font-family: monospace }\n"
"big				{ font-size: 1.3em }\n"
"small, sub, sup	{ font-size: .83em }\n"
"sub				{ vertical-align: sub }\n"
"sup				{ vertical-align: super }\n"
"s, strike, del		{ text-decoration: line-through }\n"
"::link				{ color: blue }\n"
"::link:hover, u	{ text-decoration: underline }\n"
"::link:active		{ text-decoration: underline; color: red }";

/* only \t, \n, \r, \f and ' ' are considered spaces in CSS */
int cssIsSpace(int chr)
{
	return chr < DIM(spaces) ? spaces[chr] : 0;
}

STRPTR skipspace(STRPTR str)
{
	while (*str)
	{
		if (*str == '/' && str[1] == '*')
		{
			/* skip comments */
			for (str += 2; *str && ! (*str == '*' && str[1] == '/'); str ++);
			if (*str) str += 2;
		}
		uint8_t chr = *(DATA8)str;
		if (chr > DIM(spaces) || ! spaces[chr]) return str;
		str ++;
	}
	return str;
}

static int cssIsNL(STRPTR text)
{
	if (*text == '\r')
		return 1 + (text[1] == '\n');
	else if (*text == '\n')
		return 1;
	else
		return 0;
}

/* strings needs to be parsed carefully because they may contain special characters */
static STRPTR cssSkipString(STRPTR p)
{
	TEXT sep = *p, n;

	for (p ++; *p && *p != sep; p ++) {
		/* cannot span multiple lines - need to be escaped */
		if ((n = cssIsNL(p))) return p + n;
		if (*p == '\\' && p[1]) p++;
	}

	return *p == sep ? p + 1 : p;
}

/* skip bogus CSS declaration as specified is CSS2.1 - 4.2 */
static STRPTR cssSkipBogus(STRPTR fmt, TEXT stopAt)
{
	STRPTR p;
	TEXT   stack[128];
	int    count;

	for (p = fmt, count = 0; *p; p ++)
	{
		if (count == 0 && *p == stopAt) return p;
		switch (*p) {
		case '{': stack[count++] = '}'; break; /* bracket: must parse until matching one is found */
		case '(': stack[count++] = ')'; break;
		case '[': stack[count++] = ']'; break;
		case '}':
		case ']':
		case ')':
			if (count > 0 && stack[count-1] == *p)
				count --;
			if (count == 0 && *p == '}') return p + 1;
			break;
		case '\"': /* string */
		case '\'':
			p = cssSkipString(p)-1;
			break;
		case '\\': /* escape */
			if (isdigit(p[1]))
			{
				strtol(p, &p, 10);
				if (cssIsSpace(*p)) p = skipspace(p);
				p --;
			}
			else if (p[1])
			{
				p ++;
			}
			break;
		case '/': /* comment */
			if (p[1] == '*') {
				p = skipspace(p)-1;
				break;
			}
			// no break;
		default:
			if (count == 0 && *p == ';')
				return p;
		}
	}
	return p;
}

/* convert unicode code point to utf-8 */
int CP2UTF8(DATA8 dest, int cp)
{
	DATA8 out = dest;
	if (cp <= 0x7F) {
	   *out++ = cp;
	} else if (cp <= 0x7FF) {
	   *out++ = 0xC0 | ((cp >> 6) & 0x1F);
	   *out++ = 0x80 | (cp & 0x3F);
	} else if (cp <= 0xFFFF) {
	   *out++ = 0xE0 | ((cp >> 12) & 0x0F);
	   *out++ = 0x80 | ((cp >> 6) & 0x3F);
	   *out++ = 0x80 | (cp & 0x3F);
	} else if (cp <= 0x10FFFF) {
	   *out++ = 0xF0 | ((cp >> 18) & 0x07);
	   *out++ = 0x80 | ((cp >> 12) & 0x3F);
	   *out++ = 0x80 | ((cp >> 6)  & 0x3F);
	   *out++ = 0x80 | (cp & 0x3F);
	}
	return out - dest;
}

int HexToBin(TEXT c)
{
	if ('0' <= c && c <= '9') return c - '0';
	if ('a' <= c && c <= 'f') return c - 'a' + 10;
	return c - 'A' + 10;
}

static int CharInSet(STRPTR str, STRPTR set)
{
	STRPTR p;
	int nb;

	if (str == NULL) return 0;
	for (p = str, nb = 0; *p; p ++)
	{
		if (strchr(set, *p)) nb ++;
	}
	return nb;
}

/* parse ident and convert unicode characters to UTF-8, return ptr to next token */
DATA8 cssIdent(DATA8 start)
{
	DATA8 s, d;

	/* parse special characters */
	if (*start == '-') start ++;

	for (s = d = start; *s; )
	{
		uint8_t chr = *(DATA8)s;

		if (chr > 127 || isalnum(chr) || strchr("-_\\", chr))
		{
			if (*s == '\\')
			{
				/* unicode characters */
				int cp, i;

				for (start = ++ s, cp = i = 0; isxdigit(*s) && i < 6; cp <<= 4, cp |= HexToBin(*s), s ++);
				/* might be followed by one newline to break character globing */
				if (cp > 0)
				{
					if (*s == '\r' && s[1] == '\n') s += 2;
					else s += cssIsSpace(*s);
					d += CP2UTF8(d, cp);
				}
				else if (start == s)
					*d ++ = *s ++;
			}
			else *d ++ = *s ++;
		}
		else break;
	}
	if (d < s) *d = 0;
	return s;
}

/*
 * Tokenizer : find token within stream. This part should've been handled through lex/flex,
 * though it is simple enough to do it by hand.
 *
 * This function does a little bit more than finding token, it also parses them so that
 * we can handle them more easily after (like strings: escaped characters are unescaped, ...)
 *
 * Returns the token type and end of token in "*end".
 */
int cssTokenizer(STRPTR start, STRPTR * end)
{
	STRPTR p = skipspace(start);

	if (*p == '@')
	{
		for (p ++; *p && isalpha(*p); p ++);
		*p++ = 0;
		*end = skipspace(p);
		return CSST_ATRULE;
	}
	else if (*p == '\"' || *p == '\'')
	{
		/* string */
		TEXT   sep = *start;
		STRPTR d;

		/* parse special characters */
		for (p = d = start + 1; *p && *p != sep; )
		{
			if (*p == '\\')
			{
				/* unicode characters */
				int cp, i;

				for (start = ++ p, cp = i = 0; isxdigit(*p) && i < 6; cp <<= 4, cp |= HexToBin(*p), p ++);
				/* might be followed by one newline to break character globing */
				if (*p == '\r') p ++;
				if (*p == '\n') p ++;
				if (cp > 0)
					d += CP2UTF8(d, cp);
				else if (start == p)
					*d ++ = *p ++;
			}
			else *d ++ = *p ++;
		}
		*d = 0;
		*end = skipspace(p + 1);
		return CSST_STRING;
	}
	else if (*p == '<' && strncmp(p + 1, "!--", 3) == 0)
	{
		/* HTML comment usually starting inside <style>, simply ignore */
		*end = p + 4;
		return CSST_SPACES;
	}
	else if (*p == '-' && p[1] == '-' && p[2] == '>')
	{
		*end = p + 3;
		return CSST_SPACES;
	}
	else if (*p == '}')
	{
		*end = skipspace(p + 1);
		return CSST_SPACES;
	}
	else if (*p == '{')
	{
		/* opening block without rule: skip whole block */
		*end = cssSkipBogus(p, 0);
		return CSST_UNKNOWN;
	}
	else
	{
		/* supposed to be a selector at this point, if not selector parsing will return an error */
		return CSST_SELECTOR;
	}
}

/* remove comments and separate tokens with at most one ASCII space */
static void cssNormalizeSpace(DATA8 start)
{
	DATA8 p, end;
	/* !cssIsSpace(*start) must be true */
	for (p = start; *p; p++)
	{
		if (cssIsSpace(*p) || (p[0] == '/' && p[1] == '*'))
		{
			end = skipspace(p);
			if (p > start && p[-1] != ' ')
				*p++ = ' ';

			if (end > p) strcpy(p, end);
			if (*p == 0) break;
		}
	}
	/* remove trailing space */
	if (p > start && p[-1] == ' ') p[-1] = 0;
}

/* get to end of url() token */
static STRPTR cssSkipURL(STRPTR start)
{
	STRPTR p;
	for (p = start + 4; *p && *p != ')' && *p != '}'; )
	{
		if (*p == '\"' || *p == '\'') p = cssSkipString(p);
		else p ++;
	}
	return p;
}

/* parse nth-child pseudo selector arguments */
Bool cssParseNthChild(STRPTR fmt, int * ra, int * rb)
{
	int a = 1, b = 0;
	fmt = skipspace(fmt);
	if (strncasecmp(fmt, "odd", 3) == 0)
		a = 2, b = 1, fmt += 3;
	else if (strncasecmp(fmt, "even", 4) == 0)
		a = 2, fmt += 4;
	else /* parse an+b */
	{
		if (*fmt == '-') a = -1, fmt ++; else
		if (*fmt == '+') fmt ++;
		if (isdigit(*fmt))
			a *= strtoul(fmt, &fmt, 10);
		if (*fmt == 'n')
		{
			fmt = skipspace(fmt+1);
			if (*fmt == '-') b = -1, fmt = skipspace(fmt+1); else
			if (*fmt == '+') b =  1, fmt = skipspace(fmt+1);
			if (isdigit(*fmt))
				b *= strtol(fmt, &fmt, 10);
		}
		else b = a, a = 0;
	}
	fmt = skipspace(fmt);
	if (*fmt != ')') return False;
	*ra = a; *rb = b;
	return True;
}

/* parse a CSS color */
Bool cssParseColor(STRPTR * value, CSSColor * color)
{
	STRPTR p = *value;
	int    r, g, b, alpha, n;

	alpha = 255;
	if (*p == '#') /* #xxx or #xxxxxx */
	{
		for (n = 0, p ++; isxdigit(p[n]); n ++);

		switch (n) {
		case 1: /* gray: typical value: #0 for black of #f for white */
			r = HexToBin(p[0]); r |= r << 4;
			g = b = r;
			break;
		case 4: /* #RGBA */
			alpha = HexToBin(p[3]);
			alpha |= alpha << 4;
			// no break;
		case 3: /* #RGB */
			r = HexToBin(p[0]); r |= r << 4;
			g = HexToBin(p[1]); g |= g << 4;
			b = HexToBin(p[2]); b |= b << 4;
			break;
		case 8: /* #RRGGBBAA */
			alpha = strtoul(p + 6, NULL, 16);
			// no break;
		case 6: /* #RRGGBB */
			sscanf(p, "%2x%2x%2x", &r, &g, &b);
			break;
		default:
			return False;
		}
		p += n;
	}
	else if (strncasecmp(p, "rgb(", 4) == 0) /* rgb(r,g,b) */
	{
		n = -1;
		if (sscanf(p+4, "%d, %d, %d)%n", &r, &g, &b, &n) < 3)
		{
			/* try with percentage */
			float rf, gf, bf;
			if (sscanf(p+4, "%f%%, %f%%, %f%%)%n", &rf, &gf, &bf, &n) >= 3)
			{
				r = rf * 255 / 100;
				g = gf * 255 / 100;
				b = bf * 255 / 100;
			}
		}
		if (n < 0) return False;
		p += 4 + n;
	}
	else if (strncasecmp(p, "rgba(", 5) == 0) /* rgba(r,g,b,a) */
	{
		float a;
		n = -1;
		if (p[5] == '#' || isalpha(p[5]))
		{
			/* extension: want to specify RGB in hex or using a color name, alpha in float */
			*value = p+5;
			cssParseColor(value, color);
			p = *value;
			r = color->rgba[0];
			g = color->rgba[1];
			b = color->rgba[2];
			if (*p == ',')
			{
				if (sscanf(p + 1, "%f%n", &a, &n) >= 1)
					p += n+1, alpha = a * 255;
				else
					return False;
			}
			if (*p == ')') p ++;
			else return False;
		}
		else if (sscanf(p+5, "%d, %d, %d, %f)%n", &r, &g, &b, &a, &n) >= 4 && n > 0)
		{
			alpha = a * 255;
			p += 5 + n;
		}
		else return False;

		/* if alpha is 0, rgb values don't matter */
		if (alpha <= 0)
		{
			alpha = 0;
			/* distinguish from unset state */
			if (r+g+b == 0) r = 1;
		}
		else if (alpha > 255)
		{
			alpha = 255;
		}
	}
	else if (strncasecmp(p, "hsl(", 4) == 0)
	{
		int hsv[3];
		if (sscanf(p+4, "%d, %d%%, %d%%)%n", hsv, hsv+1, hsv+2, &n) >= 3)
		{
			/* normalized in range 255-255-255 from range 360-100-100 */
			int i, f, m, N, v;
			if (hsv[1] < 0) hsv[1] = 0;
			if (hsv[1] > 100) hsv[1] = 100;
			if (hsv[2] < 0) hsv[2] = 0;
			if (hsv[2] > 100) hsv[2] = 100;
			hsv[0] %= 360;

			i = hsv[0] / 60;
			v = hsv[2];
			f = ((hsv[0] - 60*i) << 8) / 60;
			if ((i&1) == 0) f = 256 - f; /* if i is even */
			m = v * (100 - hsv[1]) * 255 / 10000;
			N = v * (100 - (hsv[1] * f >> 8)) * 255 / 10000;
			v = v * 255 / 100;
			switch (i) {
			case 6:
			case 0: r = v; g = N; b = m; break;
			case 1: r = N; g = v; b = m; break;
			case 2: r = m; g = v; b = N; break;
			case 3: r = m; g = N; b = v; break;
			case 4: r = N; g = m; b = v; break;
			case 5: r = v; g = m; b = N;
			}
		}
		else return False;
		p += 4+n;
	}
	else if (strncasecmp(p, "gray(", 5) == 0)
	{
		/* extension: black only color, range [0-1], ie: 0 = black, 1 = white */
		float k;
		if (sscanf(p+5, "%f)%n", &k, &n) >= 1)
		{
			p += 5+n;
			if (k < 0) k = 0;
			if (k > 1) k = 1;
			r = g = b = k*255;
		}
		else return False;
	}
	else if (strncasecmp(p, "selfore", 7) == 0)
	{
		uint8_t rgba[4];
		GetSelectionColor(rgba, NULL);
		r = rgba[0];
		g = rgba[1];
		b = rgba[2];
		alpha = 255;
		p += 7;
	}
	else if (strncasecmp(p, "selback", 7) == 0)
	{
		uint8_t rgba[4];
		GetSelectionColor(NULL, rgba);
		r = rgba[0];
		g = rgba[1];
		b = rgba[2];
		alpha = 255;
		p += 7;
	}
	else /* named color */
	{
		STRPTR next, named;
		ULONG  val;
		for (next = p; *next && isalnum(*next); next ++);

		if (*next)
		{
			r = next - p;
			named = memcpy(alloca(r+1), p, r);
			named[r] = 0;
		}
		else named = (STRPTR) p;
		val = cssColorSearch(named);
		p = next;

		if (val == (ULONG) -1)
		{
			/* distinguish from unset state (alpha is 0 therefore we can use whatever for RGB) */
			memcpy(color->rgba, "\1\0\0", 4);
			*value = p;
			return True;
		}
		else if (val)
		{
			b = val & 0xff; val >>= 8;
			g = val & 0xff; val >>= 8;
			r = val & 0xff;
		}
		else return False;
	}
	if (r < 0) r = 0; if (r > 255) r = 255;
	if (g < 0) g = 0; if (g > 255) g = 255;
	if (b < 0) b = 0; if (b > 255) b = 255;
	*value = p;
	color->rgba[0] = r;
	color->rgba[1] = g;
	color->rgba[2] = b;
	color->rgba[3] = alpha;
	return True;
}

/* transform floating point into a fixed-point 32bit tagged number */
ULONG cssFromUnit(int unit, REAL val)
{
	int rel = 0;
	switch (unit) {
	case Pixel:      break;
	case Points:     break;
	case Centimeter: val *= 72 / 2.54f; break;
	case Millimeter: val *= 72 / 25.4f; break;
	case Inch:       val *= 72; break;
	case Em:         rel  = 1; break;
	case VpWidth:    rel  = 3; break;
	case VpHeight:   rel  = 3; break;
	case Percentage: rel  = 2;
	}

	/* effective range is [-32767, 32767] */
	if (val >= 0) return rel | PT(val);
	val = -val;
	return (1UL<<31) | rel | PT(val);
}

/* parse a CSS position with its unit */
Bool cssParsePos(STRPTR * value, ULONG * pos, int withUnit)
{
	STRPTR cur = *value, init = cur;
	double num = strtod(cur, value);

	if (cur < *value)
	{
		int len, unit;

		/* length of the suffix */
		for (len = 0, cur = *value; *cur && ! cssIsSpace(*cur) && !strchr("|;,)}", *cur); cur ++, len ++);
		unit = FindInList("px,pt,cm,mm,in,em,vw,vh,%", *value, len);

		switch (unit) {
		case -1:
			if (len > 0) return False;
			if (withUnit <= 0)
			{
				if (num < 0) return False;
				if (withUnit < 0) *pos = cssFromUnit(Points, num);
				else *pos = num;
				return True;
			}
			/* "0" is the only value that can be specified without unit */
			*pos = 0;
			if (len == 0 && fabs(num) < 0.00001)
				return True;
			break;
		case Percentage:
			len = 1; // no break;
		default:
			*pos = cssFromUnit(unit, num);
			*value += len;
			return True;
		}
	}
	*value = init;
	return False;
}

Bool cssParseNum(STRPTR * value, APTR num, int type)
{
	switch (type) {
	case 4: *(ULONG *)num = strtoul(*value, value, 10); break;
	case 1: *(DATA8)  num = strtoul(*value, value, 10); break;
	case 2: *(REAL *) num = strtod(*value, value);
	}
	int ch = **value;
	return ch == 0 || cssIsSpace(ch);
}

/* parse angle in deg, rad, grad or turn */
Bool cssParseAngle(STRPTR fmt, STRPTR * pend, uint16_t * mem)
{
	double angle = strtod(fmt, pend);

	if (*pend > fmt)
	{
		/* parse unit */
		double mod = 0;
		fmt = *pend;
		if (strncasecmp(fmt, "deg", 3) == 0)  fmt += 3, mod = 360; else
		if (strncasecmp(fmt, "rad", 3) == 0)  fmt += 3, mod = 2 * M_PI; else
		if (strncasecmp(fmt, "grad", 4) == 0) fmt += 4, mod = 400; else
		if (strncasecmp(fmt, "turn", 4) == 0) fmt += 4, mod = 1;
		if (mod > 0)
		{
			if (angle < 0 || angle >= mod)
				angle -= floor(angle / mod) * mod;

			/* normalize angle in the range [0-65535] */
			*mem = angle / mod * 65536;
		}
		else if (angle != 0)
		{
			/* only 0 is allowed to not have a unit */
			return False;
		}
		else *mem = 0;
		*pend = fmt;
		return True;
	}
	return False;
}

/* check that <fmt> is a valid URI */
Bool cssParseURI(STRPTR fmt, STRPTR * pend, STRPTR * mem)
{
	if (strncasecmp(fmt, "url(", 4) == 0)
	{
		STRPTR end;
		STRPTR start = fmt + 4;
		STRPTR p = skipspace(start);

		*mem = fmt;

		if (*p != '\"' && *p != '\'')
		{
			for (end = p; *end && *end != ')'; end ++);
			if (*end == 0) return False;
			end++;
		}
		else
		{
			TEXT sep = *p;
			for (p ++; *p && *p != sep; )
			{
				if (*p == '\\')
				{
					/* unicode characters */
					int i;
					for (start = ++ p, i = 0; isxdigit(*p) && i < 6; i ++, p ++);
					if (*p == '\r') p ++;
					if (*p == '\n') p ++;
					if (start == p) p ++;
				}
				else p ++;
			}
			if (*p == 0) return False;
			end = skipspace(p+1);
			if (*end == ')') end ++;
			else return False;
		}

		if (pend) *pend = end;
		return True;
	}
	/* extension: use NVG image id as source (typical use case: reuse OpenGL texture) */
	if (strncasecmp(fmt, "id(", 3) == 0)
	{
		*mem = fmt;
		int id = strtoul(fmt + 3, &fmt, 10);
		if (id == 0 || *fmt != ')') return False;
		if (pend) *pend = fmt + 1;
		return True;
	}
	return False;
}

/* parse linear-gradient and repeating-linear-gradient function. fmt will NOT be modified */
Bool cssParseGradient(STRPTR fmt, STRPTR * pend, Gradient * bg)
{
	bg->flags = 0;
	/* we only care about W3C spec here, not the countless proprietary variations */
	if (strncasecmp(fmt, "repeating-", 10) == 0)
		bg->flags = GRADIENT_REPEAT, fmt += 10;
	if (strncasecmp(fmt, "linear-gradient(", 16) == 0)
		fmt += 16;
	else if (strncasecmp(fmt, "radial-gradient(", 16) == 0)
		fmt += 16, bg->flags = GRADIENT_RADIAL;
	else return False;

	/* From MDN: linear-gradient([ <angle> | to <side-or-corner> ,]? <color-stop> [, <color-stop>]+) */
	fmt = skipspace(fmt);

	if (bg->flags == GRADIENT_RADIAL)
	{
		/* from MDN:
		 * radial-gradient(
		 *   [ [ circle || <length> ]                         [ at <position> ]? , |
		 *     [ ellipse || [ <length> | <percentage> ]{2} ]  [ at <position> ]? , |
		 *     [ [ circle | ellipse ] || <extent-keyword> ]   [ at <position> ]? , |
		 *     at <position> ,
		 *   ]?
		 *  <color-stop> [ , <color-stop> ]+)
		 */
		STRPTR init = strchr(fmt,',');
		STRPTR desc;
		int    dist, shape, sep;
		if (init == NULL) return False;
		*init = 0; desc = STRDUPA(fmt); *init++ = ',';

		sep = 0;
		init = cssIdent(desc);
		if (*init)
		{
			*init = 0;
			init = skipspace(init+1);
		}
		shape = FindInList("circle,ellipse", desc, 0);
		if (shape >= 0)
		{
			fmt += init - desc;
			sep = 1;
			init = cssIdent(desc = init);
			if (*init)
			{
				*init = 0;
				init = skipspace(init+1);
			}
		}
		else shape = 0; /* circle */

		dist = FindInList("closest-side,closest-corner,farthest-side,farthest-corner", desc, 0);
		if (dist < 0) dist = 3;  /* farthest-corner */
		else fmt += init - desc, sep = 1;

		bg->radial.ry = 0xff;
		bg->radial.rx = 3 | (shape<<2) | (dist << 4);
		bg->radial.cy = bg->radial.cx = cssFromUnit(Percentage, 50);

		if (strncasecmp(fmt, "at", 2) == 0)
		{
			fmt = skipspace(fmt+2); sep = 1;
			if (strncasecmp(fmt, "center", 6))
			{
				if (! cssParsePos(&fmt, &bg->radial.cx, True)) return False;
				if (! cssParsePos(&fmt, &bg->radial.cy, True)) bg->radial.cy = bg->radial.cx;
			}
			else fmt += 6;
			fmt = skipspace(fmt);
		}
		if (! sep) goto skipcheck;
	}
	/* parse angle or direction */
	else if (strncasecmp(fmt, "to", 2) == 0 && cssIsSpace(fmt[2]))
	{
		static uint8_t valid_dir[] = {0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0};
		int flag = 0;
		for (fmt = skipspace(fmt+2); *fmt && *fmt != ','; )
		{
			STRPTR next;
			for (next = fmt; *next && *next != ',' && ! cssIsSpace(*next); next ++);
			int res = FindInList("left,top,right,bottom", fmt, next - fmt);
			if (res < 0) return False;
			flag |= 1 << res;
			fmt = skipspace(next);
		}
		if (valid_dir[flag] == 0) return False; /* imposible direction, like "to left right" */
		bg->flags |= flag;
	}
	else if (cssParseAngle(skipspace(fmt), &fmt, &bg->orient))
	{
		/* special cases: use optimized endpoints */
		switch (bg->orient) {
		case     0: bg->flags |= 2; break;
		case 16384: bg->flags |= 4; break; /* 90deg */
		case 32768: bg->flags |= 8; break; /* 180deg */
		case 49152: bg->flags |= 1;        /* 270deg */
		}
	}
	/* nothing specified: default to "to bottom" */
	else { bg->flags |= 8; goto skipcheck; }

	ColorStop grad;
	fmt = skipspace(fmt);
	if (*fmt != ',') return False;
	fmt = skipspace(fmt + 1);
	skipcheck:
	grad = bg->colors;
	while (*fmt)
	{
		CSSColor col;
		if (! cssParseColor(&fmt, &col))
			return False;
		fmt = skipspace(fmt);
		if (memcmp(col.rgba, "\1\0\0", 4) == 0)
			col.val = 0;
		memcpy(grad->rgba, col.rgba, 4);
		if (*fmt != ',' && *fmt != ')')
		{
			if (! cssParsePos(&fmt, &grad->pos, 2))
				return False;
		}
		else grad->pos = 3; /* defered */
		grad ++;
		fmt = skipspace(fmt);
		bg->colorStop ++;

		if (*fmt == ')') { fmt ++; break; }
		if (*fmt == ',') fmt ++;
		fmt = skipspace(fmt);
	}
	*pend = fmt;
	return True;
}

/* theme will be parsed into a single buffer */
DATA8 cssAllocThemeBytes(int size, APTR data)
{
	if (sit.themeSize + size > sit.themeMax)
	{
		sit.themeMax = (sit.themeSize + size + 255) & ~255;
		DATA8 mem = realloc(sit.theme, sit.themeMax);
		if (! mem) return NULL;
		sit.theme = mem;
	}
	int pos = sit.themeSize;
	sit.themeSize += size;
	if (data) memcpy(sit.theme + pos, data, size);
	return sit.theme + pos;
}

/*
 * parse CSS block attributes
 * transform them into table of key/value pairs (NULL-terminated)
 */
static STRPTR cssParseStyles(STRPTR start, int * count)
{
	STRPTR * attr = NULL;
	STRPTR   fmt = skipspace(start);
	int      nb = 0;

	while (*fmt && *fmt != '}')
	{
		if (attr == NULL)
			attr = (STRPTR *) cssAllocThemeBytes(2 * sizeof (STRPTR), NULL);
		attr[0] = fmt;
		fmt = cssIdent(fmt);

		if (*fmt == ':')
		{
			/* attribute name, followed by attribute value */
			*fmt++ = 0;
			start = fmt = skipspace(fmt);
			while (*fmt && *fmt != '}' && *fmt != ';')
			{
				TEXT chr = *fmt;
				if (chr == '\\' && fmt[1]) fmt += 2; else
				if (chr == '/' && fmt[1] == '*') fmt = skipspace(fmt); else
				if (strncasecmp(fmt, "url(", 4) == 0) fmt = cssSkipURL(fmt); else
				if (strncasecmp(fmt, "linear-gradient(", 16) == 0 ||
				    strncasecmp(fmt, "repeating-linear-gradient(", 26) == 0)
				{
					struct Gradient_t dummy = {};
					if (!cssParseGradient(fmt, &fmt, &dummy))
						goto error;
				}
				else if (chr == '\"' || chr == '\'')
				{
					fmt = cssSkipString(fmt);
					/* unterminated literal not at end of stream */
					if (fmt[-1] != chr && *fmt)
						goto error;
				}
				else fmt ++;
			}
			if (start < fmt)
			{
				StrToLower(attr[0], -1);
				attr[1] = start;
				nb ++;
				if (*fmt == '}') { *fmt = 0; cssNormalizeSpace(start); *fmt = '}'; break; }
				if (*fmt == ';') { *fmt++ = 0; }
				cssNormalizeSpace(start);
				/* if there is an error, we'll reuse our previous allocation */
				attr = NULL;
			}
			else goto error;
		}
		else
		{
			error:
			fmt = cssSkipBogus(fmt, '}');
			if (*fmt == ';') *fmt++ = 0;
		}
		fmt = skipspace(fmt);
	}
	if (*fmt == '}') *fmt ++ = 0;
	*count = nb;
	return fmt;
}

void cssParseInlineStyles(SIT_Widget node, STRPTR styles)
{
	if (node->inlineStyles)
		free(node->inlineStyles), node->inlineStyles = NULL;
	if (IsDef(styles))
	{
		int length = strlen(styles);
		int commit = sit.themeSize;
		int count;

		/* alloc everything in one block */
		styles = STRDUPA(styles);
		cssParseStyles(styles, &count);
		if (count > 0)
		{
			count *= 2;
			node->inlineStyles = malloc((count + 1) * sizeof (STRPTR) + length + 1);
			STRPTR dup = (STRPTR) (node->inlineStyles + count + 1);
			STRPTR * attr;
			memcpy(dup, styles, length + 1);
			memcpy(node->inlineStyles, sit.theme + commit, count * sizeof (STRPTR));
			node->inlineStyles[count] = NULL;
			sit.themeSize = commit;
			commit = dup - styles;
			/* inlineStyles still points to a stack allocated mem block at this point */
			for (attr = node->inlineStyles; count > 0; attr[0] += commit, count --, attr ++);
		}
	}
}

/* transform selector string into something more easily parsable */
static CSSRule cssCompileSelector(STRPTR start, int prevRule)
{
	STRPTR p, sel, prev;
	CSSSel compil = alloca(sizeof *compil * (CharInSet(start, ">:+[].# \t") + 5));
	ULONG  specif = 0;
	int    length = 0;
	int    rule   = CSSR_NONE;
	int    trans  = CSST_CURRENT;
	int    next   = 0;

	for (p = prev = start; *p; )
	{
		switch (*p) {
		case '\t': case '\n':
		case '\f': case '\r': case ' ':
			*p = 0;
			p = skipspace(p+1);
			if (trans == CSST_CURRENT)
				trans = CSST_ANY;
			break;

		case '+':
			trans = CSST_SIBLING; p ++;
			break;

		case '>':
			trans = CSST_CHILD; p ++;
			break;

		case '[': rule ++; // no break;
		case '#': rule ++; // no break;
		case '.': rule ++; // no break;
		case ':': rule ++; // no break;
			*p = 0;
			prev = p + 1;
			rule ++;
			/* pseudo elements (::) have different specificity than pseudo-class (:) */
			if (rule == CSSR_PSEUDO && prev[0] == ':')
				prev ++, specif ++;
			else
				specif += (rule == CSSR_ID ? 1<<16 : 1<<8);

			if (rule == CSSR_ATTR)
			{
				STRPTR end = NULL;
				prev = skipspace(p+1);
				sel = cssIdent(prev);
				p = skipspace(sel);
				if (strncmp(p, "=",  1) == 0) rule = CSSR_ATTREQV,  p ++; else
				if (strncmp(p, "~=", 2) == 0) rule = CSSR_ATTRLIST, p += 2; else
				if (strncmp(p, "|=", 2) == 0) rule = CSSR_ATTRDASH, p += 2;
				p = skipspace(p);

				if (rule > CSSR_ATTR && cssTokenizer(p, &end) == CSST_STRING)
				{
					strcpy(sel + 1, p + 1);
					p = end;
				} else if (*p != ']') {
					if (sel + 1 != p) strcpy(sel + 1, p);
					p = cssIdent(sel+1);
				}
				while (*p && *p != ']') p ++;
				if (*p) *p ++ = 0;
				sel[0] = 0;
			}
			else p = cssIdent(prev);
			break;

		case '*': /* processed like a tag name */
			prev = "*";
			p ++;
			rule = CSSR_TAG;
			specif ++;
			break;

		default: /* tag name */
			specif ++;
			prev = p;
			p = cssIdent(p);
			if (p == prev) return NULL;
			rule = CSSR_TAG;
		}

		if (rule > 0)
		{
			compil[length].item  = prev;
			compil[length].type  = rule;
			compil[length].trans = next > 0 ? (next&0xff)-1 : trans;
			rule = 0;
			next >>= 8;
			trans = CSST_CURRENT;
 			length ++;
		}
	}

	struct CSSRule_t cssr = {.nbsel = length, .specif = specif};

	if (prevRule >= 0)
	{
		CSSRule previous = (CSSRule) (sit.theme + prevRule);
		previous->next = sit.themeSize;
	}
	rule = sit.themeSize;
	cssAllocThemeBytes(sizeof cssr, &cssr);
	cssAllocThemeBytes(length * sizeof *compil, compil);
	return (CSSRule) (sit.theme + rule);
}

/* parse a CSS stylesheet */
static void cssParseString(STRPTR rules, STRPTR rel)
{
	STRPTR p, eof;

	for (p = rules; *p; p = eof)
	{
		p = skipspace(p);
		if (cssTokenizer(p, &eof) != CSST_SELECTOR) continue;

		/* beginning of a rule (note: eof is not set at this point) */
		CSSRule rule;
		STRPTR  start;
		Bool    end, fail;
		int     commit = sit.themeSize;

		/* parse selector lines */
		for (start = p, fail = end = False; *p && ! end; )
		{
			if (*p == ',' || *p == '{')
			{
				end = (*p == '{'); *p++ = 0;
				rule = cssCompileSelector(start, sit.lastRule);
				start = skipspace(p);
				if (rule == NULL)
				{
					/* CSS2.1 - 4.1.7 mandates if one selector fails, the whole declaration must be dropped */
					/* (including other selectors and the attribute block) */
					if (end) *--p = '{'; /* let cssSkipBogus() do its job */
					fail = True;
					sit.themeSize = commit;
				}
				else if (fail)
				{
					/* we have to compile the selector but discard it anyway because of previous errors */
					sit.themeSize = commit;
				}
				else sit.lastRule = (DATA8) rule - sit.theme;
			}
			else if (*p == '\"' || *p == '\'')
			{
				p = cssSkipString(p);
			}
			else p ++;
		}

		if (! fail)
		{
			int count, offset = sit.themeSize;
			p = cssParseStyles(p, &count);
			if (count > 0)
			{
				int first = commit;
				do {
					rule = (CSSRule) (sit.theme + first);
					rule->styles = offset;
					rule->nbstyles = count;
					first = rule->next;
				} while (first > 0);
			}
			else sit.themeSize = commit; /* empty declaration: nuke all selectors */
			eof = p;
		}
		else eof = cssSkipBogus(p, 0);
	}
}

/* initiate parsing of external CSS file */
Bool cssParse(STRPTR css, int isPath)
{
	/* parse an external stylesheet */
	STRPTR buf = NULL;
	sit.relPath[0] = 0;
	if (isPath)
	{
		FILE * in = fopen(css, "rb");

		if (in)
		{
			int sz;
			fseek(in, 0, SEEK_END); sz  = ftell(in);
			fseek(in, 0, SEEK_SET); buf = malloc(sz + 1);
			fread(buf, sz, 1, in);  buf[sz] = 0;
			fclose(in);

			/* pre-def styles need to be parsed before */
			cssParseString(defCSS, NULL);
			cssParseString(buf, css);

			/* make all resources relative to where stylesheet is */
			CopyString(sit.relPath, css, sizeof sit.relPath);
			ParentDir(sit.relPath);
			sit.relPathSz = strlen(sit.relPath);
		}
		else return False;
	}
	else /* inline stylesheet */
	{
		buf = strdup(css);
		cssParseString(defCSS, NULL);
		cssParseString(buf, NULL);
	}
	if (sit.cssFile) free(sit.cssFile);
	sit.cssFile = buf;
	return True;
}

