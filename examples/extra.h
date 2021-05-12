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
	VT_CursorX,                 /* CSG: int */
	VT_CursorY,                 /* CSG: int */
	VT_Palette,                 /* CSG: uint8_t [4 * 16] */
	VT_MaxLines,                /* CSG: int, def: 300 */
	VT_TabSize,                 /* CSG: int, def: 4 */
	VT_TopLine,                 /* CSG: int */
	VT_DefFgBg,                 /* CSG: int, use macro VT_DEFFGBG */
	VT_CaretStyle,              /* CSG: int */
	VT_SelLength,               /* __G: int */
	VT_SelBuffer,               /* __G: TEXT[VT100_SelLength] */
	VT_DebugLines
};

/* private stuff below */
#ifdef VT_IMPL
typedef struct VirtualTerm_t *  VirtualTerm;
typedef struct VTLines_t *      VTLines;
typedef struct VTCoord_t *      VTCoord;
typedef struct VTCoord_t        VTCoord_t;
typedef uint16_t *              DATA16;

struct VTCoord_t
{
	VTLines    buf;
	uint16_t   line;
	uint16_t   chr;
};

struct VirtualTerm_t
{
	SIT_Widget canvas;
	SIT_Widget scroll;
	ListHead   lines;           /* VTLines */
	DATA8      palette;
	DATA8      buffer;
	DATA8      reformat;
	APTR       ctx;
	uint16_t   tabSizePx;
	uint8_t    tabSize;
	uint8_t    hasScroll;
	uint8_t    hasSelect;
	uint8_t    selColors[8];
	VTCoord_t  selStart, selCur, selEnd;
	uint16_t   colStart, colEnd;
	int        bufUsage;
	int        bufMax;
	int        topLine;
	int        topTarget;
	int        topOffset;
	int        totalLines;
	int        maxLines;
	int        maxBuffer;
	int        formatWidth;
	int        width, height;
	int        postProcess;
	int        fontSize;
	int        cx, cy, cursor;
	int        scrollPad;
	int        fontId;
};

#define VT_CHUNK      2048
#define VT_MASK       (VT_CHUNK-1)
#define VT_LINES      128

#define VT_SELSTART   0x0100
#define VT_SELEND     0x0200
#define VT_SELBOTH    0x0300
#define VT_SELWHOLE   0x0700

struct VTLines_t
{
	ListNode node;
	uint16_t styles[VT_LINES];
	int      start[VT_LINES];
};

#endif
#endif
