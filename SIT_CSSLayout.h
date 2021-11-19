/*
 * SIT_CSSLayout.h : public functions for formatting inline nodes.
 *
 * Written by T.Pierron, Mar 2020
 */

#ifndef	SIT_CSSLAYOUT_H
#define SIT_CSSLAYOUT_H

#include "SIT_CSSParser.h"

REAL ToPoints(SIT_Widget parent, SIT_Widget node, ULONG fixed, int side);
REAL layoutCalcLineHeight(SIT_Widget);
void layoutCalcBox(SIT_Widget);
void layoutMeasureWords(SIT_Widget node, SizeF * ret);
void layoutAdjustBorderRadius(SIT_Widget);
int  layoutUpdateStyles(SIT_Widget);
void layoutParseHTML(SIT_Widget, STRPTR);
void layoutFreeTree(SIT_Widget start);
void layoutCalc(SIT_Widget);
REAL layoutNCSize(SIT_Widget, int side);
void layoutAdjustBorderRadius(SIT_Widget);
void layoutFree(SIT_Widget);
void layoutAlignText(SIT_Widget);
REAL layoutSize(SIT_Widget, int side);
REAL layoutLineWidth(WordWrap wrap, int max);
int  layoutUpdateChildren(SIT_Widget);
void layoutFindFont(SIT_Widget);
void layoutGetTextContent(SIT_Widget, STRPTR dest, int max);
void layoutSetSize(SIT_Widget);
Bool layoutSizeChanged(SIT_Widget);
void layoutCalcPadding(SIT_Widget);

enum /* possible values for 'side' parameter of ToPoints() */
{
	CSS_TOP    = 0,
	CSS_RIGHT  = 1,
	CSS_BOTTOM = 2,
	CSS_LEFT   = 3
};

#define CSS_WIDTH      CSS_LEFT
#define CSS_HEIGHT     CSS_TOP
#define CSS_ADDPAD     0x80
#define CSS_ADDBORDER  0x40

#define EPSILON        0.0001f
#define ALMOST0(v)     (fabsf(v) < EPSILON)

#endif
