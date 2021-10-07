/*
 * SIT_Dialog.c : manage top level shell windows
 *
 * Written by T.Pierron, june 2020.
 */

#include <stdio.h>
#include <string.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"

	TagList DialogClass[] = {
		{ SIT_DialogStyles, "styles",       C__, SIT_INT,  OFFSET(SIT_Dialog, customStyles) },
		{ SIT_AccelTable,   "accelTable",  _SG,  SIT_PTR,  OFFSET(SIT_Dialog, accel) },
		{ SIT_TagEnd }
	};


static int SIT_DialogCorner(SIT_Widget w, float x, float y)
{
	#define dialog   ((SIT_Dialog)w)
	int corner = 0;
	REAL min = w->style.font.size * 0.5f;

	x = x + w->layout.pos.left - w->box.left;
	y = y + w->layout.pos.top  - w->box.top;

	/* need to ignore title for normal dialog */
	if (x < min) corner |= 1; else
	if (x > w->box.right - w->box.left - min) corner |= 4;
	if (y < min) corner |= 2; else
	if (y > w->box.bottom - w->box.top - min) corner |= 8;
	if (__builtin_popcount(corner) <= 1)
	{
		/* give corner more room */
		uint8_t corner2 = 0;
		min *= 2;
		if (x < min) corner2 |= 1; else
		if (x > w->box.right - w->box.left - min) corner2 |= 4;
		if (y < min) corner2 |= 2; else
		if (y > w->box.bottom - w->box.top - min) corner2 |= 8;
		if (__builtin_popcount(corner2) == 2)
			return corner2;
	}
	return corner;
	#undef dialog
}

static int SIT_DialogMove(SIT_Widget w, APTR cd, APTR ud)
{
	static REAL rightEdge, bottomEdge;
	SIT_Dialog    dialog = (SIT_Dialog) w;
	SIT_OnMouse * msg = cd;

	#define SITV_DoMove     0x1000
	#define SITV_DoResize   0x2000
	switch (msg->state) {
	case SITOM_Move:
		if (dialog->customStyles & SITV_Resizable)
		{
			/* hilight corner hovered */
			uint8_t corner = SIT_DialogCorner(w, msg->x, msg->y);

			if (dialog->cornerHover != corner)
			{
				/* way too annoying otherwise */
				dialog->cornerHover = corner;
				if (HAS_EVT(sit.root, SITE_OnChange))
				{
					struct SIT_OnChange_t call_data = {.type = SIT_CHANGE_CURSOR, .arg = corner};
					SIT_ApplyCallback(sit.root, &call_data, SITE_OnChange);
				}
			}
		}
		break;
	case SITOM_ButtonPressed:
		dialog->customStyles &= ~ (SITV_DoMove|SITV_DoResize);
		if (dialog->customStyles & SITV_Resizable)
		{
			uint8_t corner = SIT_DialogCorner(w, msg->x, msg->y);
			if (corner)
			{
				dialog->cornerResize = corner;
				dialog->customStyles |= SITV_DoResize;
				dialog->moveOffX = msg->x;
				dialog->moveOffY = msg->y;
				rightEdge  = w->box.right;
				bottomEdge = w->box.bottom;
				return 1;
			}
		}
		if (dialog->customStyles & SITV_Movable)
		{
			if ((dialog->customStyles & SITV_Plain) == 0 && msg->y >= 0)
				/* outside title bar */
				return 0;
			dialog->moveOffX = msg->x;
			dialog->moveOffY = msg->y;
			dialog->customStyles |= SITV_DoMove;
			return 1;
		}
		break;
	case SITOM_CaptureMove:
		if (dialog->customStyles & SITV_DoResize)
		{
			uint8_t corner = dialog->cornerResize;
			SizeF   min    = dialog->minSize;
			REAL    offX   = dialog->moveOffX - msg->x;
			REAL    offY   = dialog->moveOffY - msg->y;
			if (corner & 1)
			{
				w->box.left -= offX;
				if (min.width > 0 && w->box.right - w->box.left < min.width)
					w->box.left = w->box.right - min.width;
			}
			else if (corner & 4)
			{
				w->box.right = rightEdge - offX;
				if (min.width > 0 && w->box.right - w->box.left < min.width)
					w->box.right = w->box.left + min.width;
			}

			if (corner & 2)
			{
				w->box.top -= offY;
				if (min.height > 0 && w->box.bottom - w->box.top < min.height)
					w->box.top = w->box.bottom - min.height;
			}
			else if (corner & 8)
			{
				w->box.bottom = bottomEdge - offY;
				if (min.height > 0 && w->box.bottom - w->box.top < min.height)
					w->box.bottom = w->box.top + min.height;
			}
			sit.dirty = 1;
			SIT_LayoutWidgets(w, KeepDialogSize);
			SIT_MoveWidgets(w);
		}
		else if (dialog->customStyles & SITV_DoMove)
		{
			REAL offX = dialog->moveOffX - msg->x;
			REAL offY = dialog->moveOffY - msg->y;
			w->box.left -= offX; w->box.right  -= offX;
			w->box.top  -= offY; w->box.bottom -= offY;
			w->layout.pos.left -= offX;
			w->layout.pos.top  -= offY;
			sit.dirty = 1;
			SIT_MoveWidgets(w);
		}
	default: break;
	}
	return 0;
}

static int SIT_DialogMouseOut(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Dialog dialog = (SIT_Dialog) w;
	SIT_Widget newIn  = cd;
	if (dialog->cornerHover > 0)
	{
		while (newIn && newIn != w)
			newIn = newIn->parent;
		if (newIn == NULL)
		{
			dialog->cornerHover = 0;
			if (HAS_EVT(sit.root, SITE_OnChange))
			{
				struct SIT_OnChange_t msg = {.type = SIT_CHANGE_CURSOR};
				SIT_ApplyCallback(sit.root, &msg, SITE_OnChange);
			}
		}
	}
	return 1;
}

Bool SIT_InitDialog(SIT_Widget w, va_list args)
{
	static struct SIT_Attach_t defAttach[] = {
		{SITV_AttachPos(50), SITV_AttachPosition, SITV_OffsetCenter},
		{SITV_AttachPos(50), SITV_AttachPosition, SITV_OffsetCenter}
	};
	SIT_Dialog dialog = (SIT_Dialog) w;
	SIT_Widget parent = NULL;

	/* GeomNotified flags is to prevent children from triggering a reflow (layout will be done when dialog is managed) */
	w->flags |= SITF_TopLevel | SITF_GeomNotified;
	w->layout.flags |= LAYF_IgnoreWords;
	memcpy(w->attachment, defAttach, sizeof defAttach);

	SIT_ParseTags(w, args, w->attrs = DialogClass);

	/* check in the hierarchy for parent dialog */
	for (parent = w->parent; parent; parent = parent->parent)
		if (parent->flags & SITF_TopLevel) break;

	if (parent == NULL) return False;
	if (dialog->customStyles & SITV_Plain) w->tagName = "form";

	layoutCalcBox(w);

	SIT_AddCallback(w, SITE_OnClickMove + EVT_PRIORITY(100), SIT_DialogMove, NULL);
	SIT_AddCallback(w, SITE_OnMouseOut  + EVT_PRIORITY(100), SIT_DialogMouseOut, NULL);

	if ((dialog->customStyles & SITV_Plain) == 0)
	{
		SIT_CreateWidgets(w,
			"<label name=#title left=FORM,,NOPAD top=FORM,,NOPAD right=FORM,,NOPAD title=", w->title, ">"
			"<button name=#close right=FORM top=FORM,,NOPAD title='x' nextCtrl=NONE buttonType=", SITV_CancelButton, ">"
			"<canvas name=#client left=FORM right=FORM top=FORM bottom=FORM>"
		);
		SIT_SetAttributes(w, "<title bottom=WIDGET,client><close bottom=WIDGET,client>");
		dialog->clientArea = TAIL(w->children);
	}
	/* not alloced, need to clear ref for SIT_DestroyWidget() */
	w->title = NULL;

	return True;
}

/*
 * the only advantage in using this function instead of SIT_RemoveWidget() is that you
 * can use it on any non top-level child of the dialog to close it.
 */
DLLIMP void SIT_CloseDialog(SIT_Widget w)
{
	if (w == NULL) return;

	while (w && w->type != SIT_DIALOG)
		w = w->parent;

	if (w == sit.activeDlg)
	{
		/* activate previous dialog */
		SIT_Widget parent = w->parent;
		while ((parent->flags & SITF_TopLevel) == 0)
			parent = parent->parent;
		sit.activeDlg = parent;
		parent = sit.focus;
		sit.active = sit.hover = sit.focus = NULL;

		if (parent)
			SIT_ApplyCallback(parent, NULL, SITE_OnBlur);

		if (sit.activeDlg->type == SIT_DIALOG)
			SIT_SetFocus(((SIT_Dialog)sit.activeDlg)->lastFocus);
	}
	if (w->type == SIT_APP)
	{
		int * exit = ((SIT_App)w)->exitCode;
		if (exit) *exit = 1;
	}

	if (HAS_EVT(w, SITE_OnClose) || HAS_EVT(sit.root, SITE_OnClose))
	{
		Bool param = True;
		SIT_ApplyCallback(w, &param, SITE_OnClose);
		if (! param) return;
	}
	SIT_RemoveWidget(w);
}

/* remove dialog from tree, but does not free its resourcess */
DLLIMP void SIT_ExtractDialog(SIT_Widget w)
{
	if (w == NULL) return;

	while (w && w->type != SIT_DIALOG)
		w = w->parent;

	/* parent == NULL means this dialog was already extracted */
	if (w == NULL || w->parent == NULL || w->type == SIT_APP || w->parent->type != SIT_APP) return;

	if (w == sit.activeDlg)
	{
		SIT_Widget parent = w->parent;
		while ((parent->flags & SITF_TopLevel) == 0)
			parent = parent->parent;
		sit.active = sit.hover = sit.focus = NULL;
		sit.activeDlg = parent;
	}
	ListRemove(&w->parent->children, &w->node);
	memset(&w->node, 0, sizeof w->node);
	w->parent = NULL;
	sit.dirty = 1;
}

/* insert it back where it was */
DLLIMP void SIT_InsertDialog(SIT_Widget w)
{
	if (w->parent) return; /* already in tree */
	ListAddTail(&sit.root->children, &w->node);
	sit.dirty = 1;
	w->parent = sit.root;

	REAL box[4];
	memcpy(box, &w->box, sizeof box);
	SIT_CenterDialog(w);
	/* note: w->box uses integer coordinates */
	if (memcmp(box, &w->box, sizeof box))
		SIT_MoveWidgets(w);
}
