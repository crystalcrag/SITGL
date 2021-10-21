/*
 * shtmlCSSApply.c : apply CSS rules to SIT_Widget, filling SIT_Widget->style struct.
 *
 * written by T.Pierron, may 2020.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include "SIT_CSSParser.h"

vector_t cssStyles;

struct CSSAttr_t /* keep it private */
{
	STRPTR attr;
	STRPTR defval;
	STRPTR format;
	char   inherit;         /* look in parent if unset */
	char   sz;              /* in bytes, 0 for sizeof (ULONG) */
	char   objtype;         /* 0: style, 1: background, 2: shadow */
	char   reflow;          /* reflow node if property changed */
	ULONG  get;             /* offset of field to get its real value (if > 0) */
	ULONG  arg1;            /* offset of properties within style struct (in bytes) */
	ULONG  arg2;
	ULONG  arg3;
	ULONG  arg4;
	ULONG  arg5;
	ULONG  sentinel;        /* Must be 0 */
};

typedef struct CSSAttr_t *    CSSAttr;

enum /* attributes that needs special processing */
{
	cssAttrBackground = 1,
	cssAttrBgColor,
	cssAttrBgImage,
	cssAttrBgPos,
	cssAttrBgTransform,
	cssAttrBgModColor,
	cssAttrBorder,
	cssAttrBorderImg,
	cssAttrBorderImgSrc,
	cssAttrBorderImgSlice,
	cssAttrBorderImgWidth,
	cssAttrBorderImgOutset,
	cssAttrBorderImgRepeat,
	cssAttrBorderRadius,
	cssAttrBorderSpacing,
	cssAttrFont,
	cssAttrFontSize,
	cssAttrFontWeight,
	cssAttrLineHeight,
	cssAttrTextShadow,
	cssAttrBoxShadow,
	cssAttrTransform,
	cssAttrCaret,
	cssAttrSelection,
	cssAttrLast
};

static uint8_t hoffset[cssAttrLast];
static uint8_t inherited[13];

static struct CSSAttr_t cssattrs[] = {
#include "SIT_CSSAttrs.h"
};

static STRPTR colorNames[] = {
#include "SIT_CSSColors.h"
};

#define GET(cssAttr)    (cssattrs + hoffset[cssAttr])

/* map SIT_Widget->state to SIT_Widget->style.crc32 slots */
static uint8_t state2crc[] = {
	0, /* 0: normal */
	1, /* 1: hover */
	1, /* 2: active, but no hover */
	2, /* 3: active */
	3, /* 4: focus */
	1, /* 5: focus+hover */
	0, /* 6: focus+active, no hover */
	2, /* 7: focus+active+hover */
	4, /* 8: checked */
	4, 4, 4, 4, 4, 4, 4,
	5, /* 16: kbdfocus */
	5, /* 17: kbdfocus+hover */
	5, /* 18: kbdfocus */
	6, /* 19: kbdfocus+active */
	4, 5, 4, 6,
	5, 5, 5, 5, 5, 5, 5, 5,
	7  /* 32: disabled */
};

static uint32_t crctable[256];

/* default color */
static CSSColor black = {.rgba = {0,0,0,255}};

CSSAttr cssAttrSearch(STRPTR str);

int CSS_Init(STRPTR theme, int isPath)
{
	static char defval[10];
	int i, j;

	/* supported CSS attributes */
	for (i = j = 0; i < DIM(cssattrs); i ++)
	{
		CSSAttr css = cssattrs + i;
		uint8_t inherit = css->inherit;
//		if (css->attr)
//			fprintf(stderr, "%s, %d\n", css->attr, i);
		if (css->sz == 0)
			css->sz = 4;
		if (inherit > 3)
			hoffset[css->inherit >> 2] = i;
		if ((inherit & 3) && j < DIM(inherited)-1)
			inherited[++j] = i, css->inherit = (inherit & 3) | (j<<2);

//		if (css->attr && ! cssAttrSearch(css->attr))
//			fprintf(stderr, "attr not found: %s\n", css->attr);
	}

	for (i = 0; i < 256; i++)
	{
		int c = i, k;
		for (k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
		crctable[i] = c;
	}

	/* this will make all font size relative unit to be somewhat HiDPI aware */
	sprintf(defval, "=%dpt", sit.defFontHeight);
	CSSAttr attr = GET(cssAttrFontSize);
	attr->defval = defval;

	vector_init(cssStyles, sizeof (struct CSSStyle_t));

	return cssParse(theme, isPath);
}

/*
 * These 2 functions have been generated with gperf.exe: it is a static hash tables for
 * CSS attributes. Using HashTable from UtilityLib requires 7 Kb of alloc +  5 Kb of code,
 * whereas these tables + code only need less than 1 Kb.
 *
 * If the table "cssattrs" is changed, check bin/readme.html to see how to regenerate the code below.
 */
static int cssAttrHash(DATA8 str, int len)
{
	static uint8_t asso_values[] = {
		 10, 205, 205, 205, 205, 205, 205, 205, 205, 205,
		205, 205, 205, 205, 205, 205, 205, 205, 205, 205,
		  0,   0,  35,  25,   0,   5,  25,  55,  55, 205,
		205,  40,  80,  90,   0,  20, 205,   5,  30,   0,
		205,   5,  45, 205,  75,   0, 205, 205, 205, 205,
		205, 205,   0,   0,  35,  25,   0,   5,  25,  55,
		 55, 205, 205,  40,  80,  90,   0,  20, 205,   5,
		 30,   0, 205,   5,  45, 205,  75,   0,
	};
	uint8_t chr;
	int     hval = len;

	#define decodeState(ref)    ((chr = ref) > 122 || chr < 45 ? 205 : asso_values[chr-45])
	#define decodeState(ref)    ((chr = ref) > 122 || chr < 45 ? 205 : asso_values[chr-45])
	switch (hval) {
	default: hval += decodeState(str[13]); // no break;
	case 13:
	case 12:
	case 11:
	case 10: hval += decodeState(str[9]); // no break;
	case 9:
	case 8:
	case 7:
	case 6:
	case 5:
	case 4:
	case 3:
	case 2:
	case 1: hval += decodeState(str[0]);
	}
	return hval + decodeState(str[len - 1]);
	#undef decodeState
}

CSSAttr cssAttrSearch(STRPTR str)
{
	static uint8_t wordlist[] = {
		255, 255, 255, 255, 255, 255, 255,  66,  74,  55, 255,  33,  45,  63,  51,  52,
		 27,  53,  62,  41, 255, 255,  31,  83,  42, 255,  78,  22,  11,  30, 255,  77,
		 71, 255,  21, 255, 255,  25, 255, 255,   8,   5, 255, 255,  57,  49,   3, 255,
		 39,  43,  24,  68,  72,  67, 255, 255,  81,  23, 255,  17,   9,  56,   7,  80,
		 18,  15,  14,  48,  16, 255, 255,  54, 255,  29, 255, 255, 255,  12, 255, 255,
		255,  19,  61,  20,  36, 255,   4,  32,  69,  84, 255,  89,  38,  26, 255, 255,
		255, 255,  13, 255,  47,  79, 255, 255,  58,  90,  59, 255,  10, 255,  76,  28,
		255, 255, 255,   2,  34, 255,  40, 255, 255, 255, 255,  64, 255, 255, 255, 255,
		255,  73, 255,  50,  37, 255, 255, 255, 255,  91, 255, 255, 255,  87, 255,  35,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255,   6, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		 60, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  75, 255,
		255, 255,  70, 255, 255, 255, 255,  88, 255, 255, 255, 255,  82,
    };

	int len = strlen(str);
	if (len <= 26 && len >= 3)
    {
		int key = cssAttrHash(str, len);

		if (key <= DIM(wordlist) && key >= 0)
		{
			uint8_t off = wordlist[key];
			if (off < 255 && strcasecmp(cssattrs[off].attr, str) == 0)
				return cssattrs + off;
		}
	}
	return NULL;
}

static int cssCmpColor(const void * item1, const void * item2)
{
	STRPTR * col1 = (STRPTR *) item1;
	STRPTR * col2 = (STRPTR *) item2;
	return strcasecmp(col1[0], col2[0]);
}

ULONG cssColorSearch(STRPTR color)
{
	/* simple binary sarch will be enough */
	STRPTR * value = bsearch(&color, colorNames, DIM(colorNames), 2 * sizeof (STRPTR), cssCmpColor);

	return value ? (ULONG) value[1] : 0;
}

uint32_t crc32(uint32_t crc, DATA8 buf, int max)
{
	int len = max < 0 ? strlen(buf) + 1 : max;
	crc = crc ^ 0xffffffffL;
	while (len > 0)
	{
		crc = crctable[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
		len --;
	}
	return crc ^ 0xffffffffL;
}

void cssFreeGlobals(void)
{
	vector_free(cssStyles);
}

DLLIMP Bool SIT_GetCSSValue(SIT_Widget w, STRPTR property, APTR mem)
{
	CSSAttr attr = cssAttrSearch(property);

	/* if you are a rust developper, please close your eyes */
	if (attr)
	{
		if (attr == GET(cssAttrSelection))
		{
			/* retrieve both fg and bg at the same time (in that order) */
			memcpy(mem, &w->style.fgSel, 8);
		}
		else if (attr == GET(cssAttrLineHeight))
		{
			REAL layoutCalcLineHeight(SIT_Widget);
			/* there got to be a special case :-/ */
			* (float *) mem = layoutCalcLineHeight(w);
		}
		else if (attr == GET(cssAttrBgImage))
		{
			Background bg = w->style.background;
			if (bg == NULL) return False;
			* (int *) mem = ((CSSImage)bg->image)->handle;
		}
		else if (attr->get > 0)
		{
			/* yes, completely safe as long a mem can hold 4 bytes */
			* (float *) mem = * (REAL *) ((DATA8)w + attr->get);
		}
		else if (attr->format && StrCount(attr->format, ' ') == 0)
		{
			/* only single arg property */
			if (attr->sz == 1)
			{
				/* single byte arg: retrieve them as int though */
				* (int *) mem = ((DATA8)w)[attr->arg1];
			}
			else if (strcmp(attr->format, "COLOR") == 0)
			{
				/* 4 bytes: RGBA values */
				memcpy(mem, (DATA8)w + attr->arg1, 4);
			}
			else return False;
		}
		return True;
	}
	return False;
}

/* CSS class selector match */
static Bool cssMatchList(STRPTR list, STRPTR item)
{
	STRPTR p, prev;
	int    l;

	for (p = prev = skipspace(list), l = strlen(item); ; p ++)
	{
		if (*p == 0 || cssIsSpace(*p))
		{
			if (p - prev == l && strncasecmp(prev, item, l) == 0)
				return True;

			if (*p == 0) return False;
			prev = p = skipspace(p);
		}
	}
	return False;
}

/*
 * Check if a CSS selector match a stack of nodes. This is the core function
 * that is used to apply CSS rules to DOMNode. Current complexity of this
 * function sucks (O(NbRules*NbNodes)), could be O(RulesDepth*NbNodes)
 */
static Bool cssMatchSelector(SIT_Widget * stack, int level, CSSRule rule)
{
	SIT_Widget node;
	STRPTR     attr, arg;
	int *      path  = alloca(level * sizeof *path);
	int        track = 0;
	int        skip  = 0, ir, is, curtrans = CSST_CURRENT;

	/* easier to match from end to beginning */
	for (ir = rule->nbsel - 1, is = level - 1, node = NULL; is >= 0 && ir >= 0; )
	{
		CSSSel s = (CSSSel) (rule + 1) + ir;

		if (skip == 0) node = stack[is];
		else skip = 0;

		while (is >= 0)
		{
			Bool match = False;

			switch (s->type) {
			case CSSR_NONE:
				// fprintf(stderr, "invalid 'NONE' transition for rule.\n");
				break;
			case CSSR_TAG:
				match = strcmp(s->item, "*") == 0 || strcasecmp(node->tagName, s->item) == 0;
				break;
			case CSSR_PSEUDO:
				switch (FindInList("link,active,hover,focus,kbdfocus,checked,disabled", s->item, 0)) {
				case 0: /* link */
					attr  = SIT_GetHTMLAttr(node, "href");
					match = strcmp(node->tagName, "a") == 0 && IsDef(attr);
					if (match) node->style.flags |= CSSF_LINK;
					break;
				case 1: /* active */
					match = (node->state & STATE_ACTIVATED) == STATE_ACTIVATED;
					break;
				case 2: /* hover */
					match = (node->state & STATE_HOVER) > 0;
					break;
				case 3: /* focus */
					match = (node->state & STATE_FOCUS) > 0;
					break;
				case 4: /* kbdfocus */
					match = (node->state & STATE_KBDFOCUS) > 0;
					break;
				case 5: /* checked */
					match = (node->state & STATE_CHECKED) > 0;
					break;
				case 6: /* disabled */
					match = node->enabled == 0;
				}
				break;
			case CSSR_ATTR:
			case CSSR_ATTREQV:
			case CSSR_ATTRDASH:
			case CSSR_ATTRLIST:
				attr = SIT_GetHTMLAttr(node, s->item);
				arg  = strchr(s->item, 0) + 1;
				if (s->type == CSSR_ATTR || s->type == CSSR_ATTREQV)
				{
					if (*arg) match = strcasecmp(arg, attr) == 0;
					else      match = IsDef(attr);
				}
				else match = cssMatchList(attr, arg);
				break;
			case CSSR_CLASS:
				match = node->classes && cssMatchList(node->classes, s->item);
				break;
			case CSSR_ID:
				match = strcasecmp(node->name, s->item) == 0;
			}

			if (match)
			{
				/* walk in tree according to transition type */
				switch (curtrans = s->trans) {
				case CSST_ANY:
					/* setup a backtrack point */
					path[track++] = (ir << 16) | (is - 1);
					/* no break; */
				case CSST_CHILD:
					node = node->parent;
					is --;
					break;
				case CSST_SIBLING:
					PREV(node);
					if (node == NULL) return 0;
					skip = 1;
				}
				ir --;
				break;
			}
			/* match failed: check if we can get another item on the stack */
			else if (curtrans == CSST_ANY)
			{
				if (is == 0)
				{
					if (track <= 1) return 0;
					track --;
					is = path[track] & 0xffff;
					ir = path[track] >> 16;
				}
				else is --;
				node = stack[is];
			}
			else return 0; /* no match */
		}
	}
	return ir < 0;
}

typedef enum /* keep this private */
{
	CSSArg_NotSet = 0,
	CSSArg_U32    = 1,
	CSSArg_Color  = 2,
	CSSArg_Str    = 3,
	CSSArg_Ptr    = 4,
}	CSSArg_t;

typedef struct CSSArg_t
{
	union {
		CSSColor col;
		STRPTR   str;
		ULONG    u32;
		APTR     ptr;
	} val;
	CSSArg_t type;
}   CSSArg;

/* list is from CSSAttrs.h and is '|' separated and ' ' or '\0' terminated */
static Bool cssFindInList(STRPTR list, STRPTR word, CSSArg * value, Bool deref)
{
	int pos = 0, len = strlen(word);

	while (list)
	{
		STRPTR sep = strpbrk(list, "|= ");
		if (deref)
		{
			Bool ok = False;
			value->type = CSSArg_U32;
			/* note: GRAD is processed elsewhere (cssApplyMultiple) */
			switch (FindInList("POS,FONT,COLOR,URI,INT,DBL", list, sep ? sep - list : 0)) {
			case 0: ok = cssParsePos(&word, &value->val.u32, 1); break;
			case 1: ok = True; value->val.str = word; value->type = CSSArg_Str; break;
			case 2: ok = cssParseColor(&word, &value->val.col); value->type = CSSArg_Color; break;
			case 3: ok = cssParseURI(word, &word, &value->val.str); value->type = CSSArg_Str; break;
			case 4: ok = cssParseNum(&word, &value->val.u32, 4); break;
			case 5: ok = cssParseNum(&word, &value->val.u32, 2);
			}
			if (ok) return True;
			value->type = 0;
		}
		if ((strncasecmp(list, word, len) == 0 && strchr("|= ", list[len])) || strcmp(word, "FIRST") == 0)
		{
			if (! value->type) value->type = CSSArg_U32;
			if (sep && *sep == '=') { sep ++; return cssParsePos(&sep, &value->val.u32, 0); }
			if (deref || ! isupper(*word)) value->val.u32 = pos;
			return True;
		}

		for (pos ++; *list && *list != '|' && *list != ' '; list++);
		if (*list == '|') list ++; else list = NULL;
	}
	return False;
}

/* transform CSS value into pt */
static REAL cssApplyFontSize(SIT_Widget parent, ULONG size)
{
	int  num  = size >> 16;
	int  frac = (size & 0xfffc) >> 2;
	REAL res  = num + frac / 16384.;
	REAL ref  = 16;

	/* so that stuff like: html { font-size: 1.2em } will work */
	if (parent)
		ref = parent->style.font.size;

	switch (size & 3) {
	case 0:  break; /* direct value */
	case 1:  res *= ref; break; /* em */
	case 2:  res *= ref / 100.0f; break; /* percentage */
	default:
		switch (frac) {
		case 1:  res = ref - 4; break; /* smaller */
		case 2:  res = ref + 4; break; /* larger */
		default: res = ref; /* auto */
		}
	}
	/* blurry mess otherwise */
	res = roundf(res);
	return MAX(res,1);
}

static void cssMultAssign(CSSAttr attr, APTR mem)
{
	static uint8_t unassigned[] = {0xff,0xff,0xff,0xff};

	APTR    dest[4];
	ULONG * args;
	int     set, count, sz = attr->sz;

	for (args = &attr->arg1, set = count = 0; *args; count ++, args ++)
		if (memcmp(dest[count] = mem + *args, unassigned, sz))
			set ++;

	switch (set) {
	case 0: memset(dest[0], 0, sz * 4); break;
	case 1: memcpy(dest[1], dest[0], sz); memcpy(dest[2], dest[0], sz * 2); break;
	case 2: memcpy(dest[2], dest[0], sz*2); break;
	case 3: memcpy(dest[3], dest[1], sz);
	}
}

static void cssApplyAttribute(SIT_Widget, STRPTR attr, STRPTR value);

/* handle multiple background, image, color, box-shadow, text-shadow ... */
typedef struct MultiLayer_t *      MultiLayer;
struct MultiLayer_t
{
	MultiLayer next;
	TEXT       payload[0];
};

/*
 * parse multi-param attributes: the CSS spec describe an initial order for parameters,
 * but can be declared in pretty much any order in practice: simply assign each parameter
 * from user to the first valid slot in the CSS spec.
 */
static Bool cssApplyMultipleParam(SIT_Widget node, CSSAttr a, STRPTR value)
{
	static uint8_t szObject[] = {
		0,
		sizeof (struct Background_t)  + sizeof (struct MultiLayer_t),
		sizeof (struct TextShadow_t)  + sizeof (struct MultiLayer_t),
		sizeof (struct BoxShadow_t)   + sizeof (struct MultiLayer_t),
		sizeof (struct BorderImage_t) + sizeof (struct MultiLayer_t),
	};
	MultiLayer layer, list, last;
	Background bg;
	APTR       stack[6];
	CSSAttr    ref;
	CSSArg     args[17]; /* longest attribute (border-image) has 17 arguments */
	STRPTR     p, fmt;
	STRPTR     lineHeight;
	int        i, depth, count, linePos, chrEnd;
	ULONG *    attr;

	#define pop(fmt, attr, ref)         ref = stack[--depth], attr = stack[--depth], fmt = stack[--depth]
	#define push(next, attr, ref, fmt)  stack[depth++] = next, stack[depth++] = attr, stack[depth++] = ref, \
	                                    ref -= atoi(fmt+1), fmt = ref->format, attr = &ref->arg1
	lineHeight = NULL;
	layer = NULL;
	linePos = 0;
	attr = NULL;
	bg = NULL;

	/* multiple background / shadow declaration */
	if (a->objtype > 0)
	{
		chrEnd = ',';
		i = szObject[(uint8_t)a->objtype];
		memset(layer = alloca(i), 0, i);
		if (a->objtype == 1)
			bg = (Background) layer->payload, bg->x = bg->y = AUTOVAL;
	}
	else chrEnd = 0;

	if (a == GET(cssAttrFont))
	{
		/*
		 * of course, there got to be inconsistent syntax: "font" allow specification of font-size and
		 * line-height, but instead of separating them with space, they are separated with /. Change it Q'n'D.
		 */
		p = strchr(value, '/');
		if (p)
		{
			/* hell bent, we are */
			STRPTR end;
			strtod(p+1, &end);
			while (*end && ! cssIsSpace(*end)) end ++;
			i = end - p;
			CopyString(lineHeight = alloca(i), p+1, i);
			memset(p, ' ', i);
			linePos = (p - value) | ((i-1) << 16);
		}
	}
	else if (a == GET(cssAttrBgTransform))
	{
		chrEnd = 0xff;
	}
	else if (a == GET(cssAttrTextShadow))
	{
		if (strcasecmp(value, "none") == 0)
		{
			/* cancel all text-shadows */
			node->style.shadowCount = 0;
			/* dont inherit shadows from parent node either */
			node->style.flags |= LAYF_NoTextShadow;
			return True;
		}
	}
	else if (a == GET(cssAttrBoxShadow))
	{
		if (strcasecmp(value, "none") == 0)
		{
			node->style.boxShadowCount = 0;
			node->style.boxShadowCountInset = 0;
			node->style.flags &= ~CSSF_BOXSHADOW;
			return True;
		}
	}

	for (;;) /* outer loop for multiple background/shadow/... */
	{
		STRPTR minFmt = a->format;
		int    minI   = 0;
		memset(args, 0, sizeof args);
		for (p = value; *p && *p != chrEnd; )
		{
			CSSArg arg = {.type = CSSArg_U32};
			STRPTR searchFor;

			/* spaces are normalized early, but let's be sure */
			p = skipspace(p);

			/* try to identify token pointed by p */
			if (chrEnd == 0xff)
				searchFor = "STR", arg.type = CSSArg_Str, arg.val.str = p, p = strchr(p, 0);
			else if (cssParseColor(&p, &arg.val.col))
				searchFor = "COLOR", arg.type = CSSArg_Color;
			else if (cssParsePos(&p, &arg.val.u32, 1))
				searchFor = "POS";
			else if (cssParseNum(&p, &arg.val.u32, 4))
				searchFor = "INT";
			else if (cssParseURI(p, &p, &arg.val.str))
				searchFor = "URI", arg.type = CSSArg_Str;
			else if (bg && cssParseGradient(p, &p, &bg->gradient))
				searchFor = "GRAD", arg.type = CSSArg_Ptr;
			else if (*p == '/')
			{
				/* field separator */
				STRPTR next = strchr(minFmt, '/');
				/* number of fields we need to skip */
				for (fmt = minFmt, depth = 0, ref = a; fmt < next; )
				{
					STRPTR nextFmt = strchr(fmt, ' ');
					if (*fmt == '-') /* back ref */
					{
						push(nextFmt, attr, ref, fmt);
						continue;
					}
					fmt = nextFmt; minI ++;
					if (!fmt && depth) pop(fmt, attr, ref);
					if (fmt) fmt ++;
				}
				if (next) minFmt = skipspace(next + 1);
				else return False;
				p ++;
				continue;
			}
			else if (*p != '!' && *p != ',')
			{
				/* do not modify <value> (except in a reversible way, like cssParseURI) */
				STRPTR end;
				searchFor = STRDUPA(p);
				if (*p == '\"' || *p == '\'')
					cssTokenizer(searchFor, &end), searchFor ++; /* font-name */
				else
					end = cssIdent(searchFor), *end = 0;
				arg.val.str = p;
				p += end - searchFor;
			}
			else break;

			/* find 'searchFor' in the attribute format */
			for (fmt = minFmt, ref = a, attr = &ref->arg1, i = minI, depth = 0; fmt; )
			{
				STRPTR next = strchr(fmt, ' ');
				int ret;

				if (*fmt == '-') /* back ref */
				{
					push(next, attr, ref, fmt);
					continue;
				}
				if (*fmt == '/')
				{
					fmt = skipspace(fmt+1);
					continue;
				}
				if (args[i].type)
				{
					/* already assigned */
					ret = False;
				}
				else if (*fmt == '|')
				{
					ret = cssFindInList(fmt+1, searchFor, &arg, False);
				}
				else ret = (next ? strncasecmp(fmt, searchFor, next - fmt) == 0 : strcmp(fmt, searchFor) == 0);

				if (ret)
				{
					/* found match */
					args[i] = arg;
					break;
				}
				fmt = next; i ++;
				if (!fmt && depth) pop(fmt, attr, ref);
				if (fmt) fmt ++;
			}

			if (! fmt)
			{
				/* didn't found a match in the format for given user data: cancel evrything */
				if (a == GET(cssAttrFont))
				{
					/* this is actually the font name :-/ */
					args[5].type = CSSArg_Str;
					args[5].val.str = arg.val.str;
				}
				else return False;
			}
		}

		/* assign values */
		int uniform_size = 0;
		for (fmt = a->format, ref = a, attr = &ref->arg1, depth = count = i = 0; fmt; )
		{
			STRPTR next = strchr(fmt, ' ');

			switch (*fmt) {
			case '-': /* back ref */
				push(next, attr, ref, fmt);
				continue;
			case '/': /* field sep */
				fmt += 2;
				continue;
			}
			CSSArg * arg = args + count;
			APTR     mem = (layer ? layer->payload : (STRPTR) node) + *attr;

			if (uniform_size == 0)
				uniform_size = ref->sz;
			else if (uniform_size != ref->sz)
				uniform_size = -1e6;

			switch (arg->type) {
			case CSSArg_NotSet:
				if (*fmt == '|')
					cssFindInList(fmt+1, "FIRST", arg, True); /* take first value of the list */
				else
					arg->val.u32 = ref->defval ? atoi(ref->defval) : 0;
				i --;
				// no break;
			case CSSArg_U32:
				switch (ref->sz) {
				case 1: * (DATA8) mem = arg->val.u32; break;
				case 4: * (uint32_t *) mem = arg->val.u32;
				}
				i ++;
				break;
			case CSSArg_Color:
				memcpy(mem, &arg->val.col, sizeof arg->val.col); i ++;
				break;
			case CSSArg_Str:
				uniform_size = -1e6;
				* (STRPTR *) mem = arg->val.str; i ++;
				if (a->objtype == 1) bg->gradient.colorStop = 0; /* cancel gradient, if image is provided */
				node->style.reflow |= ref->reflow;
				break;
			case CSSArg_Ptr:
				uniform_size = -1e6;
				/* this will clear node->style.background.uriBg */
				* (STRPTR *) mem = NULL; i ++; /* gradient is filled, clear 'uriBg' field */
				node->style.reflow |= ref->reflow;
			}
			arg->val.u32 = *attr;

			if (ref == GET(cssAttrFontSize))
				node->style.font.size = cssApplyFontSize(node->parent, node->style.fontSize);

			fmt = next; attr ++; count ++;
			if (!fmt && depth) pop(fmt, attr, ref);
			if (fmt) fmt ++;
		}
		if (count == 4 && i < 4 && uniform_size > 0 && a != GET(cssAttrTextShadow))
		{
			/* 4 arguments, less than 4 values: don't use 0 as a default value */
			APTR arg1 = node;
			APTR arg2 = arg1 + args[1].val.u32;
			APTR arg3 = arg1 + args[2].val.u32;
			APTR arg4 = arg1 + args[3].val.u32;
			int  sz   = a->sz;
			arg1 += args[0].val.u32;

			switch (i) {
			case 3: memcpy(arg4, arg2, sz); break;
			case 2: memcpy(arg3, arg1, sz); memcpy(arg4, arg2, sz); break;
			case 1: memcpy(arg2, arg1, sz); memcpy(arg3, arg1, sz); memcpy(arg4, arg2, sz); break;
			}
		}
		/* 2 attributes, 1 optional */
		if (count == 2 && i == 1 && uniform_size >= 4)
		{
			memcpy((APTR) node + args[1].val.u32, (APTR) node + args[0].val.u32, a->sz);
		}
		/* multi-layer attribute (background, shadow) */
		if (chrEnd && *p == chrEnd)
		{
			MultiLayer next;
			p ++;
			i = szObject[(uint8_t)a->objtype];
			memset(next = alloca(i), 0, i);
			if (a->objtype == 1)
				bg = (Background) next->payload, bg->x = bg->y = AUTOVAL;
			next->next = layer;
			layer = next;
			value = p;
		}
		else break;
	}
	switch (a->objtype) {
	case 1: /* background */
		for (list = last = layer, count = 0; list; count ++, last = list, list = list->next);
		if (count > node->style.oldBgCount)
		{
			Background back = realloc(node->style.background, count * sizeof *back);
			if (back == NULL) return False;
			node->style.background = back;
			back += node->style.oldBgCount;
			/* clear bg that have been added */
			memset(back, 0, (count - node->style.oldBgCount) * sizeof *back);
			for (node->style.bgCount = node->style.oldBgCount; node->style.bgCount < count; back ++, node->style.bgCount++)
				back->x = back->y = AUTOVAL;
			node->style.oldBgCount = node->style.bgCount;
		}
		else if (count < node->style.bgCount && a != GET(cssAttrBgColor))
		{
			/* need to repeat missing properties */
			for (count = node->style.bgCount - count, list = layer; count > 0; count --, list = list->next)
			{
				MultiLayer next;
				memcpy(next = alloca(szObject[1]), list, szObject[1]);
				last->next = next;
				next->next = NULL;
				last = next;
			}
		}
		else node->style.bgCount = count;
		bg = node->style.background;

		/* background: copy whole structure, bg-image: need to copy gradient too */
		i = a == GET(cssAttrBgImage)    ? 0 :
		    a != GET(cssAttrBackground) ? 1 : 2;

		for (list = layer; list; list = list->next, bg ++)
		{
			switch (i) {
			case 0:
				memcpy(&bg->gradient, list->payload + offsetp(Background, gradient), sizeof (Gradient));
				// no break;
			case 1:
				/* partial copy */
				if (a == GET(cssAttrBgPos))
				{
					ULONG * xy = (ULONG *) (list->payload + cssattrs[0].arg1);
					bg->x = xy[0];
					bg->y = xy[1];
				}
				else if (a == GET(cssAttrBgModColor))
				{
					Background src = (Background) list->payload;
					/* only overwrite if not fully transparent */
					if (src->gradient.colors[0].rgba[3] > 0)
						memcpy((STRPTR) bg + a->arg1, list->payload + a->arg1, a->sz);
				}
				else for (attr = &a->arg1; *attr; attr ++)
				{
					memcpy((STRPTR) bg + *attr, list->payload + *attr, a->sz);
				}
				break;
			case 2: /* overwrite all but last field */
				memcpy(bg, list->payload, offsetp(Background, image));
			}
		}
		node->style.flags |= CSSF_BACKGROUND;
		break;

	case 2: /* text-shadow */
		node->style.flags |= CSSF_TEXTSHADOW;
		for (list = layer, count = 0; list; list = list->next)
		{
			TextShadow shadow = (TextShadow) layer->payload;
			if (shadow->pos.XYfixed[0] == 0 && shadow->pos.XYfixed[1] == 0 && shadow->blurFixed == 0)
				/* user wants to cancel text-shadow */
				shadow->color.val = 0;
			else
				count ++;
		}
		node->style.shadowCount = count;
		if (count > 0)
		{
			TextShadow shadow;
			node->style.shadow = realloc(node->style.shadow, count * sizeof *shadow);
			for (list = layer, count = 0; list; list = list->next, count ++)
			{
				shadow = (TextShadow) list->payload;
				if (shadow->color.val > 0)
					memcpy(node->style.shadow + count, shadow, sizeof *shadow);
			}
		}
		else if (node->style.shadow)
		{
			free(node->style.shadow);
			node->style.shadow = NULL;
		}
		break;

	case 3: /* box-shadow -- looks similar to text-shadow but not quite */
		node->style.flags |= CSSF_BOXSHADOW;
		for (list = layer, count = 0; list; list = list->next)
		{
			BoxShadow shadow = (BoxShadow) layer->payload;
			if (shadow->XYSfixed[0] == 0 && shadow->XYSfixed[1] == 0 && shadow->blurFixed == 0 && shadow->XYSfixed[2] == 0)
				/* user wants to cancel box-shadow */
				shadow->color.val = 0;
			else
				count ++;
		}
		node->style.boxShadowCount = count;
		if (count > 0)
		{
			BoxShadow shadow;
			node->style.boxShadow = realloc(node->style.boxShadow, count * sizeof *shadow);
			for (list = layer, count = 0; list; list = list->next)
			{
				shadow = (BoxShadow) list->payload;
				if (shadow->color.val > 0)
				{
					node->layout.flags |= shadow->inset ? LAYF_HasInset : LAYF_HasBoxShadow;
					memcpy(node->style.boxShadow + count, shadow, sizeof *shadow);
					count ++;
				}
			}
		}
		else if (node->style.boxShadow)
		{
			free(node->style.boxShadow);
			node->style.boxShadow = NULL;
		}
		break;

	case 4: /* border-image */
		{
			BorderImg src = (BorderImg) layer->payload;
			BorderImg dst = node->style.borderImg;
			if (dst == NULL)
				dst = node->style.borderImg = calloc(sizeof *src, 1);

			switch (a->inherit >> 2) {
			case cssAttrBorderImgSrc:    dst->source     = src->source; break;
			case cssAttrBorderImgSlice:  dst->slice      = src->slice; break;
			case cssAttrBorderImgWidth:  dst->widthFixed = src->widthFixed; break;
			case cssAttrBorderImgOutset: dst->offFixed   = src->offFixed; break;
			case cssAttrBorderImgRepeat: dst->repeatH    = src->repeatV; dst->repeatV = src->repeatV; break;
			case cssAttrBorderImg:
				cssMultAssign(GET(cssAttrBorderImgSlice),  src);
				cssMultAssign(GET(cssAttrBorderImgWidth),  src);
				cssMultAssign(GET(cssAttrBorderImgOutset), src);
				if (src->repeatH == 0) src->repeatH = BorderImageStretch;
				if (src->repeatV == 0) src->repeatV = src->repeatH;
				memcpy(dst, src, sizeof *dst);
			}
		}
	}

	if (lineHeight) /* from font property */
	{
		/* restore what we modified so that rule can be reused */
		i = linePos & 0xffff;
		value[i] = '/';
		memcpy(value + i + 1, lineHeight, linePos >> 16);
		/* need to apply line-height separately */
		cssApplyAttribute(node, "line-height", lineHeight);
	}

	return True;
}

/*
 * Apply CSS attribute and store it into our style struct. This is the core
 * function that transfert CSS values into DOMNode. This function is close
 * to optimal complexity.
 */
static void cssApplyAttribute(SIT_Widget node, STRPTR attr, STRPTR value)
{
	CSSAttr a   = cssAttrSearch(attr);
	int     arg = 0;
	int     nbc = 0;
	int     radiusMinor = 0;
	STRPTR  p;

	/* unknown attributes are silently ignored */
	if (a == NULL || ! IsDef(a->format)) return;

	if ((a->inherit&3) && (nbc = a->inherit>>2))
		node->style.inheritSet |= 1 << nbc;

	/* multiple parameter attributes will be handled differently than single parameter attributes */
	for (nbc = 1, p = a->format; *p; nbc += *p == ' ', p ++);

	if (a == GET(cssAttrLineHeight))
	{
		double val = strtod(value, &p);
		if (p > value && *p == 0)
		{
			/* This is shortcut for % unit */
			sprintf(value = alloca(32), "%.2f%%", val * 100);
		}
	}

	if (a == GET(cssAttrBorderSpacing) && node->type != SIT_LISTBOX)
		return;

	if (a == GET(cssAttrCaret) && node->type != SIT_EDITBOX)
		return;

	if (a == GET(cssAttrBackground) && strcasecmp(value, "none") == 0)
	{
		/* "background: none" is translated into "transparent none" to be sure background-color is also reset */
		value = "transparent none";
		value = STRDUPA(value);
	}

	if (a == GET(cssAttrBorderRadius))
	{
		/* check if the declaration include major and minor axis */
		p = strchr(value, '/');
		radiusMinor = 1;
		if (p)
		{
			arg = p - value + 1;
			STRPTR dup = alloca(arg);
			CopyString(dup, value, arg);
			value = dup;
			for (p ++; *p && cssIsSpace(*p); p ++);
			if (*p) cssApplyAttribute(node, "border-radius-minor", p), radiusMinor = 0;
		}
	}

	/* explicit inherit */
	if (strcasecmp(value, "inherit") == 0)
	{
		SIT_Widget parent = node->parent;
		CSSAttr    ref;
		ULONG *    args;
		APTR       stack[6];

		if (parent == NULL) return; /* has to be set */

		#define depth    arg
		for (p = a->format, ref = a, args = &ref->arg1, depth = 0; p; )
		{
			STRPTR next = strchr(p, ' ');

			if (*p == '-') { /* back ref */
				push(next, args, ref, p);
				continue;
			}
			int offset = *args;
			memcpy((APTR) node + offset, (APTR) parent + offset, a->sz);
			p = next; args ++;
			if (!p && depth) pop(p, args, ref);
			if (p) p ++;
		}
		#undef depth
	}
	else if (strcasecmp(value, "initial") == 0)
	{
		/*
		 * CSS3: actually there is nothing more to do, because the first step of CSS apply will
		 *       set all attributes to their initial value. Inherited attributes will be set
		 *       only if attribute has not been changed with a CSS rule.
		 */
	}
	else if (nbc > 1 || a->objtype > 0)
	{
		cssApplyMultipleParam(node, a, value);
	}
	else /* only one argument - no back reference */
	{
		APTR mem = (STRPTR) node + a->arg1;
		Bool ok  = True;

		p = a->format;
		if (*p == 0)
		{
			/* unknown/unsupported format */
			ok = False;
		}
		else if (*p == '|')
		{
			CSSArg cssarg = {.type = CSSArg_NotSet};
			ok = cssFindInList(p + 1, value, &cssarg, True) + 1;
			switch (cssarg.type) {
			case CSSArg_NotSet:
			case CSSArg_Ptr: break; // only background-image uses this
			case CSSArg_U32:
				if (a->sz == 1) *(DATA8) mem = cssarg.val.u32 > 255 ? 255 : cssarg.val.u32;
				else * (ULONG *) mem = cssarg.val.u32;
				break;
			case CSSArg_Str:   memcpy(mem, &cssarg.val.str, sizeof cssarg.val.str); break;
			case CSSArg_Color: memcpy(mem, &cssarg.val.col, sizeof cssarg.val.col);
			}
		}
		else if ('A' <= *p && *p <= 'Z')
		{
			switch (FindInList("COLOR,POS,URI,FONT,STR,INT,DBL", p, 0)) {
			case 0:  ok = cssParseColor(&value, mem); break;
			case 1:  ok = cssParsePos(&value, mem, 1); break;
			case 2:  ok = cssParseURI(value, &value, mem); break;
			case 3:  ok = True; node->style.font.family = value; break;
			case 4:  ok = True; * (STRPTR *) mem = value; break;
			case 5:  ok = cssParseNum(&value, mem, a->sz); break;
			case 6:  ok = cssParseNum(&value, mem, 2); break;
			default: ok = False;
			}
		}

		if (! ok) return;
	}

	/* some attribute need special processing */
	if (a == GET(cssAttrBorder))
	{
		/* copy attributes styles to right, left and bottom */
		node->style.borderRight = node->style.borderBottom =
		node->style.borderLeft  = node->style.borderTop;
	}
	else if (a == GET(cssAttrFontSize))
	{
		node->style.font.size = cssApplyFontSize(node->parent, node->style.fontSize);
	}
	else if (a == GET(cssAttrFontWeight))
	{
		/* normal|bold|bolder|lighter|100|200|300|400|500|600|700|800|900 */
		switch (arg = node->style.font.weight) {
		case  1: arg = node->parent->style.font.weight + 400; break;
		case -1: arg = node->parent->style.font.weight - 400;
		}
		if (arg < 100) arg = 100;
		if (arg > 900) arg = 900;
		node->style.font.weight = arg;
	}
	else if (a == GET(cssAttrTransform))
	{
		p = node->style.transform;
		if (p && (strcasecmp(p, "none") == 0 || p[0] == 0))
			node->style.transform = NULL;
	}
	if (radiusMinor)
		node->style.minorRadius = node->style.majorRadius;
}

/* set initial style for given node */
void cssSetDefault(SIT_Widget node)
{
	CSSAttr a, font;
	SIT_Widget parent = node->parent;

	font = GET(cssAttrFontSize);

	/* do not clear allocated fields */
	uint8_t oldBg    = node->style.bgCount;
	int     oldflags = node->style.flags;
	uint8_t oldFlow  = node->style.overflow;

	memset(&node->style, 0, offsetof(struct Style_t, shadow));
	node->style.font.handle = -1;
	node->style.flags = oldflags;
	node->style.oldBgCount = oldBg;

	if (! parent)
		node->style.font.size = sit.defFontHeight;

	for (a = cssattrs; a < EOT(cssattrs); a ++)
	{
		if (a->defval && a->objtype == 0)
		{
			ULONG * mem = (APTR) node + a->arg1;
			STRPTR  val = a->defval;
			if (*val == '=')
				val ++, cssParsePos(&val, mem, 0);
			else if (a->format[0] == 'D')
				*(REAL*)mem = strtod(val, NULL);
			else if (a->sz == 1)
				*(DATA8)mem = strtol(val, NULL, 10);
			else
				*mem = strtol(val, NULL, 10);
		}
		if ((a->inherit&3) && parent)
		{
			if (a != font)
			{
				int off = a->arg1;
				if (off == 0) break;
				memcpy((APTR) node + off, (DATA8) parent + off, a->sz);
			}
			else /* font-size: we want pixels in style.font.size */
				node->style.font.size = parent->style.font.size;
		}
	}
	while (oldBg > 0)
	{
		oldBg --;
		Background bg = node->style.background + oldBg;

		if (bg->image) SIT_UnloadImg(bg->image);
		memset(bg, 0, offsetp(Background, dim));
	}

	if (node->type == SIT_EDITBOX)
		((SIT_EditBox)node)->caret.val = 0;
	if (parent == NULL)
	{
		node->style.font.size = cssApplyFontSize(NULL, node->style.fontSize);
		memcpy(&node->style.color, &black, sizeof black);
	}
	node->layout.flags &= ~LAYF_HasBoxShadow;
	node->style.overflow = oldFlow; /* hmm, not set through CSS :-/ */

	GetSelectionColor(node->style.fgSel.rgba, node->style.bgSel.rgba);
}

/*
 * we need to gather all styles inside a list before applying them, because
 * they are not completely sorted yet (!important not taken into account yet)
 */
static void cssAddStyles(vector v, STRPTR * styles, int count, int specif, int specif_important)
{
	static int i = 0; /* used to sort rules with same specif: last declared takes precedence over first */
	while (count > 0 && styles[0])
	{
		/* filter !important rules */
		STRPTR sep = strchr(styles[1], '!');

		if (sep)
		{
			/* anything other than "important" must discard the entire declaration */
			if (strcasecmp(skipspace(sep+1), "important"))
			{
				styles += 2;
				count  --;
				continue;
			}
			specif += specif_important << 24;
		}

		CSSStyle style = vector_nth(v, v->count);

		style->specif = specif;
		style->attr   = styles[0];
		style->value  = styles[1];
		style->order  = i ++;
		styles += 2;
		count  --;
	}
}

/* sort css property/value pairs in increasing specifity (qsort callback) */
static int cssSortStyle(const void * e1, const void * e2)
{
	#define css1  ((CSSStyle)e1)
	#define css2  ((CSSStyle)e2)
	int diff = css1->specif - css2->specif;
	if (diff) return diff;
	else return css1->order - css2->order;
	#undef css1
	#undef css2
}

static void cssPostProcess(SIT_Widget node)
{
	if (node->style.decoColor.val == 0)
		node->style.decoColor = node->style.color;

	/* set border color to the same value than "color" attribute (if border has not been set) */
	{
		Border * border;
		int i;
		for (border = &node->style.borderTop, i = 0; i < 4; i ++, border ++)
			if (border->color.val == 0) border->color = node->style.color;
	}

	if (node->style.tabSize < 1)
		node->style.tabSize = 1;

	if (node->type == SIT_EDITBOX)
	{
		SIT_EditBox edit = (SIT_EditBox) node;
		if (edit->caret.val == 0)
			edit->caret = node->style.color;
	}

	/* load image's data */
	if (node->style.bgCount > 0)
	{
		Background bg;
		int count;
		for (count = node->style.bgCount, bg = node->style.background; count > 0; count --, bg ++)
		{
			CSSImage img = cssAddImage(bg->uriBg, False);

			if (img)
			{
				bg->image      = img;
				bg->dim.width  = img->width;
				bg->dim.height = img->height;
			}
			else bg->image = NULL;
		}
	}

	/* premultiply opacity */
	if (node->style.opacity > 1) node->style.opacity = 1;
	if (node->style.opacity < 0) node->style.opacity = 0;
	if (node->parent)            node->style.opacity *= node->parent->style.opacity;
	else sit.defFontHeight = node->style.font.size;
}

/* check if geometric reflow is required after some CSS properties have been changed */
static int cssCheckChange(SIT_Widget node, Style * old, CSSAttr a)
{
	CSSAttr ref;
	STRPTR  fmt;
	APTR    stack[6];
	int     depth;
	ULONG * attr;
	int     ret;

	if (! a || a->objtype > 0) return 0;
	if (a == GET(cssAttrBorderSpacing) && node->type != SIT_LISTBOX) return 0;
	for (fmt = a->format, ref = a, attr = &ref->arg1, depth = 0, ret = 0; fmt; )
	{
		STRPTR next = strchr(fmt, ' ');

		if (*fmt == '-') /* unset back ref */
		{
			push(next, attr, ref, fmt);
			continue;
		}
		if (ref->reflow && memcmp((STRPTR) node + *attr, (STRPTR) old + *attr - offsetp(SIT_Widget, style), ref->sz))
			ret |= ref->reflow;

		fmt = next; attr ++;
		if (!fmt && depth) pop(fmt, attr, ref);
		if (fmt) fmt ++;
	}
	return ret;
}

static uint32_t cssCRC32(vector styles)
{
	CSSStyle style;
	uint32_t crc = 0, i;

	for (i = styles->count, style = vector_first(*styles); i > 0; i --, style ++)
	{
		crc = crc32(crc32(crc, style->attr, -1), style->value, -1);
	}
	return crc;
}

/* css checksum not changed, but styles can still be changed due to inheritance */
static int cssGetInherited(SIT_Widget node)
{
	SIT_Widget parent = node->parent;
	uint16_t   set;
	uint8_t    i, ret;

	if (! parent) return 0;

	/* mark style that have been referenced */
	for (i = 1, ret = 0, set = node->style.inheritSet>>1; i < DIM(inherited); i ++, set>>=1)
	{
		if (set & 1) continue;
		CSSAttr a = cssattrs + inherited[i];
		if (a != GET(cssAttrFontSize))
		{
			int off = a->arg1;
			if (memcmp((APTR) node + off, (DATA8) parent + off, a->sz))
			{
				memcpy((APTR) node + off, (DATA8) parent + off, a->sz);
				ret |= a->reflow | ReflowForce;
			}
		}
		else if (node->style.font.size != parent->style.font.size)
		{
			node->style.font.size = parent->style.font.size;
			ret |= a->reflow | ReflowForce;
		}
	}
	return ret;
}

/*
 * cssApply: apply CSS styles to given SIT_Widget. This is a O(MxN) algorithm, not
 * the most efficient (where M = number of nodes, and N = number of rules).
 * Ideally, it can be done in O(MxD), where D = depth of the longest CSS selector.
 *
 * XXX don't really care, we can cache the result of this function :-/
 */
int cssApply(SIT_Widget node)
{
	Style        oldStyles;
	CSSRule      rule;
	CSSStyle     style;
	SIT_Widget   parent;
	SIT_Widget * stack;
	uint8_t      applied, level, state;
	int          i;

	cssStyles.count = 0;

	for (level = 0, parent = node; parent; level ++, parent = parent->parent);
	stack = alloca(sizeof *stack * level);
	for (i = level-1, parent = node; parent; stack[i] = parent, i --, parent = parent->parent);

	cssStyles.count = 0;
	applied = node->style.flags & CSSF_APPLIED;
	state   = node->state;
	state = state2crc[state];
	if (node->enabled == 0)
		state = 7;

	for (rule = (CSSRule) sit.theme, i = 0; ; rule = (CSSRule) (sit.theme + rule->next), i ++)
	{
		if (cssMatchSelector(stack, level, rule))
			cssAddStyles(&cssStyles, (STRPTR *) (sit.theme + rule->styles), rule->nbstyles, rule->specif, 1);
		if (rule->next == 0) break;
	}

	if (node->inlineStyles)
		cssAddStyles(&cssStyles, node->inlineStyles, 1e6, 0xff<<16, 2);

	qsort(cssStyles.buffer, cssStyles.count, sizeof *style, cssSortStyle);

	i = cssCRC32(&cssStyles);
	if (i == node->layout.curCRC32 && applied)
	{
		node->layout.crc32[state] = i;
		//fprintf(stderr, "%s[%d]: same style as before: keep existing\n", node->name, state);
		return cssGetInherited(node);
	}

	/* if active state is same as not active, keep hover styles */
	if (state == 2 && i == node->layout.crc32[0] && node->layout.curCRC32 == node->layout.crc32[1])
	{
		return cssGetInherited(node);
	}

	memcpy(&oldStyles, &node->style, sizeof oldStyles);
	cssSetDefault(node);
	node->layout.curCRC32 = node->layout.crc32[state] = i;
	node->layout.left = 0;
	node->layout.top = 0;

	#if 0
	if (strcmp(node->name, "fname") == 0)
	{
		fprintf(stderr, "*** styles for state %d [%d] = %x\n", node->state, state, i);
		for (i = 0, style = vector_first(cssStyles); i < cssStyles.count; i ++, style ++)
			fprintf(stderr, "    %s: %s\n", style->attr, style->value);
	}
	#endif

	/* DEBUG: ((CSSStyle)cssStyles.buffer) */
	for (i = 0, style = vector_first(cssStyles); i < cssStyles.count; i ++, style ++)
	{
		STRPTR sep = strchr(style->value, '!');
		TEXT   old = 0;
		if (sep) sep -= (sep[-1] == ' '), old = *sep, *sep = 0;
		cssApplyAttribute(node, style->attr, style->value);
		if (sep) *sep = old;
	}

	/* check for layout change: has to be done after everything has been processed */
	if (applied)
	{
		for (i = 0, style = vector_first(cssStyles); i < cssStyles.count; i ++, style ++)
			node->style.reflow |= cssCheckChange(node, &oldStyles, cssAttrSearch(style->attr));
	}
	cssPostProcess(node);

	node->style.flags |= CSSF_APPLIED;
	for (parent = node; parent->type == SIT_HTMLTAG; parent = parent->parent);
	sit.dirty |= parent->visible;

	return node->style.reflow;
}

/* clear all styles applied to this node */
void cssClear(SIT_Widget node)
{
	cssSetDefault(node);

	if (node->inlineStyles)
		free(node->inlineStyles), node->inlineStyles = NULL;

	SIT_FreeCSS(node);
	vector_init(node->layout.wordwrap, sizeof (struct WordWrap_t));
	memset(&node->style, 0,sizeof node->style);
	memset(node->layout.crc32, 0xff, sizeof node->layout.crc32);
}

Bool cssCRCChanged(SIT_Widget node)
{
	uint8_t old = state2crc[node->oldState];
	uint8_t pos, state = node->state;
	if (node->flags & SITF_RecalcStyles) return 1;
	if (node->enabled == 0)
		pos = 7;
	else
		pos = state2crc[state];
	return node->layout.crc32[old] != node->layout.crc32[pos];
}

/* returns: 0: nothing, 1: file uri, 2: data uri */
static int cssGetURI(STRPTR uri, STRPTR out, int max)
{
	if (! IsDef(uri)) return 0;
	if (strncasecmp(uri, "data:", 5) == 0) return 2;

	while (*uri == '/' || *uri == '\\') uri ++;
	CopyString(out, uri, max);
	return 1;
}

static const char cd64[] = "|\0\0\0}rstuvwxyz{\0\0\0\0\0\0\0>?@ABCDEFGHIJKLMNOPQRSTUVW\0\0\0\0\0\0XYZ[\\]^_`abcdefghijklmnopq";

/* data:[<MIME-type>][;charset=<encoding>][;base64],<data> */
DATA8 cssParseDataURI(DATA8 mem, int * len, int type /*0:auto, 1:b64, 2:uri*/)
{
	DATA8 p, io, out;
	Bool  b64 = False;

	if (type == 0)
	{
		p = strchr(mem, ',');
		if (p == NULL) return NULL;
		mem += 5;

		/* need to find the encoding (base64 or URL encoding) */
		for (*p++ = 0; mem; )
		{
			DATA8 next = strchr(mem, ';');
			if (next) *next++ = 0;
			if (strcasecmp("base64", mem) == 0) { b64 = True; break; }
			mem = next;
		}
		io = p;
	}
	else p = io = mem, b64 = type == 1;

	if (b64)
	{
		uint8_t in[4];
		int     v, i;

		for (out = p; *p; )
		{
			/* read a group of 4 bytes */
			for (i = 0; i < 4 && *p; )
			{
				if ((v = *p))
				{
					if (v == '%' && p[1] && p[2]) /* URI encoding */
					{
						v = (HexToBin(p[1]) << 4) | HexToBin(p[2]);
						p += 2;
					}
					in[i] = ((v < 43 || v > 122) ? 0 : cd64[v - 43]);
					if (in[i]) in[i] -= 62, i ++;
					p ++;
				}
				else in[i++] = 0;
			}
			out[0] = in[0] << 2 | in[1] >> 4;
			out[1] = in[1] << 4 | in[2] >> 2;
			out[2] = ((in[2] << 6) & 0xc0) | in[3];
			out += 3;
		}
	}
	else /* simple URI encoding */
	{
		for (out = p; *p; out ++)
		{
			if (*p == '%' && p[1] && p[2])
			{
				*out = (HexToBin(p[1]) << 4) | HexToBin(p[2]);
				p += 3;
			}
			else *out = *p, p ++;
		}
		*out = 0;
	}
	if (len) *len = out - io;
	return io;
}

/* add image (background-image and img.src) into a cache */
CSSImage cssAddImage(STRPTR uri, Bool mask)
{
	CSSImage img;
	uint32_t crc;
	TEXT     path[256];
	int      len;
	int      flags = 0;
	int      handle = 0;

	if (uri == NULL) return NULL;

	if (strncasecmp(uri, "url(", 4) == 0)
	{
		/* extract url parameter */
		STRPTR end;
		if (cssParseURI(uri, &end, &uri))
		{
			len = end - uri;
			end = alloca(len);
			CopyString(end, uri+4, len);
			uri = end;

			if (*uri != '\"' && *uri != '\'')
			{
				for (end = uri; *end && *end != ')'; end ++);
				*end = 0;
			}
			else cssTokenizer(uri, &end), uri ++;
		}
		else return NULL;
	}

	if (strncmp(uri, "mask:", 5) == 0)
		uri += 5, mask = True;
	else if (strncmp(uri, "id(", 3) == 0)
		handle = strtoul(uri+3, NULL, 10);
	else if (strncmp(uri, "nearest:", 8) == 0)
		uri += 8, flags = 1;

	crc = crc32(0, uri, -1);
	/* only used by data uri, there is at least 10 or so bytes before data start */
	if (memcmp(uri, "\1\0c", 4) == 0)
		memcpy(&crc, uri+4, 4);

	for (img = HEAD(sit.images); img && img->crc32 != crc; NEXT(img));

	if (img)
	{
		if (handle > 0)
			SIT_GetImageSize(img);
		img->usage ++;
		return img;
	}
	if (*uri == 1) return NULL;

	img = calloc(sizeof *img, 1);
	img->bpp = mask ? 8 : 32;
	img->crc32 = crc;

	#ifdef DEBUG_SIT
	fprintf(stderr, "adding image %s\n", uri);
	#endif

	switch (cssGetURI(uri, path, sizeof path)) {
	case 1:
		if (handle > 0)
		{
			img->handle = handle;
			img->externAlloc = 1;
			SIT_GetImageSize(img);
		}
		else
		{
			cssParseDataURI(path, NULL, 2);

			if (! SIT_LoadImg(img, path, 0, flags)) {
				free(img); return NULL;
			}
		}
		img->bitmap = uri;
		break;
	case 2:
		img->bitmap = cssParseDataURI(uri, &len, 0);
		SIT_LoadImg(img, img->bitmap, len, flags);
		/* URI will be modified by cssParseDataURI(). We need to keep a link if it is further referenced */
		/* This hack is self-contained in this function, so we can afford it */
		memcpy(uri, "\1\0c", 4);
		memcpy(uri+4, &crc, 4);
		break;
	default:
		return NULL;
	}
	img->usage ++;
	ListAddHead(&sit.images, &img->node);
	return img;
}

/* also keep a cache of gradients */
CSSImage cssAddGradient(Gradient * grad, int w, int h, REAL fh)
{
	struct CSSImage_t init = {.width = w, .height = h, .bpp = 32};
	CSSImage img;
	uint32_t crc;
	int      old = grad->wxh;

	/* linear gradients use a smaller bitmap than radial */
	gradientGetParam(&init, grad);
	grad->wxh = 0;
	crc = crc32(crc32(0, (DATA8) &init.width, 2*sizeof (int)), (DATA8) grad, sizeof *grad);
	grad->wxh = old;

	for (img = HEAD(sit.images); img && img->crc32 != crc; NEXT(img));

	if (img == NULL && 0 < w && w < 14400 && 0 < h && h < 14400)
	{
		img = calloc(sizeof *img, 1);
		memcpy(img, &init, sizeof *img);
		ListAddHead(&sit.images, &img->node);
		img->crc32 = crc;
		/* will alloc the bitmap */
		if (grad->corner == 255)
			/* radial gradient */
			gradientDrawRadial(img, grad, fh);
		else
			gradientDrawLinear(img, grad, fh);
		SIT_LoadImg(img, NULL, 0, 0);
		/* a copy will be made by opengl */
		free(img->bitmap);
		img->bitmap = "grad";
	}
	if (img) img->usage ++;
	return img;
}
