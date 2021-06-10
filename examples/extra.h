/*
 * extra.h: unified header for SITGL extra widget: just compile the widget you need, no need to include
 *          them all if you don't need some part.
 *
 * written by T.Pierron, may 2021.
 */

#ifndef SITGL_EXTRA_H
#define SITGL_EXTRA_H

SIT_Widget CCOpen(SIT_Widget parent, DATA8 rgb, SIT_CallProc cb, APTR ud);
SIT_Widget FSOpen(SIT_Widget parent, STRPTR curdir, SIT_CallProc cb, APTR ud, int flags);
void       VTInit(SIT_Widget canvas, SIT_Widget scroll);

enum /* extra properties that can be set on <canvas> after VTInit has been called */
{
	VT_AddText = SIT_TagUser,   /* _S_: String */
	VT_Palette,                 /* CSG: uint8_t [4 * 16] */
	VT_MaxBuffer,               /* CSG: int, def: 65536 */
	VT_TabSize,                 /* CSG: int, def: 4 */
	VT_TopLine,                 /* CSG: int */
	VT_TotalLines,              /* __G: int */
	VT_DefFgBg,                 /* CSG: int, use macro VT_FGBG */
	VT_SelLength,               /* __G: int */
	VT_LinePadding,             /* CSG: int */
	VT_MarkText,                /* CSG: String */
	VT_MarkFgBg,                /* CSG: int, use macro VT_FGBG */
	VTX_Private,                /* _S_: use VT_SelBuffer() macro instead */
	VTX_Private2,               /* _S_: use VT_SelBuffer() macro instead */
	VT_WordWrap                 /* CSG: int (>0: wrap words, 0: wrap characters, default: 1) */
};

#define VT_FGBG(fg, bg)         (((bg)<<4)|(fg))
#define VT_SelBuffer(size)      VTX_Private, (APTR) (size), VTX_Private2

/* private stuff below */
#ifdef VT_IMPL
typedef struct VirtualTerm_t *  VirtualTerm;
typedef struct VTCoord_t        VTCoord_t;
typedef struct VTIter_t         VTIter_t;
typedef struct VTLine_t *       VTLine;
typedef struct VTDrawCmd_t *    VTDrawCmd;
typedef uint16_t *              DATA16;

struct VTCoord_t
{
	int line;
	int chr;
};

struct VTLine_t
{
	uint16_t styles;            /* VT_ATTR* | VT_SEL* */
	uint16_t indent;
	uint32_t offset;            /* absolute offsets within <buffer> */
};

struct VirtualTerm_t
{
	SIT_Widget canvas;
	SIT_Widget scroll;
	DATA8      palette;
	DATA8      buffer;          /* ring buffer */
	VTLine     lines;           /* array is <totalLines> items */
	uint16_t   curAttr[2];      /* running attribute when parsing text */
	uint16_t   lineAttr[2];     /* running attribute when rendering */
	uint16_t   defAttr;
	uint16_t   tabSizePx;
	uint16_t   startx;          /* absolute column (px) to start rendering */
	uint16_t   lineHeight;

	uint8_t    tabSize;
	uint8_t    hasScroll;
	uint8_t    hasSelect;
	uint8_t    wordSelect;

	int8_t     dx, dy;          /* used to render text-shadow */
	int8_t     autoScrollDir;
	uint8_t    waitConf;

	uint8_t    fakeBold;        /* overwrite text if no bold font is found */
	uint8_t    wordWrap;
	uint8_t    spaceLen;
	uint8_t    linePadding;

	uint8_t    lineBorder;      /* line-height */
	uint8_t    searchAttr;
	uint8_t    searchMax;

	SIT_Action autoScroll;
	uint8_t    selColors[8];
	VTCoord_t  selStart, selCur, selEnd;
	uint16_t   colStart, colEnd;
	int        bufUsage;        /* bytes currently stored in VTerm (including attributes) */
	int        bufMax;          /* max bytes that can be stored */
	int        bufStart;        /* start of content within the ring buffer */
	int        allocLines;      /* max capacity on <styles> and <lines> array */
	int        topLine;
	int        topTarget;
	int        topOffset;       /* pixel offset to shift lines in content area */
	int        totalLines;      /* number of items in <styles> and <lines> array */
	int        totalBytes;
	int        formatWidth;     /* need to redo word wrapping if it differs */
	int        width, height;   /* size of content area in px */
	int        fontSize;        /* font height in px */
	int        scrollPad;       /* width of scrollbar */
	int        scrollOff;       /* if anchored on the left */
	int        reformat;        /* line to start reformating */
	int        fontId;
	int        fontBoldId;
	uint8_t    search[32];      /* highlight this bytes (up to searchMax) */
};

struct VTIter_t
{
	DATA8    line;
	uint8_t  split;
	uint16_t size;
	int      endLine;
	int      curLine;
	DATA8    base;
};

struct VTDrawCmd_t
{
	uint16_t styles;
	uint16_t chrLen;
	uint16_t pxLen;
};

#ifdef VT_UNITTEST
/* note: need to be a power of 2 */
#define VT_DEFMAX     16
#define VT_CHUNK      16
#else
#define VT_DEFMAX     64*1024
#define VT_CHUNK      2048
#endif
#define VT_LINES      128

#define VT_SELSTART   0x1000 /* those are only set in vt->styles */
#define VT_SELEND     0x2000
#define VT_SELBOTH    0x3000
#define VT_SELWHOLE   0x7000
#define VT_SELCLEAR   0x8fff /* using operator & */
#define VT_PRIMARY    0x8000

#define VT_ATTRBOLD   0x0100
#define VT_ATTRLINE   0x0200


#endif
#endif
