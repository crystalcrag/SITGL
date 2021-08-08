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

#endif
