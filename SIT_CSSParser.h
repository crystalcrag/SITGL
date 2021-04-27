/*
 * SIT_CSSParser.h : function and datatypes for parsing CSS rules and stylesheet
 *
 * Written by T.Pierron, Apr 2009.
 */

#ifndef	SIT_CSSPARSER_H
#define	SIT_CSSPARSER_H

#include "SIT_P.h"

typedef struct CSSStyle_t *     CSSStyle;
typedef struct CSSRule_t *      CSSRule;
typedef struct CSSSel_t *       CSSSel;
typedef struct CSSImage_t *     CSSImage;


void   cssInit(void);
void   cssFreeGlobals(void);
ULONG  cssFromUnit(int unit, REAL val);
Bool   cssParseNthChild(STRPTR fmt, int * ra, int * rb);
Bool   cssParseColor(STRPTR * value, CSSColor * color);
Bool   cssParsePos(STRPTR * value, ULONG * pos, int withUnit);
Bool   cssParseNum(STRPTR * value, APTR num, int type);
Bool   cssParseAngle(STRPTR fmt, STRPTR * pend, uint16_t * mem);
Bool   cssParseGradient(STRPTR fmt, STRPTR * pend, Gradient * mem);
Bool   cssParseURI(STRPTR fmt, STRPTR * pend, STRPTR * mem);
Bool   cssParse(STRPTR css, int isPath);
STRPTR cssParseStyles(STRPTR start, uint16_t offsets[2]);
ULONG  cssColorSearch(STRPTR name);

int    cssApply(SIT_Widget);
void   cssClear(SIT_Widget);
Bool   cssCRCChanged(SIT_Widget);
void   cssSetDefault(SIT_Widget);
int    cssTokenizer(STRPTR start, STRPTR * end);
void   cssDumpRules();
DATA8  cssIdent(DATA8);
int    cssIsSpace(int chr);

CSSImage cssAddImage(STRPTR uri, Bool mask);
CSSImage cssAddGradient(Gradient * grad, int w, int h, REAL fh);

void gradientGetParam(CSSImage, Gradient * grad);
Bool gradientDrawRadial(CSSImage, Gradient * grad, REAL ratio);
Bool gradientDrawLinear(CSSImage img, Gradient * grad, REAL ratio);
void gradientGetCenter(Gradient * grad, int info[4], int width, int height, REAL ratio);

int    HexToBin(TEXT c);
STRPTR skipspace(STRPTR);

enum /* CSS lexical tokens */
{
	CSST_SPACES,
	CSST_ATRULE,
	CSST_STRING,
	CSST_SELECTOR,
	CSST_UNKNOWN
};

typedef enum                /* possible values for 'CSSSel.type' */
{
	CSSR_NONE,
	CSSR_TAG,
	CSSR_PSEUDO,
	CSSR_CLASS,
	CSSR_ID,
	CSSR_ATTR,
	CSSR_ATTREQV,
	CSSR_ATTRLIST,
	CSSR_ATTRDASH
}	CSSRuleType;

typedef enum                /* possible values for 'CSSSel.trans' */
{
	CSST_CURRENT,
	CSST_END,
	CSST_ANY,
	CSST_CHILD,
	CSST_SIBLING,
}	CSSRuleTrans;

struct CSSRule_t
{
	uint16_t next;
	uint8_t  nbsel;
	uint8_t  nbstyles;
	int      specif;
	uint16_t styles;

	#if 0
	ListNode node;
	CSSSel   selector;      /* CSS Selector (NULL for inline) */
	uint16_t nbsel;         /* nb of items in selector[] */
	uint8_t  shared;        /* True if 'styles' is shared by another rule */
	int      specif;        /* weight of rule according to selector */
	STRPTR * styles;        /* pairs of attribute/value, NULL terminated */
	#endif
};

struct CSSSel_t             /* selector item */
{
	STRPTR       item;
	CSSRuleType  type:8;    /* no need to alloc 4 bytes for these */
	CSSRuleTrans trans:8;
};

struct CSSStyle_t
{
	STRPTR attr;
	STRPTR value;
	int    specif;
	int    order;
};

struct CSSImage_t
{
	ListNode node;
	int      width;
	int      height;
	uint8_t  bpp;         /* bits per pixel (8, 24 or 32) */
	uint8_t  usage;
	uint8_t  stretch;
	uint8_t  externAlloc; /* 1 if handle was not alloced by SIT (don't dispose of handle) */
	uint32_t crc32;
	DATA8    bitmap;
	float    angle;       /* rotate before draw (rad) */
	float    rect[4];
	int      handle;      /* NVG handle of image */
};

#define	AUTOVAL      3

/* convert a floatting point in our fixed point system (assuming ps pt unit) */
#define	PT(val)   (((short)(val) << 16) | ((int) (((val) - (int)(val)) * 16384) << 2))

#endif
