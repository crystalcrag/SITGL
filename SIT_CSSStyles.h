/*
 * SIT_CSSStyles.h : datatypes to hold CSS styles per SIT_Widget.
 *
 * written by T.Pierron, May 2009.
 */

#ifndef	SIT_CSSSTYLES_H
#define	SIT_CSSSTYLES_H

#include "UtilityLibLite.h"

typedef struct Style_t         Style;
typedef struct Layout_t        Layout;
typedef struct Border_t        Border;
typedef struct Background_t *  Background;
typedef struct Font_t          Font;
typedef struct Text_t          Text;
typedef struct Size_t          Size;
typedef struct SizeF_t         SizeF;
typedef struct BoxF_t          BoxF;
typedef struct Box_t           Box;
typedef struct Gradient_t      Gradient;
typedef struct WordWrap_t *    WordWrap;
typedef struct LineHdr_t *     LineHdr;
typedef struct RectF_t         RectF;
typedef struct PointF_t        PointF;
typedef struct TextShadow_t *  TextShadow;
typedef struct BoxShadow_t *   BoxShadow;
typedef struct BorderImage_t * BorderImg;
typedef struct BImgCache_t     BImgCache;
typedef struct CSSColor_t      CSSColor;
typedef float                  REAL;  /* precision used by layout */


struct CSSColor_t
{
	union {
		uint8_t rgba[4];
		uint32_t val;
	};
};

struct Border_t
{
	ULONG    width;
	uint8_t  style;
	CSSColor color;
};

typedef struct
{
	uint8_t rgba[4];
	ULONG   pos;
}   CStopBuf;

typedef CStopBuf *  ColorStop;

struct Gradient_t
{
	uint8_t  colorStop;     /* number of stops (max 8) */
	uint8_t  flags;         /* predefined corner [1-16] or 0 to use <orient> field */
	uint16_t orient;        /* angle [0- 65535] mapped to [0 - 360[deg */
	union {
		float rect[4];      /* linear-gradient parameters */
		struct {
			ULONG cx, cy;
			ULONG rx, ry;
		} radial;           /* radial-gradient param */
	};
	ULONG    wxh;           /* gradient was render for this dimension */
	CStopBuf colors[8];     /* pre-alloc 8 color stops (pairs of color+pos) */
};

#define GRADIENT_CORNER     31
#define GRADIENT_REPEAT     0x80
#define GRADIENT_RADIAL     0x40

struct RectF_t
{
	REAL left;
	REAL top;
	REAL width;
	REAL height;
};

struct Background_t
{
	uint8_t  id, attachment, repeat, clip;
	CSSColor color;
	STRPTR   uriBg;
	ULONG    x, y;
	Gradient gradient;
	APTR     image;      /* CSSImage */
	STRPTR   transform;
	ULONG    bgw, bgh;
	RectF    dim;
};

struct Font_t
{
	STRPTR  family;
	REAL    size;
	ULONG   weight;
	uint8_t variant;
	uint8_t decoration;
	uint8_t italic;
	int     handle;
};

struct Text_t
{
	uint8_t align;
	uint8_t transform;
	uint8_t padding[2];
};

struct TextShadow_t
{
	union {
		ULONG XYfixed[2];
		REAL  XYfloat[2];
	} pos;
	union {
		ULONG blurFixed;
		REAL  blurFloat;
	};
	CSSColor color;
};

struct BoxShadow_t
{
	union {
		ULONG XYSfixed[3];
		REAL  XYSfloat[3];
	};
	union {
		ULONG blurFixed;
		REAL  blurFloat;
	};
	ULONG    inset;
	CSSColor color;
};

struct Box_t /* CSS ordered */
{
	ULONG top;
	ULONG right;
	ULONG bottom;
	ULONG left;
};

struct BoxF_t /* CSS ordered */
{
	REAL top;
	REAL right;
	REAL bottom;
	REAL left;
};

struct BImgCache_t
{
	BoxF  width;
	BoxF  bdsz;
	short count[4];
	REAL  space[4];
	REAL  start[4];
	BoxF  range;
	RectF pos;
};

struct BorderImage_t
{
	STRPTR    source;
	uint8_t   repeatH, repeatV, fill;
	Box       slice;    /* border-image-slice doesn't have units */
	Box       widthFixed;
	BoxF      widthFloat;
	Box       offFixed;
	BoxF      offFloat;
	APTR      image;    /* CSSImage */
	BImgCache cache;
};


struct PointF_t
{
	REAL x;
	REAL y;
};

struct Size_t
{
	ULONG width, height;
};

struct SizeF_t
{
	REAL width, height;
};

struct WordWrap_t
{
	STRPTR word;      /* text to render, utf-8 encoded */
	APTR   node;      /* parent of 'word' (SIT_Widget) */
	REAL   width;     /* width of word/node; soes not include leading space in case of a word; margin-box in case of a node */
	REAL   h;         /* height of the box (margin-box in case of a node) */
	REAL   y;         /* offset from baseline. >0 goes down, <0 goes up */
	REAL   bl;        /* baseline */
	REAL   space;     /* width of space leading the word */
	REAL   marginL;   /* extra space before */
	REAL   marginR;   /* extra space after */
	short  n;         /* number of bytes (not characters) in 'word' */
	char   nl;        /* &1: force line break after this word, &2: soft line break (after), &4: overflow start */
	char   va;        /* vertical-align method */
};

/* special value for WordWrap.y */
#define	VATOP        1000000
#define	VABOTTOM     1000001
#define VAMIDDLE     1000002

struct Style_t /* this structure will contain more or less a direct dump of CSS rules */
{
	uint8_t    bgCount, shadowCount, boxShadowCount, boxShadowCountInset;
	uint8_t    decoStyle, overflow, tabSize, whiteSpace;
	uint8_t    active, reflow, oldBgCount, shadowTotal;
	uint16_t   outlineMargin, inheritSet;
	ULONG      lineHeight, letterSpacing, wordSpacing, verticalAlign;
	ULONG      transformX, transformY;
	ULONG      fontSize, outlineOffset, margin, flags, left, top;
	CSSColor   color, decoColor, fgSel, bgSel;
	STRPTR     transform, textOverflow;
	Text       text;
	Box        padding, majorRadius, minorRadius;
	Border     borderTop, borderRight, borderBottom, borderLeft, outline;
	REAL       opacity;
	Font       font;
	APTR       shadowInherit;
	TextShadow shadow;
	BoxShadow  boxShadow;
	Background background;
	BorderImg  borderImg;
	ULONG      height, width, offset[4];
};

struct Layout_t               /* this structure will contain usable values for the layout module */
{
	SIT_Widget offset;
	uint32_t   crc32[8];      /* cache CSS styles */
	uint32_t   curCRC32;
	int        flags;         /* LAYF_* */
	RectF      pos;           /* position of content (past margin, padding and border), relative to DOMNode.offset */
	SizeF      textarea;      /* width/height for text content */
	SizeF      format;
	BoxF       padding;       /* size from CSS */
	BoxF       border;
	BoxF       majorRadius;
	BoxF       minorRadius;
	REAL       left, top, margin;
	REAL       wordSpacing;
	REAL       letterSpacing;
	REAL       outlineWidth;
	REAL       outlineOffset;
	REAL       scrollTop;     /* top position to start rendering */
	REAL       scrollHeight;
	REAL       scrollLeft;    /* left position to start rendering */
	vector_t   wordwrap;
	short      startword;     /* inline element: used to render box and bg */
	short      nbword;        /* inline: words it spans in the containing block */
};

#define HasBgFlags          (LAYF_HasImg|LAYF_NoBorders|LAYF_NoOutline|LAYF_HasInset)
#define HasBg(node)         (((node)->layout.flags & HasBgFlags) != HasBgFlags || node->style.background)
#define HasFlg(node, flag)  ((node)->layout.flags & (flag))

enum /* visibility */
{
	VisibilityVisible,
	VisibilityHidden,
	VisibilityCollapse
};

enum /* overflow */
{
	OverflowVisible,
	OverflowHidden,
	OverflowScroll,
	OverflowAuto
};

enum /* text-align */
{
	TextAlignLeft,
	TextAlignRight,
	TextAlignCenter,
	TextAlignJustify
};

enum /* text-transform */
{
	TextTransformNone,
	TextTransformCapitalize,
	TextTransformUppercase,
	TextTransformLowercase
};

enum /* white-space */
{
	WhiteSpaceNormal,    /* collapse space + newline */
	WhiteSpaceNowrap,    /* collapse space + newline */
	WhiteSpacePre,       /* do not collapse, do not wrap */
	WhiteSpacePreLine,   /* collapse space, keep newline, no wrap */
	WhiteSpacePreWrap    /* do not collapse, but wrap if necessary */
};

enum /* background-attachment */
{
	BackgroundAttachmentScroll,
	BackgroundAttachmentFixed
};

enum /* background-repeat */
{
	BackgroundRepeat,
	BackgroundRepeatX,
	BackgroundRepeatY,
	BackgroundNoRepeat
};

enum /* border-style */
{
	BorderStyleNone,
	BorderStyleHidden,
	BorderStyleDotted,
	BorderStyleDashed,
	BorderStyleSolid,
	BorderStyleDouble,
	BorderStyleGroove,
	BorderStyleRidge,
	BorderStyleInset,
	BorderStyleOutset
};

enum /* only a subset */
{
	DisplayNone,
	DisplayInline,
	DisplayInlineBlock
};

enum
{
	VerticalAlignBaseline   = 7,
	VerticalAlignSub        = 11,
	VerticalAlignSuper      = 15,
	VerticalAlignTop        = 19,
	VerticalAlignTextTop    = 23,
	VerticalAlignMiddle     = 27,
	VerticalAlignBottom     = 31,
	VerticalAlignTextBottom = 35,
	VerticalAlignLineMiddle = 39,
};

enum /* font-weight */
{
	FontWeightNormal,
	FontWeightBold,
	FontWeightLight
};

enum /* units */
{
	Pixel,
	Points,
	Centimeter,
	Millimeter,
	Inch,
	Em,
	VpWidth,
	VpHeight,
	Percentage
};

enum /* text-decoration-style */
{
	TextDecoSolid  = 0,
	TextDecoDouble = 1,
	TextDecoWavy   = 2
};

enum /* border-image-repeat */
{
	BorderImageStretch = 1,
	BorderImageRepeat,
	BorderImageRound,
	BorderImageSpace
};

enum /* bitfield for style.reflow */
{
	ReflowLayout    = 1,
	ReflowBgPos     = 2,
	ReflowBorderImg = 4,
	ReflowOutline   = 8,
	ReflowOffset    = 16,
	ReflowForce     = 32
};

#endif
