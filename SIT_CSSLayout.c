/*
 * SIT_CSSLayout.c : compute text nodes position and dimension according to CSS styles; block
 *                   elements position will be computed in SIT_Geometry.c (because CSS positionning SUCKS).
 *
 * Written by T.Pierron, may 2020.
 */

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <malloc.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

/* children might depend on certain parent's width/height. If it changes, layout of children must be redone */
void layoutCheckRel(SIT_Widget node, REAL len, int side)
{
	REAL * rel = &node->layout.format.width + 1 - (side & 1);
	if (*rel == 0)
		*rel = len > 0 ? len : EPSILON/2;
	else if (fabsf(*rel - len) > EPSILON)
		node->layout.flags |= LAYF_SizeChanged;
}

/* convert a CSS fixed-point unit into pt. <side> is CSS_{TOP|LEFT|BOTTOM|RIGHT} */
REAL ToPoints(SIT_Widget parent, SIT_Widget node, ULONG fixed, int side)
{
	uint16_t num  = fixed >> 16;
	int      frac = (fixed & 0xfffc) >> 2;
	uint8_t  pad  = side & (CSS_ADDBORDER|CSS_ADDPAD);
	uint8_t  bdr  = side & (CSS_ADDBORDER);
	REAL     res  = (num&0x7fff) + frac / 16384.;
	REAL     a;

	if (num & 0x8000) res = -res;
	side &= 15;
	switch (fixed & 3) {
	case 0: return res; /* direct value */
	case 1:
		/* em or ex */
		return roundf(res * node->style.font.size);
	case 2:
		/* percentage */
		frac = 1 - (side & 1);
		/* auto if height is not set (CSS2.1 10.5) (width is processed differently) */
		if (parent == NULL) return -EPSILON;
		a = (&parent->layout.pos.width)[frac];
		layoutCheckRel(parent, a, side);
		if (pad) a += (&parent->layout.padding.top)[side&1] + (&parent->layout.padding.top)[(side&1)+2];
		if (bdr) a += (&parent->layout.border.top)[side&1]  + (&parent->layout.border.top)[(side&1)+2];
		return roundf(a * res / 100.0f);
	default: return 0; /* auto */
	}
}

/* size of non-content area (ie: padding+margin+border). <side> can be CSS_{TOP|LEFT|BOTTOM|RIGHT} */
REAL layoutNCSize(SIT_Widget node, int side)
{
	/* cannot change lineHeight using margin, border or padding of inline nodes */
	if ((node->style.flags & CSSF_INLINE) && (side == CSS_TOP || side == CSS_BOTTOM))
		return 0;

	return (&node->layout.padding.top)[side] + (&node->layout.border.top)[side];
}

/* full size, including NC area. <side> can be CSS_WIDTH or CSS_HEIGHT */
REAL layoutSize(SIT_Widget node, int side)
{
	if (side == CSS_WIDTH)
		return layoutNCSize(node, CSS_LEFT) + layoutNCSize(node, CSS_RIGHT) + node->layout.pos.width;
	else
		return layoutNCSize(node, CSS_TOP) + layoutNCSize(node, CSS_BOTTOM) + node->layout.pos.height;
}

/* check if relative units need to be updated */
Bool layoutSizeChanged(SIT_Widget node)
{
	SIT_Widget parent = node->parent;
	REAL       size   = node->layout.format.width;
	if (size > 0 && fabsf(size - parent->layout.pos.width) > EPSILON)
		return True;
	size = node->layout.format.height;
	return size > 0 && fabsf(size - parent->layout.pos.width) > EPSILON;
}

REAL layoutCalcLineHeight(SIT_Widget node)
{
	if (node->style.lineHeight != AUTOVAL)
	{
		REAL lineHeight = ToPoints(NULL, node, node->style.lineHeight & ~3, 0 /* only needed for relative value */);

		/* style.lineHeight is only the minimum required - relative value are relative to font size not parent box */
		switch (node->style.lineHeight & 3) {
		case 1: lineHeight *= node->style.font.size; break; /* em, ex or multiplier */
		case 2: lineHeight *= node->style.font.size * 0.01f; break; /* percentage */
		}
		return lineHeight;
	}
	else return 0;
}

void layoutAdjustBorderRadius(SIT_Widget node)
{
	/* border-radius can only be known once layout has been done */
	REAL   maxmajor = layoutSize(node, CSS_WIDTH);
	REAL   maxminor = layoutSize(node, CSS_HEIGHT);
	REAL * major;
	REAL * minor;
	int    i, none;

	for (i = none = 0, major = &node->layout.majorRadius.top, minor = &node->layout.minorRadius.top; i < 4;
		 i ++,  major ++, minor ++)
	{
		*major = ToPoints(node, node, (&node->style.majorRadius.top)[i], CSS_LEFT | CSS_ADDBORDER);
		*minor = ToPoints(node, node, (&node->style.minorRadius.top)[i], CSS_TOP  | CSS_ADDBORDER);
		if (*major < EPSILON || *minor < EPSILON) *major = *minor = 0, none ++;
	}
	if (none < 4)
	{
		/* check for overlap: CSS backgrounds and borders level3, section 5.5 */
		static uint8_t sides[] = {0,1,2,3,4,7,5,6};
		DATA8 p;
		REAL  fact = 0;
		major = &node->layout.majorRadius.top;
		for (i = 0, p = sides; i < DIM(sides); i += 2, p += 2)
		{
			REAL val = major[p[0]] + major[p[1]];
			REAL max = i < 4 ? maxmajor : maxminor;
			if (val <= max) continue; val = max/val;
			if (val < fact || fact == 0)
				fact = val;
		}
		/* all radius need to be scaled by the same factor */
		if (fact > 0)
			for (i = 0; i < 8; major[i] *= fact, i ++);
	}
	else node->layout.flags |= LAYF_NoRoundBorder;
}

static WordWrap layoutGetWordWrap(SIT_Widget node, int nthLast)
{
	int      old = node->layout.wordwrap.count;
	WordWrap word = vector_nth(&node->layout.wordwrap, nthLast + old);
	return word;
}

/* what's considered spaces in HTML */
static int htmlIsSpace(STRPTR text)
{
	TEXT chr = *text;
	if (chr == ' ' || chr == '\n'|| chr == '\r' || chr == '\t') return 1;
	return 0;
}

/* what's considered new lines */
static int htmlIsNL(STRPTR text)
{
	if (*text == '\r')
		return 1 + (text[1] == '\n');
	else if (*text == '\n')
		return 1;
	else
		return 0;
}

#ifdef UTF8ToUTF16
#undef UTF8ToUTF16
#endif

/* capitalize/upper/lower text on the fly */
static void layoutTransformText(WordWrap word, int transform)
{
	int    len = transform == TextTransformCapitalize ? NthChar(word->word, 1) - word->word : word->n;
	STRPTR buf = alloca(len << 1);
	int    sz  = UTF8ToUTF16(buf, len, word->word, len);

	if (transform == TextTransformLowercase)
		StrToLower16(buf, sz);
	else
		StrToUpper16(buf, sz);

	/* XXX overwrite internal buffer XXX cannot change this at runtime */
	UTF16ToUTF8(word->word, len, buf, sz);
}

/* get what is considered as a word according to white-space property */
static STRPTR layoutGetWord(STRPTR str, STRPTR end, int ws, int * spc, int * newline, int * inispc)
{
	STRPTR p;
	int    nb = 0, n, init;
	*newline = 0;
	*inispc  = 0;
	if (ws == WhiteSpacePre)
	{
		for (p = str; p < end && *p != '\t' && *p != '\r' && *p != '\n'; p ++);
		if (*p == '\t')
		{
			while (p < end && (n = htmlIsSpace(p)) > 0)
				p += n, nb += n;
		}
		else if (p < end)
		{
			p += *newline = htmlIsNL(p);
		}
	} else {
		/* skip initial word or hyphen that is not preceded by a digit */
		for (p = str; p < end && htmlIsSpace(p) == 0 && !(*p == '-' && p > str && !isdigit(p[-1])); p ++); init = p == str;
		if (*p == '-') p ++;
		switch (ws) {
		case WhiteSpacePreWrap: init = False; /* spaces must be preserved - no break; */
		case WhiteSpacePreLine:
			for (; p < end && ! (*newline = htmlIsNL(p)) && (n = htmlIsSpace(p)) > 0; p += n, nb += n);
			n = *newline;
			if (n > 0) p += n;
			break;
		default:
			for (; p < end && (n = htmlIsSpace(p)) > 0; p += n, nb += n);
		}
		if (init) *inispc = nb;
	}
	*spc = nb;
	return p;
}

/* compute white space length according to collapse method */
static REAL layoutGetSpaceLen(STRPTR s, int len, int ws, REAL xcur, REAL spclen, int tabchr)
{
	if (ws == WhiteSpacePre || ws == WhiteSpacePreWrap)
	{
		/* we have to also take care of tabs */
		REAL x, tab;
		for (x = xcur, tab = spclen * tabchr; len > 0; len --, s ++) {
			if (*s == '\t') x = tab * ceilf((x+EPSILON) / tab);
			else x += spclen;
		}
		return x - xcur;
	}
	else return len > 0 ? spclen : 0;
}

/* split text into words and compute their length and position */
static void layoutBreakWords(SIT_Widget node, STRPTR text, int length)
{
	REAL   space, fh, bl, x;
	int    spc, nl, inispc;
	char   ws, trans, tab;
	STRPTR p, next, end;

	SIT_Widget offset;
	for (offset = node; offset->type == SIT_HTMLTAG; offset->layout.flags |= LAYF_NodeAdded, offset = offset->parent);

	tab   = offset->style.tabSize;
	ws    = node->style.whiteSpace;
//	var   = node->style.font.variant;
	trans = node->style.text.transform;
	p     = text;
	end   = p + length;

	nvgFontFaceId(sit.nvgCtx, node->style.font.handle);
	nvgFontSize(sit.nvgCtx,   node->style.font.size);
	nvgTextMetrics(sit.nvgCtx, &bl, NULL, &fh);
	space = nvgTextBounds(sit.nvgCtx, 0, 0, " ", NULL, NULL);

	for (x = 0; p < end; p = next)
	{
		WordWrap w;
		/* find word and following sequence of spaces */
		next = layoutGetWord(p, end, ws, &spc, &nl, &inispc);
		if (next <= p) continue;
		w = layoutGetWordWrap(offset, 0);
		w->word  = p;
		w->n     = next - p - nl - spc;
		w->h     = fh;
		w->bl    = bl;
		w->y     = w->marginR = w->marginL = 0;
		w->node  = node;
		if (trans > 0) layoutTransformText(w, trans);
		w->width = nvgTextBounds(sit.nvgCtx, 0, 0, p, p+w->n, NULL); x += w->width;
		w->space = layoutGetSpaceLen(next - nl - spc, spc, ws, x, space, tab); x += w->space;
		w->nl    = nl > 0;
		w->va    = node->style.verticalAlign;
		if (nl > 0) x = 0;

		/* used to unfuse margins when rendering borders of inline elements */
		{
			SIT_Widget n, old;
			for (n = node, old = NULL; n != offset; n->layout.nbword ++, old = n, n = n->parent)
			{
				if (n->layout.startword == 0) n->layout.startword = offset->layout.wordwrap.count, w->marginL += layoutNCSize(n, CSS_LEFT);
				if (old && old->node.ln_Next == NULL) w->marginR += layoutNCSize(n, CSS_RIGHT);
			}
		}
	}
}

/* inline or inline-block without words but borders, background, ... */
static void layoutAddIfRender(SIT_Widget node)
{
	if (HasBg(node))
	{
		/* add a dummy word */
		SIT_Widget offset, n;
		for (offset = node; offset->type == SIT_HTMLTAG; offset->layout.flags |= LAYF_NodeAdded, offset = offset->parent);
		WordWrap w = layoutGetWordWrap(offset, 0);
		memset(w, 0, sizeof *w);
		w->node  = node;
		w->va    = node->style.verticalAlign;
		w->width = node->layout.pos.width;
		w->h     = w->bl = node->layout.pos.height;
		for (n = node; n != offset; n->layout.nbword ++, n = n->parent)
		{
			if (n->layout.startword == 0) n->layout.startword = offset->layout.wordwrap.count, w->marginL += layoutNCSize(n, CSS_LEFT);
			w->marginR += layoutNCSize(n, CSS_RIGHT);
		}
	}
}

static DATA8 layoutParseEntities(DATA8 text, int max)
{
	DATA8 p, end, eof;
	if (max == 0) max = strlen(text);
	for (p = text, eof = text + max; p < eof; p ++)
	{
		int len;
		if (*p != '&') continue;

		/* be sure we've encountered a known entity */
		for (end = p + 1, len = 0; len < 16 && end < eof && *end != ';'; end ++, len ++);

		if (*end != ';') continue; *end = 0; /* forget ; skip this bogus declaration */

		/* numerical entity? */
		if (p[1] != '#')
		{
			/* first letter of entities is case sensitive */
			StrToLower(p + 2, -1);

			switch (FindInList("amp,lt,gt,nbsp,deg,micro,para,euro", p + 1, 0)) {
			case 0: len = 38; break;
			case 1: len = 60; break;
			case 2: len = 62; break;
			case 3: len = 160; break;
			case 4: len = 176; break;
			case 5: len = 181; break;
			case 6: len = 182; break;
			case 7: len = 8364; break;
			default: len = 0;
			}
			*end = ';';
		}
		else len = (p[2] == 'x' ? strtoul(p+3, NULL, 16) : strtoul(p+2, NULL, 10));

		if (len <= 0 || len > 0x10ffff) continue;

		p += CP2UTF8(p, len);
		end ++;

		if (end >= eof)
		{
			p[0] = 0;
			break;
		}
		memmove(p, end, eof - end); eof -= end - p; p --;
	}
	return p;
}

/* add an image as if it is a regular character */
static void layoutAddSymbol(SIT_Widget node, STRPTR uri, Bool fullH)
{
	CSSImage img = cssAddImage(uri, True);
	SIT_Widget offset;

	if (img == NULL) return;

	for (offset = node; offset->type == SIT_HTMLTAG; offset = offset->parent);

	WordWrap w = vector_nth(&offset->layout.wordwrap, offset->layout.wordwrap.count);

	nvgFontFaceId(sit.nvgCtx, node->style.font.handle);
	nvgFontSize(sit.nvgCtx,   node->style.font.size);
	nvgTextMetrics(sit.nvgCtx, &w->bl, NULL, &w->h);
	if (fullH)
		w->bl = w->h, w->n = -2, w->va = VerticalAlignBottom;
	else
		w->n = -1, w->va = node->style.verticalAlign;
	w->word  = (STRPTR) img;
	w->node  = node;
	w->space = w->y = w->marginR = w->marginL = 0;
	w->nl    = 0;
	w->width = roundf(w->bl * img->width / img->height);
}

typedef struct HTMLAttr_t *      HTMLAttr;

struct HTMLAttr_t /* for parsing only */
{
	HTMLAttr next;
	STRPTR   name;
	STRPTR   value;
};

/* parse everything between < and > */
static STRPTR layoutParseTag(SIT_Widget * parent, DATA8 start)
{
	SIT_Widget node, offset = *parent;
	DATA8      p, name, attr;
	int        nb = 0;
	HTMLAttr   attrs = NULL;
	HTMLAttr   last  = NULL;

	/* get tag name */
	for (name = p = start + 1; *p && *p != '>' && ! isspace(*p); p ++);

	/* go to end of tag */
	for (start = p; *start && *start != '>'; start ++);
	if (start[-1] == '/') start[-1] = 0; /* shouldn't be here */
	else *start = 0;

	if (isspace(*p))
	{
		/* count attributes */
		for (attr = p + 1, *p = 0; isspace(*attr); attr ++);

		for (p = attr; *p; )
		{
			HTMLAttr a = alloca(sizeof *a);
			a->next  = NULL;
			a->name  = p;
			a->value = "1";
			if (last) last->next = a;
			if (! attrs) attrs = a;
			last = a;
			nb += 2;

			/* skip up to next space or '=' */
			while (*p && *p != '=' && ! isspace(*p)) p ++;

			/* check if attr has a value associated */
			if (isspace(*p) || *p == 0)
			{
				if (*p) *p++ = 0;
				while (isspace(*p)) p ++;
				if (*p != '=')
				{
					/* attribute may not have a value associated (boolean) */
					continue;
				}
				for (p++; isspace(*p); p ++);
			}
			else *p++ = 0;
			if (*p == '\'' || *p == '\"')
			{
				uint8_t sep;
				a->value = p + 1;
				for (sep = *p++; *p && *p != sep; p ++);
			}
			else /* value is up to next space */
			{
				a->value = p;
				for (p ++; *p && ! isspace(*p); p ++);
			}
			if (*p)
			{
				for (*p++ = 0; isspace(*p); p ++);
			}
		}
	}

	if (nb > 0) nb ++;

	/* hardcoded: br breaks current line */
	if (strcasecmp(name, "br") == 0)
	{
		while (offset->type == SIT_HTMLTAG) offset = offset->parent;
		int count = offset->layout.wordwrap.count;
		if (count > 0) count --;
		WordWrap w = vector_nth(&offset->layout.wordwrap, count);
		if (w->nl == 1)
		{
			/* already had a break */
			w = vector_nth(&offset->layout.wordwrap, count + 1);
			memset(w, 0, sizeof *w);
			w->node = *parent;
			nvgTextMetrics(sit.nvgCtx, &w->bl, NULL, &w->h);
		}
		w->nl = 1;
		return start + 1;
	}
	else switch (FindInList("xchar,pchar", name, 0)) {
	case 0: nb = 0; goto case_common;
	case 1: nb = 1;
	case_common:
		/* image character: that one pesky symbol not defined in the font */
		if (attrs && strcasecmp(attrs->name, "src") == 0)
			layoutAddSymbol(offset, attrs->value, nb);
		return start + 1;
	}

	node = calloc(sizeof *node + nb * sizeof (DATA8) + strlen(name) + 1, 1);
	node->type = SIT_HTMLTAG;
	node->userData = (APTR) (node+1);
	node->tagName = (DATA8) ((DATA8 *) node->userData + nb);
	node->enabled = node->visible = 1;
	memset(node->layout.crc32, 0xff, sizeof node->layout.crc32);

	/* force lower case node name */
	StrToLower(strcpy(node->tagName, name), -1);

	/* assign attributes */
	if (nb > 0)
	{
		DATA8 * list;
		for (list = node->userData; attrs; attrs = attrs->next, list += 2)
			layoutParseEntities(list[0] = attrs->name, 0),
			layoutParseEntities(list[1] = attrs->value, 0);

		/* NULL-terminate to enable call to GetProperty() */
		list[0] = NULL;
	}
	else node->userData = NULL;

	/* will be needed sooner or later */
	node->classes = SIT_GetHTMLAttr(node, "\1""class");
	node->name    = SIT_GetHTMLAttr(node, "\1""id");
	node->parent  = offset;
	if (! node->name)
		node->name = node->tagName;

	/* append node in the tree */
	ListAddTail(&offset->children, &node->node);

	/* check if this tag can contain other tags */
	if (strcasecmp("img", node->tagName) == 0)
		node->style.flags |= CSSF_IMG;
	else
		*parent = node;

	name = SIT_GetHTMLAttr(node, "style");
	if (IsDef(name))
		cssParseInlineStyles(node, name);

	layoutCalcBox(node);

	return start + 1;
}

/* convert text list of words */
void layoutParseHTML(SIT_Widget w, STRPTR html)
{
	STRPTR     end, p, text;
	SIT_Widget parent;
	int        shadowCnt;

	w->style.shadowTotal = shadowCnt = w->style.shadow ? w->style.shadowCount :
		w->style.shadowInherit ? ((SIT_Widget)w->style.shadowInherit)->style.shadowCount : 0;
	if (html == NULL) return;
	for (p = text = html, parent = w; *p; )
	{
		if (*p == '<')
		{
			int type;
			/* we need to be sure this is a valid tag */
			if (p[1] == '/')   type = 0; else
			if (p[1] == '!')   type = 1; else
			if (isalpha(p[1])) type = 2; else { p ++; continue; /* assume text fragment */ }

			if (p > text)
			{
				end = layoutParseEntities(text, p - text);
				layoutBreakWords(parent, text, end - text);
				text = NULL;
			}

			switch (type) {
			case 0: /* end tag */
				for (p += 2; isspace(*p); p ++);
				for (end = p; *end && *end != '>'; end ++);
				*end++ = 0;
				text = end;
				if ((parent->layout.flags & LAYF_NodeAdded) == 0)
					layoutAddIfRender(parent);
				while (parent != w)
				{
					if (strcasecmp(p, parent->tagName) == 0)
					{
						parent = parent->parent;
						break;
					}
					parent = parent->parent;
				}
				p = end;
				break;
			case 1:
				if (strncmp(p + 2, "--", 2) == 0)
				{
					/* comment: skip content */
					for (p += 4; *p && strncmp(p, "-->", 3); p ++);
					if (*p) p += 3; text = p;
				}
				else /* doc type: skip content */
				{
					for (p += 3; *p && *p != '>'; p ++);
					if (*p) p ++;
				}
				break;
			case 2: /* tag */
				text = p = layoutParseTag(&parent, p);
				type = parent->style.shadowCount;
				if (shadowCnt < type)
					shadowCnt = type;
			}
			continue;
		}
		else p ++;
	}
	if (IsDef(text))
	{
		p = layoutParseEntities(text, p - text);
		layoutBreakWords(parent, text, p - text);
	}
	w->style.shadowTotal = shadowCnt;
}

void layoutFree(SIT_Widget start)
{
	SIT_Widget node, next;

	start->layout.wordwrap.count = 0;
	/* tree of nodes */
	for (node = next = HEAD(start->children); node; )
	{
		if (node->children.lh_Head == NULL)
		{
			while (node->node.ln_Next == NULL && node->parent)
			{
				next = node->parent;
				SIT_FreeCSS(node);
				free(node);
				node = next;
				if (node == start)
				{
					memset(&start->children, 0, sizeof start->children);
					return;
				}
			}
			next = node;
			NEXT(node);
			SIT_FreeCSS(next);
			free(next);
		}
		else node = HEAD(node->children);
	}
}

static void StrCatLen(STRPTR dest, STRPTR src, int dmax, int smax)
{
	STRPTR d = dest;
	STRPTR s = src;
	int    i = dmax;
	int    j = smax;

	while (i > 0 && *d) d ++, i --;
	while (i > 0 && j != 0 && (*d = *s++)) i --, j --, d ++;

	if (i    > 0) d[ 0] = 0; else
	if (dmax > 0) d[-1] = 0;
}

void layoutFindFont(SIT_Widget node)
{
	/* already find font id */
	static STRPTR defaultFonts[] = {"sans-serif", "serif", "monospace"};

	Font *  font   = &node->style.font;
	STRPTR  family = font->family, p, seeOther;
	int     fontId = -1;
	uint8_t style  = 0;
	int     try    = 0;

	if (! IsDef(family))
	{
		SIT_Widget parent = node->parent;
		if (parent && memcmp(&parent->style.font.weight, &node->style.font.weight, offsetof(Font, handle) - offsetof(Font, weight)) == 0)
		{
			/* use parent (inherit) */
			node->style.font.handle = parent->style.font.handle;
			return;
		}
		/* this actually defines the default font... should have a better visibility (maybe default.css?) */
		else family = defaultFonts[0], try = 1;
	}

	do {
		/* comma separated list of names: try them in order */
		TEXT name[56];
		char chr;
		for (p = family, chr = 0; *p && *p != ',' && *p != '!'; p ++);
		while (p > family && cssIsSpace(p[-1])) p --;
		seeOther = p;
		if (*p) chr = *p, *p = 0, seeOther ++;
		CopyString(name, family, sizeof name);
		/* remove quotes if needed */
		if (family[0] == '\'' || family[0] == '\"')
			cssTokenizer(name, &family), strcpy(name, name+1);
		if (chr) *p = chr;
		for (family = seeOther; cssIsSpace(family[0]); family ++);
		if (*family == 0) family = NULL;

		if (font->weight >= 700)
			StrCatLen(name, "-bold", sizeof name, -1), style ++;
		if (font->italic)
			StrCatLen(name, "-italic", sizeof name, -1), style ++;

		fontId = nvgFindFont(sit.nvgCtx, name);

		if (fontId >= 0) break;

		/* requested font not found, try some fallback fonts */
		if (family == NULL && try < 3)
		{
			/* try first removing some styles */
			while (style > 0)
			{
				STRPTR sep = strrchr(name, '-');
				if (sep) *sep = 0;
				fontId = nvgFindFont(sit.nvgCtx, name);
				if (fontId >= 0) goto break_loop;
				style --;
			}
			family = defaultFonts[try];
			try ++;
		}
	}
	while (family);
	break_loop:
	font->handle = fontId;
}

#if 0 /* done in layoutParseHTML(), not sure if needed somewhere else */

/* count max number of text shadows layer within children */
static int layoutGetTextShadowCount(SIT_Widget root)
{
	SIT_Widget node = root;
	int max = 0;
	while (node)
	{
		if ((node->layout.flags & LAYF_NodeAdded) || node == root)
		{
			int count = node->style.shadow ? node->style.shadowCount :
						node->style.shadowInherit ? ((SIT_Widget)node->style.shadowInherit)->style.shadowCount : 0;

			if (max < count)
				max = count;

			/* need to scan all children :-/ */
			if (node->children.lh_Head)
			{
				node = HEAD(node->children);
				continue;
			}
		}

		if (node == root) break;
		if (node->node.ln_Next == NULL)
		{
			do {
				node = node->parent;
				if (node == root) return max;
			} while (node->node.ln_Next == NULL);
		}
		NEXT(node);
	}
	return max;
}
#endif

static void layoutSetTextShadow(SIT_Widget node)
{
	int i, j;
	for (i = 0; i < node->style.shadowCount; i ++)
	{
		TextShadow shadow = node->style.shadow + i;
		ULONG * fixed = &shadow->pos.XYfixed[0];
		float * real  = &shadow->pos.XYfloat[0];

		for (j = 0; j < 3; j ++, fixed ++, real ++)
			*real = *fixed ? ToPoints(node, node, *fixed, CSS_LEFT) : 0;
	}
	node->style.flags &= ~CSSF_TEXTSHADOW;
	node->style.shadowTotal = node->style.shadowCount;
}

static void layoutSetBoxShadow(SIT_Widget node)
{
	int i, j;
	for (i = 0; i < node->style.boxShadowCount; i ++)
	{
		BoxShadow shadow = node->style.boxShadow + i;
		ULONG * fixed = &shadow->XYSfixed[0];
		float * real  = &shadow->XYSfloat[0];

		for (j = 0; j < 4; j ++, fixed ++, real ++)
			*real = *fixed ? ToPoints(node, node, *fixed, CSS_LEFT) : 0;
	}
	node->style.flags &= ~CSSF_BOXSHADOW;
}

static void layoutSetOutline(SIT_Widget node)
{
	node->layout.flags &= ~LAYF_NoOutline;
	node->layout.outlineWidth = ToPoints(node->parent, node, node->style.outline.width, 0);
	node->layout.outlineOffset = ToPoints(node->parent, node, node->style.outlineOffset, 0);
	if (node->layout.outlineWidth < EPSILON || node->style.outline.style == BorderStyleNone)
		node->layout.flags |= LAYF_NoOutline;
}

static void layoutSetBackgroundPos(SIT_Widget node)
{
	/* convert background-position to pixels (percentage will need adjustment) */
	int i;
	for (i = node->style.bgCount-1; i >= 0; i --)
	{
		Background bg = node->style.background + i;
		if (bg->x != AUTOVAL) bg->dim.left = ToPoints(node, node, bg->x, CSS_LEFT);
		if (bg->y != AUTOVAL) bg->dim.top  = ToPoints(node, node, bg->y, CSS_TOP);
	}
	node->style.flags &= ~CSSF_BACKGROUND;
}

void layoutCalcBox(SIT_Widget node)
{
	SIT_Widget parent = node->parent;
	int i;
	node->layout.offset = parent;
	node->layout.flags &= ~(LAYF_NoBorders|LAYF_NoOutline);

	if ((node->style.flags & CSSF_APPLIED) == 0)
		cssApply(node);

	if (parent && node->style.shadow == NULL && (parent->style.shadow || parent->style.shadowInherit) && ! (node->style.flags & LAYF_NoTextShadow))
	{
		/* can't link directly to style.shadow because it can be re-allocated */
		if (! (node->style.shadowInherit = parent->style.shadowInherit))
			node->style.shadowInherit = parent;
		node->style.shadowCount = parent->style.shadowCount;
	}
	else if (node->style.flags & CSSF_TEXTSHADOW)
	{
		layoutSetTextShadow(node);
	}

//	if (node->type != SIT_HTMLTAG && node->layout.wordwrap.count > 0)
//		node->style.shadowTotal = layoutGetTextShadowCount(node);

	if (node->style.flags & CSSF_BOXSHADOW)
		layoutSetBoxShadow(node);

	/* compute padding, border and margin offset */
	for (i = 0; i < 4; i ++)
	{
		Border * border = &node->style.borderTop + i;
		REAL * bdWidth = &node->layout.border.top + i;

		/* relative units are always relative to width */
		*bdWidth = ToPoints(parent, node, border->width, CSS_WIDTH);

		(&node->layout.padding.top)[i] = ToPoints(parent, node, (&node->style.padding.top)[i], CSS_WIDTH);
	}

	if (node->type == SIT_LISTBOX)
	{
		SIT_ListBox list = (SIT_ListBox) node;
		list->borderSpacingV = ToPoints(parent, node, list->borderCSSV, CSS_WIDTH);
		list->borderSpacingH = ToPoints(parent, node, list->borderCSSH, CSS_WIDTH);
	}

	if (node->style.wordSpacing != AUTOVAL)
		node->layout.wordSpacing = ToPoints(parent, node, node->style.wordSpacing, CSS_WIDTH);
	if (node->style.letterSpacing != AUTOVAL)
		node->layout.letterSpacing = ToPoints(parent, node, node->style.letterSpacing, CSS_WIDTH);

	#if 0
	if (parent == NULL)
	{
		REAL w = node->fixed.width;
        REAL h = node->fixed.height;

		w -= layoutNCSize(node, CSS_LEFT) + layoutNCSize(node, CSS_RIGHT);
		h -= layoutNCSize(node, CSS_TOP)  + layoutNCSize(node, CSS_BOTTOM);

		node->layout.pos.width = w;
		node->layout.pos.height = h;
	}
	#endif
	layoutFindFont(node);
	layoutSetOutline(node);

	/* perform some checks to simplify rendering phase */
	if (node->layout.border.left < EPSILON && node->layout.border.right < EPSILON &&
		node->layout.border.top  < EPSILON && node->layout.border.bottom < EPSILON)
		node->layout.flags |= LAYF_NoBorders;
	else
		node->layout.flags &= ~LAYF_NoBorders;

	if (node->style.borderImg)
	{
		BorderImg bimg = node->style.borderImg;
		for (i = 0; i < 4; i ++)
		{
			(&bimg->widthFloat.top)[i] = ToPoints(node, node, (&bimg->widthFixed.top)[i], CSS_WIDTH);
			(&bimg->offFloat.top)[i]   = ToPoints(node, node, (&bimg->offFixed.top)[i],   CSS_WIDTH);
		}
	}

	node->layout.margin = ToPoints(parent, node, node->style.margin, CSS_WIDTH);
	layoutSetBackgroundPos(node);

	/* combined padding and border (SIT order) */
	for (i = 0; i < 4; i ++)
		node->padding[i] = roundf((&node->layout.padding.top)[(i+3)&3] + (&node->layout.border.top)[(i+3)&3]) + node->layout.margin;

	node->layout.left = ToPoints(parent, node, node->style.left, CSS_WIDTH);
	node->layout.top  = ToPoints(parent, node, node->style.top,  CSS_HEIGHT);

	if (node->style.flags & CSSF_IMG)
	{
		CSSImage img = cssAddImage(SIT_GetHTMLAttr(node, "src"), False);

		if (img)
		{
			REAL w = img->width;
			REAL h = img->height;

			node->layout.flags |= LAYF_HasImg;
			node->manage = (APTR) img;

			if (node->style.width != AUTOVAL)
				node->layout.pos.width = ToPoints(parent, node, node->style.width, CSS_WIDTH);

			if (node->style.height != AUTOVAL)
				node->layout.pos.height = ToPoints(parent, node, node->style.height, CSS_HEIGHT);

			switch ((node->style.width == AUTOVAL) | ((node->style.height == AUTOVAL) << 1)) {
			case 0: /* width & height set: override image's dimension */
				w = node->layout.pos.width;
				h = node->layout.pos.height;
				break;
			case 1: /* width not set: relative to height */
				w = w * node->layout.pos.height / h;
				h = node->layout.pos.height;
				if (w < 1) w = 1;
				break;
			case 2: /* height not set: relative to width */
				h = h * node->layout.pos.width / w;
				w = node->layout.pos.width;
				if (h < 1) h = 1;
			}
			node->layout.pos.width  = roundf(w);
			node->layout.pos.height = roundf(h);

			layoutAddIfRender(node);
		}
		#if 0
		else if ((attr = SIT_GetHTMLAttr(node, "alt")) && attr[0])
		{
			/* image not found: render its alt attribute as text */
			DOMText txt = calloc(sizeof *txt, 1);
			node->flag &= ~CM_IMG;
			txt->text   = attr;
			txt->length = strlen(attr);
			txt->type   = DNT_TEXT;
			txt->parent = node;
			node->reflow = 0;
			node->style.verticalAlign = VerticalAlignBaseline;
			node->style.height = node->style.width = AUTOVAL;
			node->layout.pos.height = node->layout.pos.width = 0;
			node->layout.flags &= ~(WidthSet | HeightSet);
			ListAddTail(&node->children, &txt->node);
		}
		#endif
	}
}

void layoutSetSize(SIT_Widget node)
{
	RectF pos = {0};

	if (node->layout.wordwrap.count > 0 || node->title)
	{
		SizeF words = {0};
		layoutMeasureWords(node, &words);
		pos.width = words.width;
		pos.height = words.height;
	}

	if (node->flags & SITF_FixedWidth)  pos.width  = node->fixed.width;
	if (node->flags & SITF_FixedHeight) pos.height = node->fixed.height;
	if (node->flags & SITF_FixedX)      pos.left   = node->fixed.left;
	if (node->flags & SITF_FixedY)      pos.top    = node->fixed.top;

	pos.width  += pos.left;
	pos.height += pos.top;

	memcpy(&node->box, &pos, sizeof pos);
	SIT_LayoutCSSSize(node);
}

/* class or state has changed */
int layoutUpdateStyles(SIT_Widget node)
{
	int changes = cssCRCChanged(node) ? cssApply(node) : 0;

	if ((node->layout.flags & LAYF_NoChanges) == 0 && (node->flags & SITF_PrivateChildren))
	{
		SIT_Widget c;
		int ret = 0;
		for (c = HEAD(node->children); c; NEXT(c))
		{
			c->flags |= SITF_RecalcStyles;
			ret |= layoutUpdateStyles(c);
			c->flags &= ~SITF_RecalcStyles;
		}
		if (ret == 0) node->layout.flags |= LAYF_NoChanges;
		changes |= ret & 3;
	}

	if (changes)
	{
		/* new styles have been applied */
		layoutCalcBox(node);
		if (node->style.reflow & ReflowOutline)
		{
			layoutSetOutline(node);
		}
		if (node->style.reflow & ReflowLayout)
		{
			if (node->optimalWidth)
			{
				SizeF current = {0, 0};
				SIT_Widget max;
				if ((max = (SIT_Widget) node->max.ln_Next) || (max = (SIT_Widget) node->max.ln_Prev))
					current.width = max->box.right - max->box.left;
				node->optimalWidth(node, &current, (APTR) FitUsingCurrentBox);
				if (! ALMOST0(current.width - node->currentBox.width) || ! ALMOST0(current.height - node->currentBox.height))
				{
					memcpy(&node->currentBox, &current, sizeof current);
					SIT_InitiateReflow(node);
					#ifdef DEBUG_SIT
					fprintf(stderr, "%s: geometric reflow required\n", node->name);
					#endif
				}
				else if (max)
				{
					layoutAlignText(node);
				}
				// else fprintf(stderr, "%s: styles changed, but same size\n", node->name);
			}
		}
		if (node->style.reflow & ReflowOffset)
		{
			node->layout.left = ToPoints(node->parent, node, node->style.left, CSS_WIDTH);
			node->layout.top  = ToPoints(node->parent, node, node->style.top,  CSS_HEIGHT);
		}
		node->style.reflow = 0;
	}
	else /* might still need to recompute a few things */
	{
		if (node->style.flags & CSSF_TEXTSHADOW)
			layoutSetTextShadow(node);

		if (node->style.flags & CSSF_BOXSHADOW)
			layoutSetBoxShadow(node);

		if (node->style.flags & CSSF_BACKGROUND)
			layoutSetBackgroundPos(node);

		if (node->style.font.handle < 0)
			layoutFindFont(node);
	}
	return changes;
}

static Bool layoutCanBreak(SIT_Widget root, SIT_Widget node)
{
	static uint8_t canBreak[] = {1,0,0,0,1};

	for (;; node = node->parent)
	{
		if (! canBreak[node->style.whiteSpace]) return False;
		if (node == root) break;
	}
	return True;
}

#define PACK(b1,b2,b3,b4,b5,b6,b7,b8)     ((b1) | (b2<<1) | (b3<<2) | (b4<<3) | (b5<<4) | (b6<<5) | (b7<<6) | (b8<<7))
static uint8_t ignoreDescent[] = { /* ASCII only :-/ */
	PACK(0, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1), // 0
	PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1), // 16
	PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 0, 1, 1, 1), // 32  - ,
	PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 0, 1, 1, 1, 1), // 48  - ;
	PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1), // 64
	PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 0), // 80  - _
	PACK(1, 1, 1, 1, 1, 1, 1, 0), PACK(1, 1, 0, 1, 1, 1, 1, 1), // 96  - gj
	PACK(0, 0, 1, 1, 1, 1, 1, 1), PACK(1, 0, 1, 1, 1, 1, 1, 1), // 112 - pqy
};

/* multiple words */
void layoutMeasureWords(SIT_Widget node, SizeF * ret)
{
	WordWrap w, word;
	REAL     width, lineHeight = roundf(layoutCalcLineHeight(node));
	int      i = node->layout.wordwrap.count;

	if (i == 0 && node->title)
	{
		/* simplified text */
		REAL desc = 0;
		nvgFontFaceId(sit.nvgCtx, node->style.font.handle);
		nvgFontSize(sit.nvgCtx, node->style.font.size);
		nvgTextLetterSpacing(sit.nvgCtx, node->layout.letterSpacing);

		if (node->layout.flags & LAYF_CompactText)
		{
			DATA8 p;
			uint8_t chr;
			/* if there is no descending characters, shorten the height for a more appealing vertical centering */
			for (p = node->title; (chr = *p) < 0x80 && (ignoreDescent[chr>>3] & (1<<(chr&7))); p ++);
			if (*p == 0) nvgTextMetrics(sit.nvgCtx, NULL, &desc, NULL);
		}
		ret->width = width = nvgTextBounds(sit.nvgCtx, 0, 0, node->title, NULL, NULL);
		ret->height = node->style.font.size + desc;
		node->layout.textarea = *ret;
		switch (node->style.text.align) {
		case TextAlignLeft:   width = 0; break;
		case TextAlignRight:  width = node->layout.pos.width - width; break;
		case TextAlignCenter: width = (node->layout.pos.width - width) * 0.5f;
		}
		node->layout.wordSpacing = width;
		return;
	}

	/* compute width and prepare height */
	REAL maxw, maxh, maxWidth = ret->width;
	REAL height, asc, desc, off, tmp;
	if (node->maxBox.width > maxWidth)
		maxWidth = node->maxBox.width;
	if (maxWidth <= 0) maxWidth = sit.root->fixed.width;
	for (w = word = vector_first(node->layout.wordwrap), maxw = maxh = width = height = asc = desc = 0; i > 0; w ++)
	{
		REAL h = w->h;
		w->nl &= ~2; /* remove previous soft line break */
		if (w->marginL < 0)
			w->marginL = 0; /* cancel text-align */
		switch (w->va) {
		case VerticalAlignTextBottom:
		case VerticalAlignBottom:
		case VerticalAlignTextTop:
		case VerticalAlignTop:
		case VerticalAlignMiddle:
			if (height < h) height = h;
			break;
		case VerticalAlignBaseline:
			if (asc < w->bl) asc = w->bl; tmp = h-w->bl;
			if (desc < tmp)  desc = tmp;
			break;
		case VerticalAlignSub:
			off = h/5;
			goto case_common;
		case VerticalAlignSuper:
			off = -h/5;
			goto case_common;
		default:
			off = w->y = ToPoints(node, w->node, w->va, CSS_HEIGHT); /* pos or % */
		case_common:
			tmp = w->bl-off;
			if (asc < tmp) asc = tmp; tmp = h-w->bl+off;
			if (desc < tmp) desc = tmp;
		}
		tmp = w->width + w->space + w->marginL + w->marginR;
		if (width + tmp > maxWidth && w > word && layoutCanBreak(node, w->node))
		{
			w --;
			w->nl |= 2;
		}
		else width += tmp, i --;
		if (w->nl || i == 0)
		{
			desc += asc;
			if (height < desc)
				height = desc;
			if (height < lineHeight)
			{
				asc += lineHeight - height;
				height = lineHeight;
			}
			/* compute height and assign baseline to each word */
			while (word <= w)
			{
				REAL wordh = word->h;
				switch (word->va) {
				case VerticalAlignTextBottom:
				case VerticalAlignBottom:   word->y = height - wordh; break;
				case VerticalAlignTextTop:
				case VerticalAlignTop:      word->y = 0; break;
				case VerticalAlignMiddle:   word->y = (height - wordh) * 0.5f; break;
				case VerticalAlignBaseline: word->y = asc - word->bl; break;
				case VerticalAlignSub:      word->y = asc - word->bl + wordh/5; break;
				case VerticalAlignSuper:    word->y = asc - word->bl - wordh/5; break;
				default:                    word->y += asc - word->bl;
				}
				word->y = roundf(word->y);
				word ++;
			}
			if (maxw < width)
				maxw = width;
			maxh += height;
			width = height = asc = desc = 0;
		}
	}
	ret->height = maxh;
	ret->width  = MIN(maxw, maxWidth);
	node->layout.textarea = *ret;
}

REAL layoutLineWidth(WordWrap wrap, int max)
{
	WordWrap w;
	REAL     width;
	int      i;
	for (i = max, width = 0, w = wrap; i > 0; i --, w ++)
	{
		width += w->width + w->marginL + w->marginR;
		if (w->nl) break;
		if (i > 1) width += w->space;
	}
	return width;
}

/* need to be done once the size is completely known (SIT_Geometry.c:SIT_LayoutCSSSize()) */
void layoutAlignText(SIT_Widget node)
{
	WordWrap w, start;
	REAL     width, maxWidth = node->layout.pos.width;
	uint8_t  ta = node->style.text.align;
	int      count = node->layout.wordwrap.count;

	if (count == 0)
	{
		if (node->title)
		{
			width = node->layout.textarea.width;
			switch (ta) {
			case TextAlignLeft:   width = 0; break;
			case TextAlignRight:  width = maxWidth - width; break;
			case TextAlignCenter: width = (maxWidth - width) * 0.5f;
			}
			node->layout.wordSpacing = width;
		}
		else return;
	}
	for (w = start = vector_first(node->layout.wordwrap), width = 0; count > 0; count --, w ++)
	{
		width += w->width + w->space + w->marginR;
		if (w->marginL > 0)
			width += w->marginL;
		if (w->nl || count == 1)
		{
			width -= w->space;
			REAL pos = 0;
			switch (ta) {
			case TextAlignRight:  pos = width - maxWidth; break;
			case TextAlignCenter: pos = (width - maxWidth) * 0.5f;
			}
			if (pos < 0) start->marginL = pos;
			start = w + 1;
			width = 0;
		}
	}
}

/* get textual content from node */
void layoutGetTextContent(SIT_Widget node, STRPTR dest, int max)
{
	SIT_Widget offset;
	WordWrap   w;
	int        n;
	for (offset = node; offset->type == SIT_HTMLTAG; offset = offset->parent);

	dest[0] = 0;
	n = offset->layout.wordwrap.count;
	if (n == 0)
	{
		CopyString(dest, offset->title, max);
	}
	else for (w = vector_nth(&offset->layout.wordwrap, 0); n > 0; n --, w ++)
	{
		SIT_Widget c;
		for (c = w->node; c != offset && c != node; c = c->parent);
		if (c == node)
		{
			StrCatLen(dest, w->word, max, w->n);
			if (w->space > 0) StrCatLen(dest, " ", max, 1);
		}
	}
}
