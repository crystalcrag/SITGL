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
	VT_DefFgBg,                 /* CSG: int, use macro VT_DEFFGBG */
	VT_SelLength,               /* __G: int */
	VTX_Private,                /* __S: use VT_SelBuffer() macro instead */
	VTX_Private2,               /* __s: use VT_SelBuffer() macro instead */
	VT_WordWrap,                /* CSG: int (>0: wrap words, 0: wrap characters, default: 1) */
	VT_DebugLines
};

#define VT_DEFFGBG(fg, bg)      (((bg)<<4)|(fg))
#define VT_SelBuffer(size)      VTX_Private, (APTR) (size), VTX_Private2

/* private stuff below */
#ifdef VT_IMPL
typedef struct VirtualTerm_t *  VirtualTerm;
typedef struct VTCoord_t *      VTCoord;
typedef struct VTCoord_t        VTCoord_t;
typedef struct VTIter_t *       VTIter;
typedef struct VTIter_t         VTIter_t;
typedef uint16_t *              DATA16;

struct VTCoord_t
{
	int line;
	int chr;
};

struct VirtualTerm_t
{
	SIT_Widget canvas;
	SIT_Widget scroll;
	DATA8      palette;
	DATA8      buffer;          /* ring buffer */
	uint16_t * styles;          /* VT_ATTR* | VT_SEL* */
	uint32_t * lines;           /* absolute offsets within <buffer>, array is <totalLines> items */
	uint16_t   curAttr;         /* running attribute when parsing text */
	uint16_t   lineAttr;        /* running attribute when rendering */
	uint16_t   defAttr;
	uint16_t   tabSizePx;

	uint16_t   startx;          /* absolute column (px) to start rendering */
	uint8_t    tabSize;
	uint8_t    hasScroll;

	uint8_t    hasSelect;
	int8_t     dx, dy;          /* used to render text-shadow */
	int8_t     autoScrollDir;

	uint8_t    wordSelect;
	uint8_t    waitConf;
	uint8_t    fakeBold;        /* overwrite text if no bold font is found */
	uint8_t    wordWrap;

	uint8_t    spaceLen;

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
	int        maxTotalBytes;
	int        formatWidth;     /* need to redo word wrapping if it differs */
	int        width, height;   /* size of content area in px */
	int        fontSize;        /* font height in px */
	int        scrollPad;       /* width of scrollbar */
	int        reformat;        /* line to start reformating */
	int        fontId;
	int        fontBoldId;
};

struct VTIter_t
{
	DATA8    line;
	uint8_t  split;
	uint16_t size;
	uint16_t endChr;
	uint16_t curChr;
	int      endLine;
	int      curLine;
};

#ifdef VT_UNITTEST
#define VT_DEFMAX     16
#define VT_CHUNK      16
#else
#define VT_DEFMAX     65536
#define VT_CHUNK      2048
#endif
#define VT_LINES      128

#define VT_SELSTART   0x1000
#define VT_SELEND     0x2000
#define VT_SELBOTH    0x3000
#define VT_SELWHOLE   0x7000

#define VT_ATTRBOLD   0x0100
#define VT_ATTRLINE   0x0200
#define VT_ATTRINV    0x0400


#endif
#endif
