/*
 * SIT_ListBox.c : multi-column list or icon/tile view.
 *
 * written by T.Pierron, may 2020.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <ctype.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	struct TagList_t ListBoxClass[] = {
		{ "listBoxFlags",  SIT_ListBoxFlags,  _SG, SIT_INT,  OFFSET(SIT_ListBox, lbFlags) },
		{ "cellPaint",     SIT_CellPaint,     _SG, SIT_PTR,  OFFSET(SIT_ListBox, cellPaint) },
		{ "finalizeItem",  SIT_FinalizeItem,  _SG, SIT_PTR,  OFFSET(SIT_ListBox, finalizeItem) },
		{ NULL,            SIT_AutoComplete,  _S_, SIT_PTR,  0 },
		{ "viewMode",      SIT_ViewMode,      _SG, SIT_U8,   OFFSET(SIT_ListBox, viewMode) },
		{ NULL,            SIT_ColumnCount,   __G, SIT_INT,  OFFSET(SIT_ListBox, softColumn) },
		{ "columnWidths",  SIT_ColumnWidths,  _SG, SIT_PTR,  OFFSET(SIT_ListBox, columnWidths) },
		{ "columnNames",   SIT_ColumnNames,   _SG, SIT_PTR,  OFFSET(SIT_ListBox, columnNames) },
		{ "columnAlign",   SIT_ColumnAlign,   _SG, SIT_PTR,  OFFSET(SIT_ListBox, columnAlign) },
		{ "selectedIndex", SIT_SelectedIndex, _SG, SIT_INT,  OFFSET(SIT_ListBox, selIndex) },
		{ NULL,            SIT_ItemCount,     __G, SIT_INT,  0 },
		{ "makeVisible",   SIT_MakeVisible,   _S_, SIT_INT,  0 },
		{ NULL,            SIT_TargetRow,     _SG, SIT_INT,  OFFSET(SIT_ListBox, curRow) },
		{ NULL,            SIT_RowSelArg,     _SG, SIT_BOOL, 0 },
		{ NULL,            SIT_RowTagArg,     _SG, SIT_PTR,  0 },
		{ "sortColumn",    SIT_SortColumn,    _SG, SIT_INT,  OFFSET(SIT_ListBox, sortColumn) },
		{ "rowMaxVisible", SIT_RowMaxVisible, _SG, SIT_INT,  OFFSET(SIT_ListBox, maxRowVisible) },
		{ NULL,            SIT_TagEnd }
	};

	/* look ahead buffer: we only need one for the entire application */
	static uint8_t textLookAhead[63];
	static uint8_t textLength;

typedef struct StrPool_t *       StrPool;

struct Cell_t
{
	SizeF    sizeObj;
	RectF    sizeCell;
	APTR     obj;                /* STRPTR or SIT_Widget */
	APTR     userData;
	uint16_t flags;              /* CELL_* */
	uint16_t colLeft;            /* to get back to start of row */
};

struct StrPool_t
{
	ListNode node;
	TEXT     mem[1];
};

enum /* bitfield for Cell->flags */
{
	CELL_ISCONTROL  = 0x0001,    /* Cell->obj is a SIT_Widget, otherwise STRPTR */
	CELL_HASSIZE    = 0x0002,    /* Cell->size up to date */
	CELL_ALIGNR     = 0x0004,    /* default: left aligned */
	CELL_ALIGNC     = 0x0008,
	CELL_VALIGNB    = 0x0010,    /* default: top aligned */
	CELL_VALIGNC    = 0x0020,
	CELL_ALIGNFLAGS = 0x003c,
	CELL_SELECT     = 0x0040,
	CELL_CATEGORY   = 0x0080,    /* thead */
	CELL_CATVISIBLE = 0x0100,    /* initially hidden if no item in cat */
	CELL_COLSTART   = 0x0200,    /* start of a row */
	CELL_HIDDEN     = 0x0400,    /* temporarily hidden */
	CELL_PRESELECT  = 0x0800,    /* pre-select by lasso */
};

#define CELL_HASSELECT       (CELL_PRESELECT | CELL_SELECT)
#define SITV_HasScroll       0x0100     /* extra flag for SIT_ListBox->lbFlags */
#define SITV_PendingRecalc   0x0200     /* don't register event twice */
#define SITV_ColumnChanged   0x0400
#define SITV_ListMeasured    0x0800
#define SITV_ReorgColumns    0x1000
#define STARTCELL(list)      ((Cell) (list)->cells.buffer)

static void SIT_ListRestoreChildren(SIT_Widget td, Cell cell)
{
	SIT_Widget c, p;
	for (c = p = cell->obj, td->children.lh_Head = (APTR) c; c; c->parent = td, p = c, NEXT(c));
	td->children.lh_Tail = (APTR) p;
	td->title = NULL;
}

static void SIT_ListCalcSize(SIT_Widget td, Cell cell, int resize)
{
	cell->flags |= CELL_HASSIZE;
	if (cell->flags & CELL_ISCONTROL)
	{
		memset(&td->minBox, 0, sizeof td->minBox);
		memset(&td->optimalBox, 0, sizeof td->optimalBox);
		SIT_ListRestoreChildren(td, cell);
		SIT_LayoutWidgets(td, resize);

		cell->sizeObj.width  = td->box.right  - td->box.left;
		cell->sizeObj.height = td->box.bottom - td->box.top;
		cell->sizeCell.height = cell->sizeObj.height;
		cell->sizeCell.width  = cell->sizeObj.width;
	}
	else /* simple label */
	{
		td->title = cell->obj;
		layoutMeasureWords(td, &cell->sizeObj);
		cell->sizeCell.height = cell->sizeObj.height + td->padding[1] + td->padding[3];
		cell->sizeCell.width = cell->sizeObj.width + td->padding[0] + td->padding[2];
	}
}

static int SIT_ListMeasure(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	SIT_Widget td = list->td;
	SizeF * pref = cd;

	if (w->children.lh_Head)
		SIT_LayoutWidgets(w, (ULONG) ud);

	if (list->viewMode == SITV_ListViewReport)
	{
		SizeF size = {0, 0};
		Cell  cell, row, hdr;
		int   i, j, count;

		if (list->lbFlags & SITV_ListMeasured)
		{
			//fprintf(stderr, "list cur size: %g, %g\n", w->flags & SITF_FixedWidth  ? w->fixed.width  : w->currentBox.width,
			//	w->flags & SITF_FixedHeight ? w->fixed.height : w->currentBox.height);
			if (pref->width  < 0) pref->width  = w->flags & SITF_FixedWidth  ? w->fixed.width  : w->currentBox.width;
			if (pref->height < 0) pref->height = w->flags & SITF_FixedHeight ? w->fixed.height : w->currentBox.height;
			/* if size changes, it will be handled in OnRsize event */
			return 1;
		}

		count = list->columnCount;
		hdr = list->columns;
		if (hdr == NULL)
		{
			/* SITV_NoHeaders */
			hdr = alloca(sizeof *hdr * count);
			memset(hdr, 0, sizeof *hdr * count);
		}
		for (cell = STARTCELL(list), i = count; i > 0; i --, cell ++, hdr ++)
		{
			if ((hdr->flags & CELL_HASSIZE) == 0 && hdr->obj)
			{
				((SIT_Widget)hdr->obj)->optimalWidth(hdr->obj, &hdr->sizeObj, NULL);
				hdr->flags |= CELL_HASSIZE;
			}
			hdr->sizeCell.width = hdr->sizeObj.width;
			if (size.height < hdr->sizeObj.height)
				size.height = hdr->sizeObj.height;
			for (j = list->rowCount, row = cell; j > 0; j --, row += count)
			{
				if ((row->flags & CELL_HASSIZE) == 0)
					SIT_ListCalcSize(td, row, FitUsingInitialBox);

				if (hdr->sizeCell.width < row->sizeCell.width)
					hdr->sizeCell.width = row->sizeCell.width;
			}
			size.width += hdr->sizeCell.width;
		}
		// fprintf(stderr, "header height = %d\n", (int) size.height);
		list->hdrHeight = size.height;
		/* header height */
		if (size.width > 0)
		{
			for (hdr = list->columns, i = 0; i < count; i ++, hdr ++)
			{
				if (list->columnWidths == NULL && list->realWidths[i] >= 0)
					list->realWidths[i] = hdr->sizeCell.width / size.width;
				hdr->sizeCell.height = size.height;
			}
		}
		int maxRow = list->maxRowVisible;
		if (maxRow <= 0) maxRow = -1;
		/* compute row height */
		for (cell = STARTCELL(list), i = list->rowCount; i > 0; i --)
		{
			REAL height = 0, x = 0;
			for (hdr = list->columns, row = cell, j = list->columnCount; j > 0; x += hdr->sizeCell.width, j --, cell ++, hdr ++)
			{
				REAL h = cell->sizeCell.height;
				cell->sizeCell.top   = size.height;
				cell->sizeCell.left  = x;
				cell->sizeCell.width = hdr->sizeCell.width;
				if (height < h)
					height = h;
			}
			for (j = list->columnCount; j > 0; row->sizeCell.height = height, j --, row ++);
			if (maxRow != 0)
				size.height += height, maxRow --;
		}
		list->scrollHeight = size.height - list->hdrHeight;
		size.width  += w->padding[0] + w->padding[1];
		size.height += w->padding[1] + w->padding[3];
		if (pref->width  < 0) pref->width  = 0;
		if (pref->height < 0) pref->height = 0;
		list->lbFlags |= SITV_ListMeasured;
	}
	else /* SITV_ListViewIcon */
	{
		Cell cell;
		REAL maxw;
		int  i;

		for (cell = STARTCELL(list), i = list->cells.count, maxw = 0; i > 0; i --, cell ++)
		{
			uint16_t flags = cell->flags;
			if ((flags & CELL_HASSIZE) == 0)
				SIT_ListCalcSize(flags & CELL_CATEGORY ? list->thead : td, cell, FitUsingInitialBox);

			if (maxw < cell->sizeCell.width && (flags & CELL_CATEGORY) == 0)
				maxw = cell->sizeCell.width;
		}
		list->maxCellW = maxw;
		/* all cells have same width, but can have different height */
		for (cell = STARTCELL(list), i = list->cells.count; i > 0; cell->sizeCell.width = maxw, i --, cell ++);
		/* size has to be explicitely set */
		if (pref->width  < 0) pref->width  = 0;
		if (pref->height < 0) pref->height = 0;
	}
	return 0;
}

static int SIT_ListRender(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox  list   = (SIT_ListBox) w;
	SIT_CallProc paint  = list->cellPaint;
	SIT_Widget   td     = list->td;
	SIT_Widget   sel    = list->tdSel;
	NVGcontext * vg     = sit.nvgCtx;
	RectF        pos    = w->layout.pos;
	uint8_t      icon   = list->viewMode == SITV_ListViewIcon;
	uint8_t      text   = list->defAlign >= 0 ? list->defAlign : w->style.text.align;
	uint8_t      select = 0;
	int          col    = list->columnCount;
	Cell         cell;
	int          i, j, row;

	nvgSave(vg);
	pos.top += w->offsetY - w->layout.padding.top;
	pos.left += w->offsetX;
	pos.height += w->layout.padding.bottom + w->layout.padding.top;
	list->lbFlags &= ~SITV_ReorgColumns;

	if (icon == 0)
	{
		/* render header */
		REAL x;
		for (i = col, cell = list->columns, x = w->layout.border.left; i > 0; x += cell->sizeCell.width, i --, cell ++)
		{
			SIT_Widget hdr = cell->obj;
			if (hdr == NULL) continue;

			hdr->box.left = x;
			hdr->box.top = w->layout.border.top;
			hdr->box.right = x + cell->sizeCell.width;
			hdr->box.bottom = hdr->box.top + cell->sizeCell.height;
			hdr->parent = w;

			SIT_LayoutCSSSize(hdr);
			SIT_RenderNode(hdr);
		}
		pos.top += list->hdrHeight;
		nvgIntersectScissor(vg, pos.left, pos.top, pos.width, pos.height - list->hdrHeight);
	}
	/* we have to do clipping here, because children are managed by this control */
	else nvgIntersectScissor(vg, pos.left, pos.top, pos.width, pos.height);

	/* nested list :-/ */
	if (w->parent->state & STATE_CHECKED)
		select = 1;

	for (cell = list->rowTop, row = cell - STARTCELL(list), i = list->cells.count - row, row /= col; i > 0; i -= col, row ++)
	{
		Bool forceSel = select || (icon == 0 && (cell->flags & CELL_HASSELECT));
		if (cell->flags & CELL_HIDDEN) { cell += col; continue; }
		if (icon == 0)
		{
			SIT_Widget node = (cell->flags & CELL_HASSELECT) ? sel : td;
			if (node->style.background)
			{
				/* extend background to the whole row */
				ListNew(&node->children);
				node->box.left = cell->sizeCell.left + w->padding[0];
				node->box.top = cell->sizeCell.top - list->scrollTop + w->padding[1];
				node->box.right = cell->sizeCell.left + w->layout.pos.width - list->scrollPad;
				node->box.bottom = node->box.top + cell->sizeCell.height;
				node->title = NULL;
				node->layout.wordwrap.count = 0;

				SIT_LayoutCSSSize(node);
				SIT_RenderNode(node);
			}
		}
		for (j = 0; j < col; j ++, cell ++)
		{
			uint16_t   flags = cell->flags;
			uint8_t    align = text;
			SIT_Widget node  = (flags & CELL_HASSELECT) || forceSel ? sel : td;

			if (flags & CELL_CATEGORY)
			{
				if (list->catVisible < 2 || (flags & CELL_CATVISIBLE) == 0) continue;
				node = list->thead;
				align = TextAlignLeft;
			}

			node->box.left = cell->sizeCell.left + w->padding[0];
			REAL maxw = cell->sizeCell.width;
			if (icon == 0)
			{
				int cflags = flags & CELL_ALIGNFLAGS;
				if (cflags == 0)
					cflags = list->columns[j].flags;

				align = cflags & CELL_ALIGNR ? TextAlignRight :
				        cflags & CELL_ALIGNC ? TextAlignCenter : 0;
				if (j == col-1)
					maxw -= list->scrollPad;
			}
			if (cell->sizeObj.width <= maxw)
			{
				switch (align) {
				case TextAlignCenter: node->box.left += (maxw - cell->sizeObj.width) * 0.5f; break;
				case TextAlignRight:  node->box.left += (maxw - cell->sizeObj.width) - node->padding[2] - node->padding[0];
				}
			}
			node->box.top = cell->sizeCell.top - list->scrollTop + w->padding[1];
			node->box.right = cell->sizeCell.left + maxw + w->padding[0];
			node->box.bottom = node->box.top + cell->sizeCell.height;
			if (node->box.top > pos.height)
				goto break_all;

			if ((flags & CELL_ISCONTROL) == 0)
			{
				node->title = cell->obj;
				node->layout.textarea = cell->sizeObj;
				ListNew(&node->children);
				/* quick and easy way to customize cell appearance */
				if (paint)
				{
					SIT_OnCellPaint ocp = {.rowColumn = j | (row << 8), .LTWH = {
						node->box.left + w->offsetX + w->box.left,
						node->box.top  + w->offsetY + w->box.top,
						node->box.right - node->box.left,
						node->box.bottom - node->box.top
					}};
					if (paint(w, &ocp, cell[-j].userData))
						continue;
					if (! forceSel)
					{
						if (ocp.bgColor[3] > 0)
						{
							/* override background color */
							nvgBeginPath(vg);
							nvgRect(vg, w->offsetX + w->box.left + cell->sizeCell.left + w->padding[0],
								w->offsetY + w->box.top + cell->sizeCell.top - list->scrollTop + w->padding[1], cell->sizeCell.width, cell->sizeCell.height);
							nvgFillColorRGBA8(vg, ocp.bgColor);
							nvgFill(vg);
						}
						memcpy(node->style.color.rgba, ocp.fgColor[3] > 0 ? ocp.fgColor : (DATA8) node->attachment, 4);
					}
				}
			}
			else SIT_ListRestoreChildren(node, cell);

			SIT_LayoutCSSSize(node);
			if (node == sel)
			{
				/* background is already rendered */
				uint8_t bgCount = node->style.bgCount;
				node->style.bgCount = 0;
				SIT_RenderNode(node);
				node->style.bgCount = bgCount;
			}
			else SIT_RenderNode(node);
		}
	}
	break_all:

	if (list->lassoSX != list->lassoEX)
	{
		REAL sx = list->lassoSX, ex = list->lassoEX - sx;
		REAL sy = list->lassoSY, ey = list->lassoEY - sy;
		/* in order for the selection overlay to be visible, we need to darken it a bit */
		uint8_t bg[4] = {
			w->style.bgSel.rgba[0] * 205 >> 8,
			w->style.bgSel.rgba[1] * 205 >> 8,
			w->style.bgSel.rgba[2] * 205 >> 8,
			w->style.bgSel.rgba[3]
		};
		if (sx < 0) ex += sx, sx = 0;
		if (sy < 0) ey += sy, sy = 0;
		if (ex < 0) sx += ex, ex = -ex;
		if (ey < 0) sy += ey, ey = -ey;
		sx += pos.left;
		sy += pos.top - list->scrollTop;
		nvgBeginPath(vg);
		nvgRect(vg, sx, sy, ex, ey);
		nvgStrokeColorRGBA8(vg, bg);
		nvgStroke(vg); bg[3] >>= 1;
		nvgFillColorRGBA8(vg, bg);
		nvgFill(vg);
	}
	nvgRestore(sit.nvgCtx);

	return 1;
}

/* OnScroll on vertical scroll bar */
static int SIT_ListScroll(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = ud;
	REAL        top  = (int) cd, count;
	Cell        row;

	for (row = STARTCELL(list), count = list->cells.count; count > 0 && row->sizeCell.top+row->sizeCell.height < top; )
	{
		int nextRow = row->colLeft;
		count -= nextRow;
		row   += nextRow;
	}
	list->rowTop = row;
	list->scrollTop = top;

	return 1;
}

static Bool SIT_ListAdjustScroll(SIT_ListBox list)
{
	Bool ret = False;
	REAL max = list->super.layout.pos.height - list->hdrHeight;

	if (max <= 0) return False;
	if (list->scrollHeight > max)
	{
		if ((list->lbFlags & SITV_HasScroll) == 0)
		{
			if (list->super.vscroll == NULL)
			{
				SIT_CreateWidgets(&list->super, "<scrollbar name=vscroll lineHeight=", (int) list->super.style.font.size,
					"top=", SITV_AttachForm, (int) list->hdrHeight, SITV_NoPad, "bottom=FORM,,NOPAD right=FORM,,NOPAD>");
				SIT_AddCallback(list->super.vscroll, SITE_OnChange, SIT_ListScroll, list);
			}
			list->lbFlags |= SITV_HasScroll;
			list->scrollPad = ((SIT_App)sit.root)->defSBSize;
			ret = True;
		}
		SIT_SetValues(list->super.vscroll, SIT_Visible, True, SIT_ScrollPos, (int) list->scrollTop, SIT_MaxValue, (int) list->scrollHeight,
			SIT_PageSize, (int) max, NULL);
	}
	else if (list->lbFlags & SITV_HasScroll)
	{
		SIT_SetValues(list->super.vscroll, SIT_Visible, False, NULL);
		list->lbFlags &= ~SITV_HasScroll;
		list->scrollPad = 0;
		list->scrollTop = 0;
		list->rowTop = STARTCELL(list);
		ret = True;
	}
	return ret;
}

#if 0
static void SIT_ListDebug(SIT_ListBox list)
{
	Cell cell;
	int  i;

	fprintf(stderr, "==== list content: ====\n");
	for (cell = STARTCELL(list), i = list->cells.count; i > 0; i --, cell ++)
	{
		if (cell->flags & CELL_COLSTART)
			fprintf(stderr, "- start of row\n");
		fprintf(stderr, "  cell: %d%s %g - %g\n", cell->colLeft, cell->flags & CELL_CATEGORY ? list->catVisible < 2 ? " (hidden category)" : " (category)" : "",
			cell->sizeObj.width, cell->sizeCell.width);
	}
}
#else
#define SIT_ListDebug(x)
#endif

static void SIT_ListAdjustMaxHeight(SIT_ListBox list)
{
	float height = list->super.padding[1] + list->super.padding[3];
	float width  = list->super.padding[0] + list->super.padding[2];;
	Cell  cell;
	int   i, j;
	if ((list->lbFlags & SITV_NoHeaders) == 0)
		height = list->columns[0].sizeCell.height;
	for (cell = STARTCELL(list), i = list->cells.count, j = list->maxRowVisible; i > 0; j --, i --, cell += list->columnCount)
	{
		if (width < cell->sizeObj.width)
			width = cell->sizeObj.width;
		if (j > 0)
			height += cell->sizeObj.height;
	}
	if (height == 0) height = 1;
	SIT_SetValues(&list->super, SIT_Height, (int) height, SIT_Width, (int) width, NULL);
}


/* keep the last selected item in visual range */
static void SIT_ListMakeVisible(SIT_ListBox list, Cell cell)
{
	if (cell == NULL) return;
	if ((cell->flags & CELL_HASSIZE) == 0)
	{
		/* need to be performed later */
		list->makeVisible = cell - STARTCELL(list) + 1;
		return;
	}
	REAL y    = cell->sizeCell.top - list->scrollTop;
	REAL padT = list->super.layout.padding.top;
	REAL padB = list->super.layout.padding.bottom;
	REAL maxy = list->super.layout.pos.height;
	REAL padH = list->hdrHeight;

	list->makeVisible = 0;
	if (y < -(padT - padH))
	{
		y = cell->sizeCell.top - padT - padH;
	}
	else if (y + cell->sizeCell.height > maxy+padB)
	{
		y = cell->sizeCell.top + cell->sizeCell.height - maxy;
	}
	else return;
	if (y < 0) y = 0;
	if (y != list->scrollTop)
	{
		SIT_ListScroll(list->super.vscroll, (APTR) (int) y, list);
		SIT_ListAdjustScroll(list);
	}
}

/* handle SITE_OnResize event on list box */
static int SIT_ListResize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;

	if (w->title && list->formatTitle != w->layout.pos.width)
	{
		SizeF pref = {.width = w->layout.pos.width};
		layoutMeasureWords(w, &pref);
		list->formatTitle = w->layout.pos.width;
	}

	/* icon view: stacked left to right, top to bottom */
	if (list->viewMode == SITV_ListViewIcon)
	{
		REAL max = w->layout.pos.width, maxh, visible;
		Cell cell, row;
		REAL x, y;
		int  i, j;
		int  start = list->recalcCell;

		list->recalcCell = 0;
		redo:
		i = list->cells.count;
		if (i == 0) return 1;
		cell = vector_nth(&list->cells, start);
		if (i > 1 && list->borderCSSH == AUTOVAL)
		{
			/* distribute horizontal space */
			for (row = STARTCELL(list), j = list->cells.count; j > 0 && (row->flags & (CELL_CATEGORY|CELL_HIDDEN)); j --, row ++);
			if (j > 0)
			{
				j = (max - 0.01f) / row->sizeCell.width;
				list->borderSpacingH = j > 1 ? (max - j * row->sizeCell.width - 0.01f) / (j-1) : 0;
			}
		}
		if (start > 0)
		{
			Cell prev = cell - 1;
			for (row = STARTCELL(list); prev > row && ((prev->flags & CELL_HIDDEN) || (prev->flags & CELL_COLSTART) == 0); prev --, start --);
			if (prev->flags & CELL_HIDDEN)
				goto from_scratch;

			cell = prev + 1;
			i -= start;
			x = prev->sizeCell.left + prev->sizeCell.width + list->borderSpacingH;
			y = prev->sizeCell.top;
			j = 1;
			maxh = prev->sizeCell.height;
		}
		else from_scratch: maxh = 0, x = 0, y = w->padding[1], j = 0;

		for (visible = y + w->layout.pos.height, list->softColumn = 0; i > 0; i --, cell ++)
		{
			if (cell->flags & CELL_HIDDEN) continue;
			if (cell->flags & CELL_CATEGORY)
			{
				if (list->catVisible < 2 || (cell->flags & CELL_CATVISIBLE) == 0)
				{
					for (row = cell - j; j > 0; row ++, j --)
						row->colLeft = j, row->sizeCell.height = maxh;
					continue;
				}
				cell->sizeCell.width = max;
			}
			if (x == 0)
				cell->flags |= CELL_COLSTART;
			else
				cell->flags &= ~CELL_COLSTART;

			cell->sizeCell.left = x;
			cell->sizeCell.top = y;
			x += cell->sizeCell.width;
			if (x > max && j > 0)
			{
				x = 0;
				y += maxh+list->borderSpacingV;
				if (y > visible)
				{
					list->scrollHeight = y + maxh; /* not exact, but no big deal */
					if (SIT_ListAdjustScroll(list))
					{
						max = w->layout.pos.width - list->scrollPad;
						goto redo;
					}
				}
				list->softColumn = j;
				cell->sizeCell.left = x;
				cell->sizeCell.top = y;
				cell->flags |= CELL_COLSTART;
				x += cell->sizeCell.width + list->borderSpacingH;
				/* all cells in the row have the same height */
				for (row = cell - j; j > 0; row ++, j --)
					row->colLeft = j, row->sizeCell.height = maxh;
				maxh = 0; j = 1;
			}
			else j ++, x += list->borderSpacingH;
			if (maxh < cell->sizeObj.height)
				maxh = cell->sizeObj.height;
		}
		if (list->softColumn < j)
			list->softColumn = j;
		for (row = cell - j; j > 0; row ++, j --)
			row->colLeft = j, row->sizeCell.height = maxh;
		x = w->layout.padding.top + w->layout.padding.bottom;
		list->scrollHeight = y + maxh;

		y = w->layout.pos.height + x;
		if (w->vscroll && list->scrollTop + y > list->scrollHeight + x)
		{
			/* empty space at bottom: remove */
			x = list->scrollHeight - y;
			if (x < 0) x = 0;
			if (list->scrollTop != x)
				SIT_ListScroll(w->vscroll, (APTR) (int) x, list);
		}

		SIT_ListAdjustScroll(list);
	}
	else if (list->realWidths) /* report view: adjust column proportionally */
	{
		Cell  cell, row, hdr;
		REAL  max, total, x, top;
		int   i, j, count;
		max = w->layout.pos.width;
		count = list->columnCount;

		if (list->lbFlags & SITV_ReorgColumns)
		{
			SIT_ListReorgColumns(w, list->columnAlign);
		}

		for (cell = STARTCELL(list), hdr = list->columns, total = 0, i = 0, x = 0, top = list->hdrHeight; i < count; i ++, cell ++, hdr ++)
		{
			if (i < count-1)
			{
				REAL colWidth = list->realWidths[i];
				hdr->sizeCell.width = colWidth < 0 ? - colWidth /* fixed width */ : roundf(max * colWidth);
				total += hdr->sizeCell.width;
			}
			else hdr->sizeCell.width = max - total;

			for (j = list->rowCount, row = cell, top = list->hdrHeight; j > 0; j --, row += count)
			{
				if (row->flags & CELL_HIDDEN) continue;
				if ((row->flags & CELL_HASSIZE) == 0)
					SIT_ListCalcSize(list->td, row, FitUsingInitialBox);
				row->sizeCell.width = hdr->sizeCell.width;
				row->sizeCell.left = x;
				row->sizeCell.top = top;
				top += row->sizeCell.height;
			}
			x += hdr->sizeCell.width;
		}
		if (list->maxRowVisible > 0)
			SIT_ListAdjustMaxHeight(list);

		list->scrollHeight = top - list->hdrHeight;
		SIT_ListAdjustScroll(list);
	}
	if (list->makeVisible > 0)
	{
		if (list->makeVisible <= list->cells.count)
			SIT_ListMakeVisible(list, vector_nth(&list->cells, list->makeVisible-1));
		else
			list->makeVisible = 0;
	}
	return 1;
}

static void SIT_ListUpdateCSS(SIT_ListBox list, Cell cell)
{
	SIT_Widget node = cell->flags & CELL_SELECT ? list->tdSel : list->td;
	SIT_ListRestoreChildren(node, cell);
	layoutUpdateStyles(node);
}

/* selection state on one item */
static void SIT_ListSetSelection(SIT_ListBox list, Cell cell, Bool deselectOld, int extend)
{
	Cell old = list->selIndex < 0 ? NULL : vector_nth(&list->cells, list->selIndex);
	if (cell && list->viewMode == SITV_ListViewReport)
	{
		/* cell must point to start of row */
		int index = cell - STARTCELL(list);
		cell = vector_nth(&list->cells, index - index % list->columnCount);
	}
	if ((list->lbFlags & SITV_SelectMultiple) == 0)
		deselectOld = True, extend = False;
	else if (extend)
		deselectOld = False;

	/* category rows cannot be selected */
	if (cell && cell->flags & CELL_CATEGORY)
		return;

	if (old != cell || (list->lbFlags & SITV_SelectMultiple))
	{
		if (deselectOld)
		{
			if (list->lbFlags & SITV_SelectMultiple)
			{
				/* deselect everything but <cell> */
				int i;
				for (old = STARTCELL(list), i = list->cells.count; i > 0; i --, old ++)
				{
					if (old == cell || (old->flags & (CELL_HIDDEN|CELL_CATEGORY)) || (old->flags & CELL_SELECT) == 0) continue;
					old->flags &= ~CELL_SELECT;
					if (old->flags & CELL_ISCONTROL)
						SIT_ListUpdateCSS(list, old);
					sit.dirty = 1;
				}
			}
			else if (old)
			{
				old->flags &= ~CELL_SELECT;
				if (old->flags & CELL_ISCONTROL)
					SIT_ListUpdateCSS(list, old);
				sit.dirty = 1;
			}
		}
		uint16_t flags = 0;
		if (cell)
		{
			flags = (cell->flags & CELL_SELECT) ^ CELL_SELECT;
			list->selIndex = cell - STARTCELL(list);
			if (! deselectOld && ! extend) /* last cell must be selected */
				cell->flags ^= CELL_SELECT;
			else if (cell->flags & CELL_SELECT)
				goto break_all;
			else
				cell->flags |= CELL_SELECT;
			if (cell->flags & CELL_ISCONTROL)
				SIT_ListUpdateCSS(list, cell);
			if (extend < 2) /* select all: don't move view */
				SIT_ListMakeVisible(list, cell);
			sit.dirty = 1;
		}
		else list->selIndex = -1;
		break_all:
		if (extend)
		{
			if (old == NULL)
				old = STARTCELL(list);
			while (old != cell)
			{
				if ((old->flags & CELL_SELECT) != flags)
				{
					old->flags ^= CELL_SELECT;
					if (old->flags & CELL_ISCONTROL)
						SIT_ListUpdateCSS(list, old);
					sit.dirty = 1;
				}
				if (old < cell) old ++;
				else old --;
			}
		}

		if (HAS_EVT(&list->super, SITE_OnChange))
			SIT_ApplyCallback(&list->super, cell ? cell->userData : NULL, SITE_OnChange);
	}
}


/* used to compare filenames */
static int alnumsort(STRPTR str1, STRPTR str2)
{
	while (*str1 && *str2)
	{
		int iswd1 = isdigit(*str1);
		int iswd2 = isdigit(*str2);
		if (iswd1 && iswd2)
		{
			int n1 = strtoul(str1, &str1, 10); /* force decimal, do not use 0 for base */
			int n2 = strtoul(str2, &str2, 10);
			if (n1 < n2) return -1;
			if (n1 > n2) return  1;
		}
		else if (iswd1)
		{
			return -1;
		}
		else if (iswd2)
		{
			return 1;
		}
		else /* case insensitive compare */
		{
			int res = strncasecmp(str1, str2, 1);
			if (res) return res;
			str1 ++;
			str2 ++;
		}
	}
	if (*str2) return -1;
	if (*str1) return  1;
	return 0;
}

static STRPTR SIT_ListGetCellBuffer(Cell cell)
{
	static TEXT title[64];

	if (cell->flags & CELL_ISCONTROL)
	{
		SIT_Widget sub;
		for (sub = cell->obj; sub; NEXT(sub))
		{
			layoutGetTextContent(sub, title, sizeof title);
			if (title[0]) return title;
		}
		return "";
	}
	else return cell->obj;
}

static int SIT_ListInsertSort(SIT_ListBox list, APTR rowTag, STRPTR text)
{
	SIT_Callback cb;
	Cell cell;
	int col, cols, start, end;

	if (HAS_EVT(&list->super, SITE_OnSortItem))
		for (cb = HEAD(list->super.callbacks); cb && cb->sc_Event != SITE_OnSortItem; NEXT(cb));
	else
		cb = NULL;
	col  = list->sortColumn;
	cols = list->columnCount;
	cell = STARTCELL(list);
	if (col < 0) col = -col-2;

	for (start = 0, end = list->cells.count / cols; start < end; )
	{
		int  res;
		int  row = (end + start) >> 1;
		Cell middle = cell + row * cols;
		if (cb)
		{
			SIT_OnSort cd = {.item1 = middle->userData, .item2 = rowTag, .column = col};
			res = cb->sc_CB(&list->super, &cd, cb->sc_UserData);

			if (res > 1)
			{
				STRPTR item1 = SIT_ListGetCellBuffer(middle + col);
				switch (res) {
				case SIT_SortAlpha:    res = strcasecmp(item1, text); break;
				case SIT_SortBinary:   res = strcmp(item1, text); break;
				case SIT_SortNumeric:  res = atoi(item1) - atoi(text); break;
				case SIT_SortAlphaNum: res = alnumsort(item1, text);
				}
			}
		}
		else /* use SIT_SortAlpha */
		{
			STRPTR item1 = SIT_ListGetCellBuffer(middle + col);
			res = strcasecmp(item1, text);
		}
		if (list->sortColumn < 0) res = -res;
		if (res == 0) { start = row; break; }
		if (res <  0) start = row + 1;
		else end = row;
	}
	return start;
}

/* column ordering changed */
static void SIT_ListReorder(SIT_ListBox list)
{
	int  cols  = list->columnCount;
	int  items = list->cells.count;
	int  col   = list->sortColumn;
	int  szRow = sizeof (struct Cell_t) * cols;
	Cell tmp   = alloca(szRow);
	APTR sel   = NULL;

	if (col == -1) col = 0; else
	if (col <   0) col = -col-2;
	if (list->selIndex >= 0)
	{
		Cell cell = vector_nth(&list->cells, list->selIndex);
		sel = cell->obj;
	}

	list->cells.count = cols;

	int i;
	for (i = cols; i < items; i += cols)
	{
		Cell row = STARTCELL(list) + i + col;
		int ins = SIT_ListInsertSort(list, row->userData, row->obj) * cols;

		if (ins < i)
		{
			Cell cell = STARTCELL(list) + ins;
			memcpy(tmp, row - col, szRow);
			memmove(cell + cols, cell, (i - ins) * sizeof *row);
			memcpy(cell, tmp, szRow);
		}
		list->cells.count += cols;
	}
	if (sel)
	{
		Cell cell, eof;
		/* restore cursor/selection */
		for (cell = STARTCELL(list), eof = cell + items; cell < eof && cell->obj != sel; cell += cols);
		list->selIndex = cell < eof ? cell - STARTCELL(list) : -1;
	}
	SIT_ListResize(&list->super, NULL, NULL);
}

static Bool IntersectRectF(REAL * rect1, REAL * rect2)
{
	return MIN(rect1[2], rect2[2]) - MAX(rect1[0], rect2[0]) > EPSILON &&
	       MIN(rect1[3], rect2[3]) - MAX(rect1[1], rect2[1]) > EPSILON;
}

/* select all items within the lasso */
static void SIT_ListSelectLasso(SIT_ListBox list)
{
	Cell cell;
	int  col = list->columnCount;
	int  i, row;
	char icon = list->viewMode == SITV_ListViewIcon;
	REAL max  = list->super.layout.pos.height;
	REAL lasso[4];

	if (list->lassoSX > list->lassoEX) lasso[0] = list->lassoEX, lasso[2] = list->lassoSX;
	else                               lasso[0] = list->lassoSX, lasso[2] = list->lassoEX;
	if (list->lassoSY > list->lassoEY) lasso[1] = list->lassoEY, lasso[3] = list->lassoSY;
	else                               lasso[1] = list->lassoSY, lasso[3] = list->lassoEY;

	lasso[1] -= list->scrollTop;
	lasso[3] -= list->scrollTop;

	for (cell = list->rowTop, row = cell - STARTCELL(list), i = list->cells.count - row, row /= col; i > 0; i -= col, row ++, cell += col)
	{
		if (cell->flags & (CELL_HIDDEN|CELL_CATEGORY|CELL_SELECT)) continue;
		REAL bbox[4];

		bbox[0] = cell->sizeCell.left;
		bbox[1] = cell->sizeCell.top  - list->scrollTop;
		bbox[2] = bbox[0] + (icon ? cell->sizeCell.width : list->super.layout.pos.width);
		bbox[3] = bbox[1] + cell->sizeCell.height;

		uint16_t flags = IntersectRectF(lasso, bbox) ? CELL_PRESELECT : 0;

		if (flags != (cell->flags & CELL_PRESELECT))
		{
			if (flags) cell->flags |= CELL_PRESELECT | CELL_SELECT;
			else       cell->flags &= ~(CELL_PRESELECT | CELL_SELECT);

			if (cell->flags & CELL_ISCONTROL)
				SIT_ListUpdateCSS(list, cell);

			/* not needed anymore */
			cell->flags &= ~CELL_SELECT;
		}

		if (bbox[1] > max) break;
	}
}

/* convert CELL_PRESELECT flag into CELL_SELECT */
static void SIT_ListFinishPreselect(SIT_ListBox list)
{
	Cell cell;
	int  i, col = list->columnCount;
	for (cell = STARTCELL(list), i = list->cells.count; i > 0; i -= col, cell += col)
	{
		if (cell->flags & CELL_PRESELECT)
			cell->flags ^= CELL_PRESELECT|CELL_SELECT;
	}
}

static int SIT_ListAutoScroll(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	REAL y = list->scrollTop + list->autoScrollDir * 15;
	REAL max = list->scrollHeight - list->super.layout.pos.height;
	if (y < 0)   y = 0;
	if (y > max) y = max;
	if (y != list->scrollTop)
	{
		if (list->autoScrollDir < 0) list->lassoEY = y;
		else list->lassoEY = y + list->super.layout.pos.height;
		SIT_ListSelectLasso(list);
		SIT_ListScroll(list->super.vscroll, (APTR) (int) y, list);
		SIT_ListAdjustScroll(list);
	}
	return 25;
}

static void SIT_ListStartAutoScroll(SIT_ListBox list, int dir)
{
	static SIT_Action autoScroll;
	int8_t cur = list->autoScrollDir;
	if (dir && cur == 0)
		autoScroll = SIT_ActionAdd(&list->super, sit.curTime + 100, -1, SIT_ListAutoScroll, NULL);

	if (dir == 0 && autoScroll)
		SIT_ActionReschedule(autoScroll, -1, -1), autoScroll = NULL;
	list->autoScrollDir = dir;
}

/* SITE_OnClick inside list */
static int SIT_ListClick(SIT_Widget w, APTR cd, APTR ud)
{
	/* check which item has been clicked on */
	SIT_ListBox   list = (SIT_ListBox) w;
	SIT_OnMouse * msg  = cd;
	REAL          max  = w->layout.pos.height;
	REAL          x, y;
	Cell          cell;
	int           i, j;

	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button != 0)
			return 0;
		x = msg->x - w->layout.padding.left;
		y = msg->y - w->layout.padding.top;

		if ((list->lbFlags & SITV_NoHeaders) == 0 && (list->lbFlags & SITV_DoSort) && list->viewMode == SITV_ListViewReport && y < list->hdrHeight)
		{
			/* click on headers */
			REAL left = 0;
			for (i = list->columnCount, cell = list->columns; i > 0; i --, left += cell->sizeCell.width, cell ++)
				if (left <= x && x < left + cell->sizeCell.width) break;

			if (i > 0)
			{
				int cur = list->sortColumn;
				if (cur < 0) cur = -cur-2;
				i = list->columnCount - i;
				if (i == cur && list->sortColumn >= 0) i = -i-2;
				if (HAS_EVT(w, SITE_OnSortColumn) && SIT_ApplyCallback(w, (APTR) i, SITE_OnSortColumn))
				{
					list->sortColumn = i;
					SIT_ListReorder(list);
					sit.dirty = 1;
				}
				return 1;
			}
			break;
		}
		else if ((list->lbFlags & SITV_SelectNone) == 0)
		{
			int  col = list->softColumn;
			if (col <= 0) col = 1;
			for (cell = list->rowTop, i = list->cells.count - (cell - STARTCELL(list)); i > 0; )
			{
				Cell rowStart = cell;
				if (cell->flags & CELL_HIDDEN)
				{
					i -= col;
					cell += col;
					continue;
				}
				for (j = MIN(col, i); j > 0; i --, j --, cell ++)
				{
					REAL top = cell->sizeCell.top - list->scrollTop;

					if (y < top || top > max) { i = 0; break; }
					if (y > top + cell->sizeCell.height)
					{
						if ((cell->flags & CELL_CATEGORY) == 0)
						{
							j = cell->colLeft;
							i -= j;
							cell += j;
						}
						else i --, cell ++;
						break;
					}
					if (cell->sizeCell.left <= x && x < cell->sizeCell.left + cell->sizeCell.width)
					{
						static uint32_t lastClick;
						Cell row = list->viewMode == SITV_ListViewReport ? rowStart : cell;
						if (row == vector_nth(&list->cells, list->selIndex) && TimeMS() - lastClick < sit.dblClickMS)
						{
							SIT_ApplyCallback(w, row->userData, SITE_OnActivate);
						}
						else SIT_ListSetSelection(list, cell, (msg->flags & SITK_FlagCtrl) == 0, (msg->flags & SITK_FlagShift) > 0);
						lastClick = TimeMS();
						if (list->lbFlags & SITV_SelectMultiple)
						{
							/* start lasso selection */
							list->lassoSX = list->lassoEX = x;
							list->lassoSY = y + list->scrollTop;
							return 2;
						}
						return 1;
					}
				}
			}
		}
		/* didn't click over an item */
		if ((list->lbFlags & SITV_SelectAlways) == 0 && (msg->flags & SITK_FlagCtrl) == 0)
		{
			/* didn't click on any item: deselect the one selected then */
			SIT_ListSetSelection(list, NULL, True, False);
		}

		/* start lasso selection */
		if (list->lbFlags & SITV_SelectMultiple)
		{
			list->lassoSX = list->lassoEX = x;
			list->lassoSY = y + list->scrollTop;
			return 2;
		}
		else break;

	case SITOM_CaptureMove:
		if (list->lbFlags & SITV_SelectMultiple)
		{
			list->lassoEX = msg->x;
			list->lassoEY = msg->y + list->scrollTop;
			SIT_ListSelectLasso(list);
			sit.dirty = 1;
			if (msg->y < -w->padding[1])
			{
				/* vertical auto-scroll */
				SIT_ListStartAutoScroll(list, -1);
				return 1;
			}
			else if (msg->y > w->layout.pos.height)
			{
				SIT_ListStartAutoScroll(list, 1);
				return 1;
			}
			else /* stop auto-scrolling */
			{
				SIT_ListStartAutoScroll(list, 0);
			}
		}
		else if (list->super.vscroll) /* scroll content of list */
		{
			max = list->scrollHeight - list->super.layout.pos.height;
			y = list->lassoSY - msg->y;
			if (y < 0)   y = 0;
			if (y > max) y = max;
			if (y != list->scrollTop)
			{
				SIT_ListScroll(list->super.vscroll, (APTR) (int) y, list);
				SIT_ListAdjustScroll(list);
			}
		}
		break;

	case SITOM_ButtonReleased:
		if ((list->lbFlags & SITV_SelectMultiple) == 0)
			return 0;
		SIT_ListStartAutoScroll(list, 0);
		SIT_ListFinishPreselect(list);
		list->lassoEX = list->lassoSX = 0;
		sit.dirty = 1;

	default: break;
	}
	return 0;
}

static void SIT_ListClearAndFree(SIT_Widget td)
{
	if (td)
	{
		td->parent = NULL;
		td->title  = NULL;
		ListNew(&td->children);
		SIT_DestroyWidget(td);
	}
}

static void SIT_ListFreeCells(SIT_ListBox list, Cell start, int count)
{
	Cell cell;
	SIT_CallProc cb = list->finalizeItem;
	for (cell = start; count > 0; count --, cell ++)
	{
		if (cb && cell->userData)
			cb(&list->super, NULL, cell->userData);

		if ((cell->flags & CELL_ISCONTROL) && cell->obj)
		{
			SIT_Widget td = cell->obj;
			do {
				SIT_Widget next = (SIT_Widget) td->node.ln_Next;
				td->parent = NULL;
				SIT_DestroyChildren(td);
				SIT_DestroyWidget(td);
				td = next;
			} while (td);
		}
	}
}

static int SIT_ListFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	APTR str;
	Cell cell;
	int  i;

	if (list->viewMode == SITV_ListViewReport)
	{
		/* headers */
		for (i = list->columnCount, cell = list->columns; i > 0; i --, cell ++)
		{
			if (cell->obj == NULL) continue;
			SIT_DestroyChildren(cell->obj);
			SIT_DestroyWidget(cell->obj);
		}
	}

	if (list->columns)      free(list->columns);
//	if (list->columnNames)  free(list->columnNames);
//	if (list->columnWidths) free(list->columnWidths);
//	if (list->columnAlign)  free(list->columnAlign);
	if (list->realWidths)   free(list->realWidths);

	SIT_ListClearAndFree(list->td);
	SIT_ListClearAndFree(list->tdSel);
	SIT_ListClearAndFree(list->thead);

	while ((str = ListRemHead(&list->strPool)))
		free(str);

	SIT_ListFreeCells(list, STARTCELL(list), list->cells.count);
	vector_free(list->cells);
	return 0;
}

static Cell SIT_ListMovePage(SIT_ListBox list, int dir)
{
	if ((list->lbFlags & SITV_HasScroll) == 0) return NULL;

	int  page = list->super.layout.pos.height - list->hdrHeight;
	int  top  = list->scrollTop + dir * page;
	int  max  = list->scrollHeight - page;
	Cell sel  = STARTCELL(list);
	Cell eof  = sel + list->cells.count;

	if (top > max) top = max;
	if (top < 0)   top = 0;
	if (top != list->scrollTop)
	{
		SIT_ListScroll(list->super.vscroll, (APTR) top, list);
		SIT_SetValues(list->super.vscroll, SIT_ScrollPos, top, NULL);
		sit.dirty = 1;
		if (list->selIndex >= 0)
		{
			Cell topRow = list->rowTop;
			if (topRow->sizeCell.top < list->scrollTop)
				topRow += topRow->colLeft;
			sel += list->selIndex;
			/* select the first/last visible item */
			if (sel < topRow)
			{
				while (sel->sizeCell.top - list->scrollTop < 0)
					sel += sel->colLeft;
			}
			else
			{
				max = list->scrollTop + list->super.layout.pos.height;
				while (sel->sizeCell.top + sel->sizeCell.height > max)
					sel -= sel->colLeft;
			}
			return sel;
		}
	}
	/* if view cannot move, move cursor */
	else if (dir < 0)
	{
		while (sel < eof && (sel->flags & CELL_CATEGORY))
			sel ++;

		if (sel - STARTCELL(list) != list->selIndex)
			return sel;
	}
	else
	{
		for (eof -= list->columnCount; eof > sel && (eof->flags & CELL_CATEGORY); eof -= list->columnCount);

		if (eof - STARTCELL(list) != list->selIndex)
			return eof;
	}
	return NULL;
}

static int SIT_ListKeyboard(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	SIT_OnKey * msg  = cd;

	if ((list->lbFlags & SITV_SelectNone) == 0 && list->cells.count > 0)
	{
		Cell cell, end;
		if ((list->selIndex == -1 && (msg->keycode == SITK_Up || msg->keycode == SITK_Down)) || msg->keycode == SITK_Home)
		{
			end = vector_nth(&list->cells, list->cells.count-1);
			if (msg->keycode == SITK_Up)
			{
				/* select last item */
				for (cell = end, end = STARTCELL(list); cell >= end && ((cell->flags & CELL_COLSTART) == 0 || (cell->flags & CELL_HIDDEN)); cell --);
				if (cell >= end)
				{
					SIT_ListSetSelection(list, cell, True, False);
					return 1;
				}
			}
			else
			{
				/* select first item */
				for (cell = STARTCELL(list); cell <= end && (cell->flags & (CELL_CATEGORY|CELL_HIDDEN)); cell ++);
				if (cell <= end)
				{
					SIT_ListSetSelection(list, cell, True, False);
					return 1;
				}
			}
			return 0;
		}
		cell = vector_nth(&list->cells, list->selIndex);

		switch (msg->keycode) {
		case SITK_Left:
			end = STARTCELL(list);
			for (cell --; cell >= end && (cell->flags & (CELL_CATEGORY|CELL_HIDDEN)); cell --);
			if (cell < end) return 0;
			break;
		case SITK_Right:
			end = STARTCELL(list) + (list->cells.count-1);
			for (cell ++; cell < end && (cell->flags & (CELL_CATEGORY|CELL_HIDDEN)); cell ++);
			if (cell == end) return 0;
			break;
		case SITK_Down:
		{	int nth;
			for (nth = 0, end = cell; (end->flags & CELL_COLSTART) == 0; end --, nth ++);
			end = vector_nth(&list->cells, list->cells.count-1);
			for (cell += cell->colLeft; cell <= end && (cell->flags & CELL_CATEGORY); cell += cell->colLeft);
			if (cell > end) return 0;
			if (nth >= cell->colLeft) nth = cell->colLeft-1;
			cell += nth;
		}	break;
		case SITK_Up:
		{	int nth;
			for (nth = 0, end = cell; (cell->flags & CELL_COLSTART) == 0; cell --, nth ++);
			for (cell --; cell >= STARTCELL(list) && (cell->flags & CELL_CATEGORY); cell --);
			while (cell >= STARTCELL(list) && (cell->flags & CELL_COLSTART) == 0) cell --;
			if (cell < STARTCELL(list)) return 0;
			if (nth >= cell->colLeft) nth = cell->colLeft-1;
			cell += nth;
		}	break;
		case SITK_PrevPage:
			cell = SIT_ListMovePage(list, -1);
			if (! cell) return 0;
			break;
		case SITK_NextPage:
			cell = SIT_ListMovePage(list, 1);
			if (! cell) return 0;
			break;
		case SITK_End:
			end = vector_nth(&list->cells, list->cells.count-1);
			while (end > STARTCELL(list) && (end->flags & CELL_CATEGORY)) end --;
			if (cell == end) return 0;
			cell = end;
			break;
		case SITK_Return:
			SIT_ApplyCallback(w, cell->userData, SITE_OnActivate);
			return 1;
		#ifndef SIT_ListDebug
		case SITK_Space:
			SIT_ListDebug(list);
		#endif
		default: return 0;
		}

		textLength = 0;
		SIT_ListSetSelection(list, cell, True, (msg->flags & SITK_FlagShift) > 0);

		return 1;
	}
	return 0;
}

/* try to find the text entered by user */
static int SIT_ListAutoComplete(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	SIT_OnKey * msg  = cd;

	if (msg)
	{
		if ((list->lbFlags & SITV_SelectMultiple) && msg->utf8[0] == 1 /* ctrl + a */)
		{
			/* select all instead */
			int old = list->selIndex;
			list->selIndex = -1;
			SIT_ListSetSelection(list, vector_nth(&list->cells, list->cells.count-1), False, 2);
			list->selIndex = old;
			return 1;
		}
		int len = strlen(msg->utf8);
		if (msg->utf8[0] == 9)
		{
			if (textLength > 0)
				textLength --;
		}
		else if (msg->utf8[0] < 32)
		{
			return 0;
		}
		else if (textLength + len <= sizeof textLookAhead)
		{
			memcpy(textLookAhead + textLength, msg->utf8, len);
			textLength += len;
		}
		else return 0;
	}

	/* look for a string that begins with <textLookAhead> */
	Cell cell;
	int  i, col = list->columnCount;
	for (i = 0, cell = STARTCELL(list); i < list->cells.count; i += col, cell += col)
	{
		if (cell->flags & CELL_CATEGORY) continue;
		if (strncasecmp(SIT_ListGetCellBuffer(cell), textLookAhead, textLength) == 0)
		{
			SIT_ListSetSelection(list, cell, True, False);
			return 1;
		}
	}
	return 1;
}

static void SIT_ListSetColumns(SIT_ListBox list)
{
	int count = StrCount(list->columnNames, '\t') + 1;

	list->columnCount = count;
	list->columns = calloc(sizeof (struct Cell_t), list->columnCount);
	list->realWidths = calloc(sizeof (REAL), list->columnCount);

	if (list->viewMode == SITV_ListViewReport)
	{
		STRPTR * columns = alloca(sizeof *columns * count);
		STRPTR   widths  = list->columnWidths;
		REAL     total   = 0;
		TEXT     name[16];
		int      i;

		list->softColumn = list->columnCount;
		if (list->columnNames)
			Split(columns, STRDUPA(list->columnNames), count, '\t');
		else
			columns[0] = "";

		for (i = 0; i < count; i ++)
		{
			Cell cell = list->columns + i;
			sprintf(name, "column%d#th", i);
			if ((list->lbFlags & SITV_NoHeaders) == 0)
			{
				SIT_Widget label = SIT_CreateWidget(name, SIT_LABEL, &list->super, SIT_Title, columns[i], NULL);
				cell->obj = label;
				cell->flags |= CELL_ISCONTROL;
				label->style.overflow = SITV_EllipsisRight;
				ListRemove(&list->super.children, &label->node);
			}
			REAL w = 1;
			if (widths)
			{
				w = strtod(widths, &widths);
				if (w <= 0) w = 1;
				widths = strchr(widths, '\t');
				if (widths) widths ++;
			}
			list->realWidths[i] = w;
			total += w;
		}
		if (total > 0 && list->columnWidths)
		{
			/* normalize between 0 and 1 */
			REAL * norm;
			for (i = 0, norm = list->realWidths; i < count; i ++, norm ++)
				norm[0] = norm[0] / total;
		}
		if (IsDef(list->columnAlign))
		{
			Split(columns, STRDUPA(list->columnAlign), count, '\t');
			for (i = 0; i < count; i ++)
			{
				Cell cell = list->columns + i;
				cell->flags &= ~(CELL_ALIGNR|CELL_ALIGNC);
				if (columns[i] == NULL) break;
				switch (columns[i][0]) {
				case 'C': cell->flags |= CELL_ALIGNC; break;
				case 'R': cell->flags |= CELL_ALIGNR;
				}
			}
		}
	}
	else if (list->columnAlign)
	{
		/* can also be changed using CSS text-align property */
		switch (list->columnAlign[0]) {
		case 'L': list->defAlign = TextAlignLeft; break;
		case 'C': list->defAlign = TextAlignCenter; break;
		case 'R': list->defAlign = TextAlignRight; break;
		default:  list->defAlign = -1;
		}
	}
}

static Cell SIT_ListGetNth(SIT_ListBox list, int nth)
{
	int col = nth >> 24; nth &= 0xffffff;
	if (col < 0 || col >= list->columnCount)
		return NULL;
	nth *= list->columnCount;
	if (nth < 0 || nth+col >= list->cells.count) return NULL;
	return (Cell) vector_nth(&list->cells, nth + col);
}

static int SIT_SetListBoxValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Variant val = ud;
	TagList     tag = cd;
	SIT_ListBox list = (SIT_ListBox) w;
	Cell        cell;

	if (cd == NULL)
	{
		/* post process callback */
		if (list->lbFlags & SITV_ColumnChanged)
		{
			SIT_ListSetColumns(list);
			list->lbFlags ^= SITV_ColumnChanged;
		}
	}
	else switch (tag->tl_TagID) {
	case SIT_Title:
		/* SIT_Title acts like a placeholder */
		layoutFree(w);
		layoutParseHTML(w, w->title = val->string);
		break;
	case SIT_ColumnWidths:
	case SIT_ColumnNames:
	case SIT_ColumnAlign:
		list->lbFlags |= SITV_ColumnChanged;
		w->postProcess = SIT_SetListBoxValues;
		memcpy((DATA8) w + tag->tl_Arg, &val->string, sizeof val->string);
		break;
	case SIT_SelectedIndex:
		cell = SIT_ListGetNth(list, val->integer);
		SIT_ListSetSelection(list, cell, True, False);
		break;
	case SIT_MakeVisible:
		cell = SIT_ListGetNth(list, val->integer);
		SIT_ListMakeVisible(list, cell);
		break;
	case SIT_RowSelArg:
		cell = SIT_ListGetNth(list, list->curRow);
		SIT_ListSetSelection(list, cell, False, False);
		break;
	case SIT_RowTagArg:
		cell = SIT_ListGetNth(list, list->curRow);
		if (cell) cell->userData = val->pointer;
		break;
	case SIT_AutoComplete:
		CopyString(textLookAhead, val->pointer, sizeof textLookAhead);
		textLength = strlen(textLookAhead);
		SIT_ListAutoComplete(w, NULL, NULL);
		break;
	default:
		SIT_SetWidgetValue(w, cd, ud);
	}

	return 1;
}

void SIT_ListGetArg(SIT_Widget w, int type, APTR arg)
{
	SIT_ListBox list = (SIT_ListBox) w;
	Cell cell = SIT_ListGetNth(list, list->curRow);
	switch (type) {
	case 0: * (Bool *) arg = cell && (cell->flags & CELL_SELECT) > 0; break;
	case 1: * (APTR *) arg = cell ? cell->userData : NULL; break;
	}
}

/* SITE_OnFocus */
static int SIT_ListResetSearch(SIT_Widget w, APTR cd, APTR ud)
{
	textLength = 0;
	return 1;
}

int SIT_ListGetItemCount(SIT_Widget w)
{
	SIT_ListBox list = (SIT_ListBox) w;

	if (list->viewMode == SITV_ListViewIcon)
	{
		/* ignore categories */
		return list->cells.count - list->catCount;
	}
	else return list->cells.count / list->columnCount;
}


static void SIT_ListClearCell(SIT_ListBox list, Cell cell, int flags)
{
	cell->flags &= ~CELL_HASSIZE;
	memset(&cell->sizeObj, 0, sizeof cell->sizeObj + sizeof cell->sizeCell);
	if (cell->flags & CELL_ISCONTROL)
	{
		SIT_ListRestoreChildren(list->td, cell);
		SIT_ChangeChildrenStyle(list->td, flags);
	}
}

/* stylesheet changed/screen resized: recompute everything */
void SIT_ListClearStyles(SIT_Widget w, int flags)
{
	SIT_ListBox list = (SIT_ListBox) w;
	Cell cell;
	int  i;

	list->lbFlags &= ~SITV_ListMeasured;
	layoutClearStyles(list->td, flags);
	layoutClearStyles(list->tdSel, flags);
	for (i = list->columnCount, cell = list->columns;   i > 0; SIT_ListClearCell(list, cell, flags), i --, cell ++);
	for (i = list->cells.count, cell = STARTCELL(list); i > 0; SIT_ListClearCell(list, cell, flags), i --, cell ++);
}


Bool SIT_InitListBox(SIT_Widget w, va_list args)
{
	SIT_ListBox list = (SIT_ListBox) w;

	/* default values */
	list->selIndex  = list->sortColumn = -1;
	list->defAlign  = -1;
	w->optimalWidth = SIT_ListMeasure;
	w->finalize     = SIT_ListFinalize;
	w->render       = SIT_ListRender;
	w->setValue     = SIT_SetListBoxValues;
	w->flags       |= SITF_RenderChildren;
	vector_init_zero(list->cells, sizeof (struct Cell_t));

	SIT_AddCallback(w, SITE_OnResize,     SIT_ListResize, NULL);
	SIT_AddCallback(w, SITE_OnClickMove,  SIT_ListClick, NULL);
	SIT_AddCallback(w, SITE_OnRawKey,     SIT_ListKeyboard, NULL);
	SIT_AddCallback(w, SITE_OnVanillaKey, SIT_ListAutoComplete, NULL);
	SIT_AddCallback(w, SITE_OnFocus,      SIT_ListResetSearch, NULL);
	SIT_ParseTags(w, args, w->attrs = ListBoxClass);

	if (list->viewMode == SITV_ListViewReport)
		w->tagName = "table";

	layoutCalcBox(w);

	/* avoid allocating one HTMLTAG per cell: will considerably reduce the amount of memory needed */
	SIT_Widget td;
	list->td = td = SIT_CreateWidget("td", SIT_HTMLTAG, w, NULL);
	td->flags |= SITF_PrivateChildren;
	td->style.overflow = SITV_EllipsisRight;
	memcpy(td->attachment, &td->style.color, 4);

	/* must not be rendered in SIT_RenderNode() */
	ListRemove(&w->children, &list->td->node);

	if ((list->lbFlags & SITV_SelectNone) == 0)
	{
		list->tdSel = td = SIT_CreateWidget("td", SIT_HTMLTAG, w, NULL);
		td->state |= STATE_CHECKED;
		td->style.flags &= ~CSSF_APPLIED;
		td->flags |= SITF_PrivateChildren;
		layoutCalcBox(td);
		td->style.overflow = SITV_EllipsisRight;
		ListRemove(&w->children, &td->node);
	}

	SIT_ListSetColumns(list);
	w->postProcess = NULL;
	list->lbFlags &= ~ SITV_ColumnChanged;

	return True;
}

/* item added at runtime: recompute layout and/or node position (icon view only) */
static int SIT_ListRecalcItemPos(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_ListBox list = (SIT_ListBox) w;
	REAL        maxw = 0;
	Cell        cell;
	int         count, i;

	if (list->viewMode == SITV_ListViewIcon)
	{
		for (i = list->recalcCell, count = list->cells.count, cell = vector_nth(&list->cells, i); i < count; i ++, cell ++)
		{
			if (cell->flags & CELL_HIDDEN) continue;
			if ((cell->flags & CELL_HASSIZE) == 0)
				SIT_ListCalcSize(list->td, cell, (int) ud);

			if (cell->flags & CELL_CATEGORY)
				continue;

			if (maxw < cell->sizeObj.width)
				maxw = cell->sizeObj.width;
		}
		if (maxw != list->maxCellW)
		{
			for (i = list->recalcCell, cell = STARTCELL(list); i > 0; i --, cell ++)
			{
				if (cell->flags & CELL_HIDDEN) continue;
				if (maxw < cell->sizeObj.width)
					maxw = cell->sizeObj.width;
			}
			if (maxw != list->maxCellW)
			{
				list->maxCellW = maxw;
				/* will have to change pos of item: recalc everything */
				list->recalcCell = 0;
			}
		}
		if (list->maxRowVisible > 0)
			SIT_ListAdjustMaxHeight(list);

		/* all cells have same width, but can have different height */
		for (cell = STARTCELL(list), i = list->cells.count; i > 0; i --, cell ++)
			if ((cell->flags & (CELL_HIDDEN|CELL_CATEGORY)) == 0)
				cell->sizeCell.width = maxw;
	}

	count = list->lbFlags & SITV_ReorgColumns;
	list->lbFlags &= ~(SITV_PendingRecalc | SITV_ReorgColumns);

	if (count)
		SIT_ListReorgColumns(w, list->columnAlign);

	SIT_ListResize(w, cd, ud);

	return 0;
}

static STRPTR SIT_ListAddPool(SIT_ListBox list, int length)
{
	StrPool pool = malloc(sizeof *pool + length - 1);
	ListAddTail(&list->strPool, &pool->node);
	return pool->mem;
}

static void SIT_ListStartRecalc(SIT_ListBox list, int pos)
{
	if ((list->lbFlags & SITV_PendingRecalc) == 0)
	{
		SIT_ActionAdd(&list->super, sit.curTime, -1, SIT_ListRecalcItemPos, (APTR) FitUsingInitialBox);
		list->lbFlags |= SITV_PendingRecalc;
		list->recalcCell = pos;
	}
	else if (list->recalcCell > pos)
	{
		list->recalcCell = pos;
	}
}

DLLIMP int SIT_ListInsertItem(SIT_Widget w, int row, APTR rowTag, ...)
{
	SIT_ListBox list = (SIT_ListBox) w;
	STRPTR      utf8;
	va_list     args;
	int         i, cols, len, top;
	int         oldVis;

	if (w == NULL || w->type != SIT_LISTBOX) return -1;
	if (row < 0)
		row = list->rowCount;

	if (list->sortColumn != -1)
	{
		i = list->sortColumn;
		if (i < 0) i = -i-2;
		for (va_start(args, rowTag), utf8 = va_arg(args, STRPTR); i > 0; i --)
		{
			utf8 = va_arg(args, STRPTR);
			if (utf8 == NULL) break;
		}
		va_end(args);
		if (utf8 && utf8 != SITV_TDSubChild)
			row = SIT_ListInsertSort(list, rowTag, utf8);
		else
			row = list->rowCount; /* will have to be sorted later */
	}

	oldVis = list->catVisible < 2;
	cols = list->columnCount;
	if (list->selIndex >= row * cols)
		list->selIndex += cols;
	list->rowCount ++;
	i = list->rowCount * cols;
	top = list->rowTop ? list->rowTop - STARTCELL(list) : 0;
	vector_nth(&list->cells, i-1);
	list->rowTop = vector_nth(&list->cells, top);

	Cell cells = STARTCELL(list);
	Cell cell;
	cells += row * cols;
	if (row < list->rowCount-1)
		memmove(cells + cols, cells, (list->rowCount - 1 - row) * cols * sizeof *cells);
	memset(cells, 0, cols * sizeof *cells);
	if (rowTag != SITV_CategoryRow)
	{
		/* check if there is a hidden category before: now it will be visible */
		if (list->catCount > 0 && cells > (Cell) list->cells.buffer)
		{
			Cell cat = cells - 1;
			if ((cat->flags & (CELL_CATVISIBLE|CELL_CATEGORY)) == CELL_CATEGORY)
			{
				cat->flags |= CELL_CATVISIBLE;
				list->catVisible ++;
			}
		}
		/* an item has been added: hide placeholder text */
		w->layout.flags |= LAYF_IgnoreWords;
	}
	else /* category */
	{
		/* hidden unless there are at least 2 with at least one item in it */
		list->catCount ++;
		if (list->thead == NULL)
		{
			SIT_Widget td = SIT_CreateWidget("thead", SIT_HTMLTAG, w, NULL);
			list->thead = td;
			ListRemove(&w->children, &td->node);
		}
	}

	/* get text size */
	for (i = len = 0, va_start(args, rowTag), cell = cells; i < cols && (utf8 = va_arg(args, STRPTR)); i ++, cell ++)
	{
		if (rowTag == SITV_CategoryRow)
			cell->flags |= CELL_CATEGORY;
		else
			cell->userData = rowTag;
		if (utf8 != SITV_TDSubChild)
			len += strlen(utf8) + 1;
		else
			cell->flags |= CELL_ISCONTROL;
		cell->colLeft = cols - i;
	}
	va_end(args);

	/* assign text */
	if (len > 0)
	{
		STRPTR string = SIT_ListAddPool(list, len);

		for (i = len = 0, va_start(args, rowTag), cell = cells; i < cols && (utf8 = va_arg(args, STRPTR)); i ++, cell ++)
		{
			if (utf8 != SITV_TDSubChild)
			{
				cell->obj = string;
				strcpy(string, utf8);
				string += strlen(utf8) + 1;
			}
		}
		cells->flags |= CELL_COLSTART;
	}

	if (w->optimalBox.width >= 0)
	{
		/* layout done: item added after being displayed: recompute node position next update cycle */
		SIT_ListStartRecalc(list, cells - (Cell) list->cells.buffer);
		/* category become visible: recalc everything */
		if (oldVis != (list->catVisible < 2))
			list->recalcCell = 0;
	}

	return row;
}

static StrPool SIT_ListGetStrPool(SIT_ListBox list, Cell row)
{
	StrPool str;
	STRPTR  mem;
	int     i;
	for (i = list->columnCount; i > 0 && (row->flags & CELL_ISCONTROL); i --, row ++);
	if (i == 0) return NULL;
	for (str = HEAD(list->strPool), mem = row->obj; str->mem != mem; NEXT(str));
	return str;
}

/* cell being removed: adjust visual layout */
static void SIT_ListCellRemoved(SIT_ListBox list, int row)
{
	Cell cells = STARTCELL(list) + row;
	if (list->viewMode == SITV_ListViewIcon)
	{
		/* icon view: recalc everything following item deleted */
		int nth = row - (row == list->cells.count-1);
		/* check if category needs to be hidden */
		if (list->catVisible > 1)
		{
			if (cells > STARTCELL(list) && (cells[-1].flags & CELL_CATEGORY))
			{
				Cell last = vector_nth(&list->cells, list->cells.count-1);
				if (cells == last || (cells[1].flags & CELL_CATEGORY))
				{
					cells --;
					nth --;
					cells->flags &= ~CELL_CATVISIBLE;
					list->catVisible --;
					if (list->catVisible < 2)
						nth = 0;
				}
			}
		}
		if (nth < 0)
		{
			/* no more items to display */
			list->scrollTop = 0;
			list->scrollHeight = 0;
			SIT_ListAdjustScroll(list);
		}
		else SIT_ListStartRecalc(list, nth);
	}
	else SIT_ListStartRecalc(list, row);
}

/* delete one row/item in the list */
DLLIMP void SIT_ListDeleteRow(SIT_Widget w, int row)
{
	SIT_ListBox list = (SIT_ListBox) w;
	int cols, rows;
	if (w == NULL || w->type != SIT_LISTBOX || list->cells.count == 0)
		return;
	if (row == DeleteAllRows)
	{
		/* start from scratch */
		SIT_ListFreeCells(list, STARTCELL(list), list->cells.count);

		/* need to be changed before callback */
		list->selIndex = -1;
		if (list->selIndex >= 0 && HAS_EVT(&list->super, SITE_OnChange))
			SIT_ApplyCallback(&list->super, NULL, SITE_OnChange);
		list->cells.count = 0;
		list->rowCount = 0;
		list->scrollTop = 0;
		list->scrollHeight = 0;
		list->catCount = 0;
		list->catVisible = 0;
		list->rowTop = NULL;
		vector_free(list->cells);
		vector_init_zero(list->cells, sizeof (struct Cell_t));
		APTR str;
		while ((str = ListRemHead(&list->strPool)))
			free(str);
		if (list->lbFlags & SITV_HasScroll)
			SIT_ListAdjustScroll(list);
		sit.dirty = 1;
		return;
	}
	rows = list->rowCount-1;
	if (row == -1)
		row = rows;
	else if (row > rows)
		row = rows;
	list->rowCount --;
	cols = list->columnCount;
	row *= cols;
	Cell cells = STARTCELL(list) + row;
	if (cells->flags & CELL_CATEGORY)
		list->catCount --;
	if (list->selIndex == row)
	{
		list->selIndex = -1;
		if ((list->lbFlags & SITV_SelectMultiple) == 0 && HAS_EVT(&list->super, SITE_OnChange))
			SIT_ApplyCallback(&list->super, NULL, SITE_OnChange);
	}
	else if (list->selIndex > row)
	{
		list->selIndex -= cols;
	}

	StrPool str = SIT_ListGetStrPool(list, cells);
	if (str)
	{
		ListRemove(&list->strPool, &str->node);
		free(str);
	}
	SIT_ListCellRemoved(list, row);
	SIT_ListFreeCells(list, cells, cols);
	memmove(cells, cells + cols, (rows * cols - row) * sizeof *cells);
	list->cells.count -= cols;
	if (list->maxRowVisible > 0)
		SIT_ListAdjustMaxHeight(list);
	sit.dirty = 1;
}

/* show/hide a row; typical use case: filtering items of a list */
DLLIMP void SIT_ListSetRowVisibility(SIT_Widget w, int row, Bool visible)
{
	SIT_ListBox list = (SIT_ListBox) w;
	int cols, rows;
	if (w == NULL || w->type != SIT_LISTBOX || list->cells.count == 0)
		return;

	rows = list->rowCount-1;
	if (row == -1)
		row = rows;
	else if (row > rows)
		row = rows;

	cols = list->columnCount;
	row *= cols;
	Cell cells = STARTCELL(list) + row;
	if (cells->flags & CELL_CATEGORY)
		list->catCount --;
	uint16_t flags = cells->flags;
	if (visible) flags &= ~CELL_HIDDEN;
	else         flags |= CELL_HIDDEN;
	if (cells->flags == flags) return;

	if (list->selIndex == row)
	{
		cells->flags &= ~CELL_SELECT;
		if (cells->flags & CELL_ISCONTROL)
			SIT_ListUpdateCSS(list, cells);
		if ((list->lbFlags & SITV_SelectMultiple) == 0 && HAS_EVT(&list->super, SITE_OnChange))
			SIT_ApplyCallback(&list->super, NULL, SITE_OnChange);
		list->selIndex = -1;
	}

	Cell cell;
	for (cell = cells; cols > 0; cols --, cell ++)
	{
		if (visible) cell->flags &= ~CELL_HIDDEN;
		else         cell->flags |=  CELL_HIDDEN;
	}

	if (w->optimalBox.width >= 0)
		SIT_ListCellRemoved(list, cells - (Cell) list->cells.buffer);
	sit.dirty = 1;
}


/* add sub-control into a list */
DLLIMP SIT_Widget SIT_ListInsertControlIntoCell(SIT_Widget w, int row, int col)
{
	if (w == NULL || w->type != SIT_LISTBOX) return NULL;
	SIT_ListBox list = (SIT_ListBox) w;
	SIT_Widget  td   = list->td;

	if (td->userData)
		SIT_ListFinishInsertControl(w);

	if (row < 0)
		row = list->rowCount-1;
	if (list->viewMode == SITV_ListViewIcon)
		col = 0;

	int nth = row * list->columnCount + col;

	/* need to be pre-allocated */
	if (nth < 0 || nth >= list->cells.count)
		return NULL;

	Cell cell = vector_nth(&list->cells, nth);

	if ((cell->flags & CELL_ISCONTROL) == 0)
		return NULL;

	td->userData = cell;
	SIT_ListRestoreChildren(td, cell);

	return td;
}

/* need to call this one before inserting next one */
DLLIMP void SIT_ListFinishInsertControl(SIT_Widget w)
{
	if (w == NULL || w->type != SIT_LISTBOX) return;

	SIT_ListBox list = (SIT_ListBox) w;
	SIT_Widget  td   = list->td;
	Cell        cell = td->userData;

	if (cell == NULL) return;

	cell->obj = HEAD(td->children);

	ListNew(&td->children);
	td->userData = NULL;

	if (list->sortColumn != -1)
	{
		/* now we can sort insert that item */
		TEXT buffer[64];
		Cell start = STARTCELL(list);
		int  cols = list->columnCount;
		int  index;

		/* control item will always be inserted at the end if sorting is activated */
		list->cells.count -= cols;
		CopyString(buffer, SIT_ListGetCellBuffer(cell), sizeof buffer);
		index = SIT_ListInsertSort(list, cell->userData, buffer) * cols;

		if (index != cell - start)
		{
			/* need to shift some items */
			Cell tmp = alloca(sizeof *tmp * cols);
			memcpy(tmp, cell, sizeof *tmp * cols);
			cell = start + index;
			memmove(cell + cols, cell, (list->cells.count - index) * sizeof *cell);
			memcpy(cell, tmp, sizeof *tmp * cols);
		}
		list->cells.count += cols;
	}

	if (w->optimalBox.width >= 0)
	{
		/* layout done: item added after being displayed: recompute node position next update cycle */
		cell->flags &= ~CELL_HASSIZE;
		SIT_ListStartRecalc(list, cell - (Cell) list->cells.buffer);
	}
}

DLLIMP Bool SIT_ListSetCell(SIT_Widget w, int row, int col, APTR rowTag, int align, STRPTR text)
{
	SIT_ListBox list = (SIT_ListBox) w;
	if (w == NULL || w->type != SIT_LISTBOX)
		return False;

	int count = list->columnCount;
	if (col < 0) col = 0;
	if (col >= count) col = count-1;

	Cell cell = SIT_ListGetNth(list, row);
	if (cell == NULL) return False;

	/* rowTag will always be set on first column (because that the only column SIT_RowTag can retrieve) */
	if (rowTag != DontChangePtr)
		cell->userData = rowTag;
	cell += col;

	if (align > 0)
	{
		while (align & 0xff)
		{
			switch (align & 0xff) {
			case 'R': cell->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); cell->flags |= CELL_ALIGNR; break;
			case 'C': cell->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); cell->flags |= CELL_ALIGNC; break;
			case 'L': cell->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); break;
			case 'B': cell->flags &= ~(CELL_VALIGNB | CELL_VALIGNC); cell->flags |= CELL_VALIGNB; break;
			case 'M': cell->flags &= ~(CELL_VALIGNB | CELL_VALIGNC); cell->flags |= CELL_VALIGNC; break;
			case 'T': cell->flags &= ~(CELL_VALIGNB | CELL_VALIGNC); break;
			}
			align >>= 8;
		}
	}

	if (text != DontChangePtr)
	{
		if (cell->flags & CELL_ISCONTROL)
			SIT_ListFreeCells(list, cell, 1);

		cell->flags &= ~CELL_ISCONTROL;
		StrPool str = SIT_ListGetStrPool(list, cell-col);
		if (str)
		{
			SIT_Widget td = list->td;
			STRPTR mem;
			Cell   cells;
			int    i, sz;
			cell->obj = text;

			td->title = cell->obj;
			layoutMeasureWords(td, &cell->sizeObj);

			/* not the most efficient way of doing this :-/ */
			for (cells = cell-col, sz = 0, i = list->columnCount; i > 0; cells ++, i --)
			{
				if (cells->flags & CELL_ISCONTROL) continue;
				int len = strlen(cells->obj) + 1;
				cells->obj = strcpy(alloca(len), cells->obj);
				sz += len;
			}

			ListRemove(&list->strPool, &str->node);
			str = realloc(str, sizeof *str + sz);
			ListInsert(&list->strPool, &str->node, str->node.ln_Prev);

			for (cells = cell-col, sz = 0, mem = str->mem, i = list->columnCount; i > 0; cells ++, i --)
			{
				if (cells->flags & CELL_ISCONTROL) continue;
				cells->obj = strcpy(mem, cells->obj);
				mem = strchr(mem, 0) + 1;
			}
		}
		else cell->obj = strcpy(SIT_ListAddPool(list, strlen(text) + 1), text);
	}

	return True;
}

DLLIMP STRPTR SIT_ListGetCellText(SIT_Widget w, int col, int row)
{
	SIT_ListBox list = (SIT_ListBox) w;
	if (w == NULL || w->type != SIT_LISTBOX)
		return NULL;

	Cell cell = SIT_ListGetNth(list, row);

	if (cell)
	{
		int max = list->columnCount - 1;
		if (col < 0) col = 0;
		if (col > max) col = max;
		cell += col;
		if ((cell->flags & CELL_ISCONTROL) == 0)
			return cell->obj;
	}
	return NULL;

}

DLLIMP Bool SIT_ListSetColumn(SIT_Widget w, int col, int width, int align, STRPTR label)
{
	SIT_ListBox list = (SIT_ListBox) w;
	if (w == NULL || w->type != SIT_LISTBOX || list->viewMode != SITV_ListViewReport)
		return False;

	int count = list->columnCount;
	if (col < 0) col = 0;
	if (col >= count) col = count-1;

	Cell hdr = list->columns + col;

	switch (align) {
	case 'R': hdr->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); hdr->flags |= CELL_ALIGNR; break;
	case 'C': hdr->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); hdr->flags |= CELL_ALIGNC; break;
	case 'L': hdr->flags &= ~(CELL_ALIGNR | CELL_ALIGNC); break;
	}

	if ((list->lbFlags & SITV_NoHeaders) == 0 && label != DontChangePtr)
	{
		layoutFree(hdr->obj);
		layoutParseHTML(hdr->obj, label);
	}

	if (width != DontChange)
	{
		REAL max = 0, diff;
		Cell cell;
		int  i;
		if (width == AutoArrange)
		{
			if ((list->lbFlags & SITV_NoHeaders) == 0)
				max = hdr->sizeObj.width;

			for (cell = STARTCELL(list) + col, i = 0, count = list->cells.count; i < count; cell += list->columnCount, i ++)
				if (max < cell->sizeObj.width)
					max = cell->sizeObj.width;
		}
		else max = width;

		list->realWidths[col] = - max;
		if (w->layout.pos.width > 0)
		{
			max /= w->layout.pos.width;
			diff = list->realWidths[col] - max;

			/* redistribute widths among other columns */
			count = list->columnCount;
			if (count > 1)
			for (i = 0, diff /= count-1; i != col; i ++)
				list->realWidths[i] += diff;

			SIT_ListResize(w, NULL, NULL);
		}
	}

	sit.dirty = 1;

	return True;
}

/* reorganize column widths to avoid clipping items */
DLLIMP void SIT_ListReorgColumns(SIT_Widget w, STRPTR fmt)
{
	SIT_ListBox list = (SIT_ListBox) w;
	if (w == NULL || w->type != SIT_LISTBOX || list->viewMode != SITV_ListViewReport) return;

	if ((list->lbFlags & SITV_PendingRecalc) || (list->lbFlags & SITV_ListMeasured) == 0)
	{
		/* sizeObj field not set for all cells, can't do it now */
		list->lbFlags |= SITV_ReorgColumns;
		list->columnAlign = fmt;
		return;
	}

	int    col    = list->columnCount, i;
	REAL * widths = alloca(sizeof *widths * col);
	REAL   total  = 0;
	Cell   cell, eof;

	/* get maximal width for each column */
	memset(widths, 0, sizeof *widths * col);
	for (cell = STARTCELL(list), eof = cell + list->cells.count; cell < eof; cell += col)
	{
		for (i = 0; i < col; i ++)
		{
			REAL width = cell[i].sizeObj.width;
			if (widths[i] < width)
				widths[i] = width;
		}
	}
	uint8_t hdr = (list->lbFlags & SITV_NoHeaders) == 0;
	REAL    fixed = 0, pad = list->td->padding[0] + list->td->padding[2];
	DATA8   keep;
	if (! fmt)
		memset(fmt = alloca(col), 0, col);
	for (i = 0, cell = list->columns, keep = fmt; i < col; i ++, keep ++)
	{
		REAL width;
		if (hdr && (width = cell[i].sizeObj.width) > widths[i])
			widths[i] = width;
		widths[i] += pad;
		total += widths[i];
		if (*keep == '*') fixed += widths[i];
	}
	/* redistribute width */
	pad = ((SIT_App)sit.root)->defSBSize;
	widths[col-1] += pad;
	total += pad;
	if (keep[-1] == '*') fixed += pad;
	if (fixed > 0)
	{
		REAL remain = total - fixed;
		if (remain < 0) remain = 0;
		for (i = 0, keep = fmt; i < col; i ++, keep ++)
		{
			if (*keep != '*')
				widths[i] = remain > 0 ? (w->layout.pos.width - fixed) * widths[i] / remain : 0;
			list->realWidths[i] = widths[i] / w->layout.pos.width;
		}
	}
	else for (i = 0, keep = fmt; i < col; i ++, keep ++)
	{
		list->realWidths[i] = widths[i] / total;
	}
	sit.dirty = 1;
}

/* find an item by its rowTag */
DLLIMP int SIT_ListFindByTag(SIT_Widget w, APTR tag)
{
	if (w == NULL || w->type != SIT_LISTBOX) return -1;
	SIT_ListBox list = (SIT_ListBox) w;
	Cell        cell;
	int         row, col, i;

	for (row = 0, cell = STARTCELL(list), col = list->columnCount, i = list->cells.count; i > 0; i -= col, row ++, cell += col)
	{
		if (cell->userData == tag)
			return row;
	}

	return -1;
}

/* get row/column of item being hovered at pos <mouseX>, <mouseY */
DLLIMP int SIT_ListGetItemOver(SIT_Widget w, float rect[4], float mouseX, float mouseY, SIT_Widget * mouseIsRelTo)
{
	if (w == NULL || rect == NULL || w->type != SIT_LISTBOX) return -1;
	SIT_ListBox list = (SIT_ListBox) w;
	SIT_Widget parent = *mouseIsRelTo;

	if (mouseY != 0xbaadf00d && parent != w)
	{
		/* need to be relative to <w> */
		if (parent == NULL)
			parent = sit.root;
		mouseX -= (w->offsetX - w->box.left) - (parent->offsetX + parent->box.left);
		mouseY -= (w->offsetY - w->box.top)  - (parent->offsetY + parent->box.top);
	}

	for (parent = w; (parent->flags & SITF_TopLevel) == 0; parent = parent->parent);
	*mouseIsRelTo = parent;

	Cell cell;
	REAL height = w->layout.pos.height + w->layout.padding.bottom + w->layout.padding.top;
	REAL offX   = w->offsetX + w->layout.pos.left - (parent->offsetX + parent->layout.pos.left - parent->padding[0]);
	REAL offY   = w->offsetY + w->layout.pos.top  - (parent->offsetY + parent->layout.pos.top  - parent->padding[1]);
	int  row, col, i, j;

	if (mouseY == 0xbaadf00d)
	{
		/* cell coordiantes, instead of screen coords */
		row = (int) mouseX;
		col = i = 0;
		j   = row & 0xff;
		row >>= 8;
		if (row < 0 || row >= list->rowCount || j >= list->columnCount)
			return -1;

		height = 1e6;
		cell = STARTCELL(list) + row * list->columnCount + j;
		goto get_rect;
	}

	for (cell = list->rowTop, row = cell - STARTCELL(list), col = list->columnCount, i = list->cells.count - row, row /= col; i > 0; i -= col, row ++)
	{
		if (cell->flags & (CELL_HIDDEN|CELL_CATEGORY)) { cell += col; continue; }
		for (j = 0; j < col; j ++, cell ++)
		{
			get_rect:
			rect[0] = cell->sizeCell.left + w->padding[0];
			rect[1] = cell->sizeCell.top - list->scrollTop + w->padding[1];
			rect[2] = rect[0] + cell->sizeCell.width;
			rect[3] = rect[1] + cell->sizeCell.height;
			if (rect[1] > height)
				return -1;

			if ((rect[0] <= mouseX && mouseX <= rect[2] &&
			     rect[1] <= mouseY && mouseY <= rect[3]) || mouseY == 0xbaadf00d)
			{
				rect[0] += offX;   rect[2] += offX;
				rect[1] += offY;   rect[3] += offY;
				return j | (row << 8);
			}
		}
	}
	return -1;
}
