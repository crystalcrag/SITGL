/*
 * SIT_Widget.c : base class for widget creation. Dispatch tags to custom class.
 *
 * written by T.Pierron, may 2020.
 */

#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "nanovg.h"
#include "SIT_P.h"
#include "SIT_CSSLayout.h"


	/* common properties for all widgets */
	struct TagList_t WidgetClass[] = {
		{ "title",      SIT_Title,       CSG, SIT_STR,  OFFSET(SIT_Widget, title) },
		{ "style",      SIT_Style,       CSG, SIT_PTR,  0},
		{ "name",       SIT_Name,        __G, SIT_STR,  OFFSET(SIT_Widget, name) },
		{ "classes",    SIT_Classes,     CSG, SIT_STR,  OFFSET(SIT_Widget, classes) },
		{ "userData",   SIT_UserData,    _SG, SIT_PTR,  OFFSET(SIT_Widget, userData) },
		{ "ptrEvents",  SIT_PtrEvents,   _SG, SIT_BOOL, OFFSET(SIT_Widget, ptrEvents) },
		{ "enabled",    SIT_Enabled,     _SG, SIT_BOOL, OFFSET(SIT_Widget, enabled) },
		{ "visible",    SIT_Visible,     CSG, SIT_BOOL, OFFSET(SIT_Widget, visible) },
		{ "nextCtrl",   SIT_NextCtrl,    _SG, SIT_STR,  0},
		{ "tabNum",     SIT_TabNum,      _SG, SIT_INT,  OFFSET(SIT_Widget, tabOrder) },
		{ "y",          SIT_Y,           CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.top) },
		{ "x",          SIT_X,           CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.left) },
		{ "rect",       SIT_Rect,        CSG, SIT_ABBR, ABBR(1, 1, 1, 1) },
		{ NULL,         SIT_Padding,     __G, SIT_INT,  0},
		{ "absX",       SIT_AbsX,        CSG, SIT_UNIT, 0},
		{ "absY",       SIT_AbsY,        CSG, SIT_UNIT, 0},
		{ NULL,         SIT_Parent,      __G, SIT_PTR,  OFFSET(SIT_Widget, parent) },
		{ NULL,         SIT_NextSibling, __G, SIT_PTR,  OFFSET(SIT_Widget, node.ln_Next) },
		{ NULL,         SIT_PrevSibling, __G, SIT_PTR,  OFFSET(SIT_Widget, node.ln_Prev) },
		{ NULL,         SIT_FirstChild,  __G, SIT_PTR,  OFFSET(SIT_Widget, children.lh_Head) },
		{ NULL,         SIT_LastChild,   __G, SIT_PTR,  OFFSET(SIT_Widget, children.lh_Tail) },
		{ NULL,         SIT_CtrlType,    __G, SIT_INT,  OFFSET(SIT_Widget, type) },
		{ NULL,         SIT_TagPrivate1, C__, SIT_PTR,  0}, /* BuddyCtrl */
		{ NULL,         SIT_TagPrivate2, C__, SIT_PTR,  OFFSET(SIT_Widget, buddyText) },
		{ "buddyLabel", SIT_BuddyLabel,  C__, SIT_ABBR, ABBR(1, 1, 0, 0)},
		{ "composited", SIT_Composited,  _SG, SIT_BOOL, OFFSET(SIT_Widget, composited) },
		{ NULL,         SIT_NVGcontext,  __G, SIT_PTR,  0},
		{ "toolTip",    SIT_ToolTip,     C__, SIT_STR,  0},
		{ NULL,         SIT_ClientRect,  __G, SIT_PTR,  0},
		{ "height",     SIT_Height,      CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.height) },
		{ "width",      SIT_Width,       CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.width) },

		/* attachments */
		#define	ATTACH(side, field)    OFFSET(SIT_Widget, attachment[side].field)
		{ "leftOffset",       SIT_LeftOffset,       CSG, SIT_UNIT, ATTACH(0, sa_Offset) },
		{ "topOffset",        SIT_TopOffset,        CSG, SIT_UNIT, ATTACH(1, sa_Offset) },
		{ "rightOffset",      SIT_RightOffset,      CSG, SIT_UNIT, ATTACH(2, sa_Offset) },
		{ "bottomOffset",     SIT_BottomOffset,     CSG, SIT_UNIT, ATTACH(3, sa_Offset) },
		{ "leftObject",       SIT_LeftObject,       CSG, SIT_CTRL, ATTACH(0, sa_Arg) },
		{ "topObject",        SIT_TopObject,        CSG, SIT_CTRL, ATTACH(1, sa_Arg) },
		{ "rightObject",      SIT_RightObject,      CSG, SIT_CTRL, ATTACH(2, sa_Arg) },
		{ "bottomObject",     SIT_BottomObject,     CSG, SIT_CTRL, ATTACH(3, sa_Arg) },
		{ "leftAttachment",   SIT_LeftAttachment,   CSG, SIT_INT,  ATTACH(0, sa_Type) },
		{ "topAttachment",    SIT_TopAttachment,    CSG, SIT_INT,  ATTACH(1, sa_Type) },
		{ "rightAttachment",  SIT_RightAttachment,  CSG, SIT_INT,  ATTACH(2, sa_Type) },
		{ "bottomAttachment", SIT_BottomAttachment, CSG, SIT_INT,  ATTACH(3, sa_Type) },
		{ "left",             SIT_Left,             CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ "top",              SIT_Top,              CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ "right",            SIT_Right,            CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ "bottom",           SIT_Bottom,           CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ "maxWidth",         SIT_MaxWidth,         CSG, SIT_CTRL, 0 },
		{ "minWidth",         SIT_MinWidth,         CSG, SIT_UNIT, OFFSET(SIT_Widget, minBox.width) },
		{ "minHeight",        SIT_MinHeight,        CSG, SIT_UNIT, OFFSET(SIT_Widget, minBox.height) },
		{ "maxBoxWidth",      SIT_MaxBoxWidth,      CSG, SIT_UNIT, OFFSET(SIT_Widget, maxBox.width) },
		{ "maxBoxHeight",     SIT_MaxBoxHeight,     CSG, SIT_UNIT, OFFSET(SIT_Widget, maxBox.height) },
		#undef ATTACH

		{ SIT_TagEnd }
	};

	#define S(x)      sizeof (struct SIT_Callback_t) * x
	static uint16_t sizeof_widgets[] = {    /* callback slots preallocated */
		sizeof (struct SIT_App_t),          S(1),
		sizeof (struct SIT_Dialog_t),       S(3),
		sizeof (struct SIT_Label_t),        S(1),
		sizeof (struct SIT_Button_t),       S(1),
		sizeof (struct SIT_EditBox_t),      S(5),
		sizeof (struct SIT_Frame_t),        S(1),
		sizeof (struct SIT_ListBox_t),      S(6),
		sizeof (struct SIT_Widget_t),       S(8), /* SIT_CANVAS */
		sizeof (struct SIT_ScrollBar_t),    S(4),
		sizeof (struct SIT_Slider_t),       S(4),
		sizeof (struct SIT_Progress_t),     S(1),
		sizeof (struct SIT_ComboBox_t),     S(4),
		sizeof (struct SIT_Tab_t),          S(2),
		sizeof (struct SIT_Tooltip_t),      S(1),
		sizeof (struct SIT_FileDialog_t),   0,
		sizeof (struct SIT_FolderSel_t),    0,
		sizeof (struct SIT_Widget_t),       0     /* SIT_HTMLTAG */
	};
	#undef S

	static STRPTR widgetNames[] = {         /* generic name for CSS (need to be different for each) */
		"html", "dialog", "label", "button", "editbox", "fieldset", "listbox", "canvas", "vscroll",
		"slider", "progress", "combobox", "tab", "tooltip", "", "", NULL
	};

	#if 0
	static uint8_t geomPolicy[] = {
		FitUsingCurrentBox,  // SITV_Auto
		KeepDialogSize,      // SITV_Fixed
		FitUsingOptimalBox   // SITV_Optimal
	};
	#endif

	static uint8_t focusStop[] = {
		0, // SIT_APP
		0, // SIT_DIALOG
		0, // SIT_LABEL: check children?
		1, // SIT_BUTTON
		1, // SIT_EDITBOX
		0, // SIT_FRAME
		1, // SIT_LISTBOX
		0, // SIT_CANVAS
		0, // SIT_SCROLLBAR
		1, // SIT_SLIDER
		0, // SIT_PROGRESS
		1, // SIT_COMBOBOX
		0, // SIT_TAB
		0, // SIT_TOOLTIP
		0, // SIT_FILESELECT
		0, // SIT_DIRSELECT
		0, // SIT_HTMLTAG
	};


/* returns 0 if value is same, >0 otherwise */
int SIT_SetWidgetValue(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Variant val = ud;
	TagList     tag = cd;

	if ((w->visible || tag->tl_TagID == SIT_Visible || w->type == SIT_HTMLTAG) && tag->tl_Flags == CSG &&
	    ! (tag->tl_TagID == SIT_Title && w->type == SIT_EDITBOX))
	{
		/* setting this property will require a geometric reflow */
		APTR p = (STRPTR)w + tag->tl_Arg;
		/* check first if value differs */
		switch (tag->tl_Type) {
		default:       if (* (int  *)   p == val->integer) return 0; break; /* SIT_INT */
		case SIT_BOOL: if (* (DATA8)    p == val->boolean) return 0; break;
		case SIT_UNIT: if (* (float *)  p == (float) val->real) return 0; break;
		case SIT_REAL: if (* (double *) p == val->real)    return 0;
		/* no need to check SIT_CTRL, SIT_PTR, SIT_STR */
		}
		int flag = SITF_GeometryChanged;
		if (w->flags & SITF_TopLevel)
		{
			/* optimize in case only position of dialog is changed: don't recalc whole geom */
			switch (tag->tl_TagID) {
			case SIT_X: flag = SITF_Style1Changed; break;
			case SIT_Y: flag = SITF_Style2Changed; break;
			}
		}
		w->flags |= flag;
	}

	if (tag->tl_Arg > 0)
	{
		STRPTR * str;
		#define	SET_VALUE(widget, type, val) \
			* (type *) ((char *)widget + tag->tl_Arg) = val

		if (tag->tl_TagID == SIT_Enabled)
			w->oldEna = w->enabled;
		#ifdef DEBUG_SIT
		if (tag->tl_Flags == C__)
		{
			if (w->flags & SITF_InitDone)
				fprintf(stderr, "warning: %s: setting create-only property '%s'\n", w->name, tag->tl_TagName);
		}
		else if ((tag->tl_Flags & _S_) == 0)
		{
			fprintf(stderr, "warning: %s: setting read-only property '%s'\n", w->name, tag->tl_TagName);
		}
		#endif
		switch (tag->tl_Type) {
		case SIT_ABBR: break;
		case SIT_CTRL:
		case SIT_PTR:  SET_VALUE(w, void *,   val->pointer); break;
		case SIT_INT:  SET_VALUE(w, int,      val->integer); break;
		case SIT_REAL: SET_VALUE(w, double,   val->real);    break;
		case SIT_BOOL: SET_VALUE(w, uint8_t,  !!val->boolean); /* we need 0 or 1 */ break;
		case SIT_UNIT: SET_VALUE(w, REAL,     val->real);    break;
		case SIT_STR:
			str = (STRPTR *) ((STRPTR)w + tag->tl_Arg);
			/* SIT_Classes is more often than not a static string */
			if (*str && (tag->tl_TagID != SIT_Classes || (w->flags & SITF_CustomClasses)))
				if (*str) free(*str);
			*str = val->string;
		}
	}

	/* special processing for a few tags */
	switch (tag->tl_TagID) {
	case SIT_Title:
		if (w->flags & SITF_InitDone)
		{
			w->layout.wordwrap.count = 0;
			layoutParseHTML(w, w->title);
		}
		break;
	case SIT_Composited:
		w->composited = val->boolean;
		break;
	case SIT_Visible:
		if (w->type == SIT_TOOLTIP)
		{
			if (val->boolean == 0 && sit.curTooltip == w)
			{
				if (sit.toolTip)
					SIT_ActionReschedule(sit.toolTip, -1, -1);
				sit.curTooltip = NULL;
				sit.toolTip = NULL;
			}
			else if (val->boolean)
			{
				SIT_SetValues(w, SIT_DisplayTime, SITV_ResetTime, NULL);
			}
		}
		break;
	case SIT_Enabled:
		if (w->oldEna != w->enabled)
		{
			w->oldState = w->oldEna ? 0 : STATE_DISABLED;
			w->flags |= SITF_RecalcStyles;
		}
		break;
	case SIT_Classes:
		w->flags |= SITF_RecalcStyles | SITF_CustomClasses;
		memset(w->layout.crc32, 0xff, sizeof w->layout.crc32);
		break;
	case SIT_Style:
		w->flags |= SITF_RecalcStyles;
		memset(w->layout.crc32, 0xff, sizeof w->layout.crc32);
		/* note: val->string is a direct pointer from user, might be a static string at this point */
		cssParseInlineStyles(w, val->string);
		break;
	case SIT_X:
		if (w->flags & SITF_Style1Changed)
		{
			REAL sz = w->box.right - w->box.left;
			w->box.left = w->fixed.left;
			w->box.right = w->box.left + sz;
			w->flags &= ~SITF_Style1Changed;
		}
		w->flags |= SITF_FixedX;
		break;
	case SIT_Y:
		if (w->flags & SITF_Style2Changed)
		{
			REAL h = w->box.bottom - w->box.top;
			w->box.top = w->fixed.top;
			w->box.bottom = w->box.top + h;
			w->flags &= ~SITF_Style2Changed;
		}
		w->flags |= SITF_FixedY;
		break;
	case SIT_Width:
		if (w->fixed.width < 0) w->flags &= ~SITF_FixedWidth;
		else w->flags |= SITF_FixedWidth;
		break;
	case SIT_Height:
		if (w->fixed.height < 0) w->flags &= ~SITF_FixedHeight;
		else w->flags |= SITF_FixedHeight;
		break;
	case SIT_TagPrivate1:
		if (w->buddyText)
			SIT_CreateBuddyLabel(w, w->buddyText, ((SIT_Variant)ud)->pointer);
		break;
	case SIT_MaxWidth:
		if (val->pointer)
		{
			/* add to linked list */
			SIT_Widget prev = val->pointer;
			SIT_Widget next = (APTR) prev->max.ln_Next;
			prev->max.ln_Next = (APTR) w;
			w->max.ln_Prev = (APTR) prev;
			w->max.ln_Next = (APTR) next;
			if (next) next->max.ln_Prev = (APTR) w;
		}
		else /* remove from linked list */
		{
			SIT_Widget prev = (APTR) w->max.ln_Prev;
			SIT_Widget next = (APTR) w->max.ln_Next;

			if (prev) prev->max.ln_Next = (APTR) next;
			if (next) next->max.ln_Prev = (APTR) prev;
		}
		break;
	case SIT_AccelTable:
		sit.curAccel = NULL;
		if (((SIT_App)w)->accel) w->flags |= SITF_HasAccel;
		else w->flags &= ~SITF_HasAccel;
	}
	return 1;
}

/* allow user to specify class, id and tag name in widget's name */
static void SIT_SplitName(SIT_Widget w)
{
	STRPTR p;
	STRPTR id = w->name, cls = NULL, tag = NULL, state = NULL;
	for (p = id; *p; p ++)
	{
		if (*p == '.') cls = p + 1, p[0] = 0; else
		if (*p == '#') tag = p + 1, p[0] = 0; else
		if (*p == ':') state = p + 1, p[0] = 0;
	}
	if (id[0] == 0) id = tag ? tag : cls;

	w->name = id;
	if (tag) w->tagName = tag;
	w->classes = cls;
	if (state && strcasecmp(state, "active") == 0)
		w->state = STATE_ACTIVATED;
}

/* canvas: too simple to have its dedicated file */
int SIT_MeasureCanvas(SIT_Widget w, APTR cd, APTR userData)
{
	SizeF * ret = cd;
	SizeF   sz;

	if (w->children.lh_Head)
		SIT_LayoutWidgets(w, (ULONG) userData);

	sz.width  = w->box.right  - w->box.left;
	sz.height = w->box.bottom - w->box.top;
	if (ret->width  < sz.width)  ret->width  = sz.width;
	if (ret->height < sz.height) ret->height = sz.height;

	if (w->title)
	{
		layoutMeasureWords(w, &sz);
		sz.width  += w->padding[0] + w->padding[2];
		sz.height += w->padding[1] + w->padding[3];
		if (ret->width  < sz.width)  ret->width  = sz.width;
		if (ret->height < sz.height) ret->height = sz.height;
	}
	w->childBox = *ret;
	return 1;
}

Bool SIT_InitCanvas(SIT_Widget w, va_list args)
{
	w->flags |= SITF_RenderChildren | SITF_Container;
	w->optimalWidth = SIT_MeasureCanvas;

	SIT_ParseTags(w, args, w->attrs = WidgetClass);

	return True;
}

/* SIT_FRAME: too simple to have its dedicated file */
static int SIT_FrameMeasure(SIT_Widget w, APTR cd, APTR mode)
{
	SIT_Frame frame = (SIT_Frame) w;
	REAL      fh    = w->style.font.size;
	SizeF *   pref  = cd;
	SizeF     min   = *pref;
	float     size;

	layoutMeasureWords(w, &min);
	frame->title.width  = roundf(min.width);
	frame->title.height = roundf(min.height);

	int i;
	REAL minPadding[] = {roundf(fh * 0.9f), roundf(fh * 1.2f), roundf(fh * 0.9f), roundf(fh * 0.8f)};
	if (w->layout.border.bottom == 0)
		minPadding[3] = 0;
	for (i = 0; i < 4; i ++)
	{
		REAL * pad = w->padding + i;
		if (*pad < minPadding[i])
		{
			(&min.width)[i&1] += - *pad;
			*pad = minPadding[i];
		}
	}

	/* layout children */
	if (w->children.lh_Head && SIT_LayoutWidgets(w, (ULONG) mode))
	{
		size = w->box.right  - w->box.left;
		if (min.width < size) min.width = size;
		size = w->box.bottom - w->box.top;
		if (min.height < size) min.height = size;
	}

	frame->title.width += size = roundf((w->padding[0] + w->padding[1]) * 0.25f);

	min.width  += w->padding[0] + w->padding[2] + size;
//	min.height += w->padding[1] + w->padding[3];

	if (pref->width  < min.width)  pref->width  = min.width;
	if (pref->height < min.height) pref->height = min.height;
	w->childBox = *pref;

	return 0;
}

static int SIT_FrameNoBorders(SIT_Widget w, APTR cd, APTR ud)
{
	/* cancel border rendering: need special processing */
	SIT_Frame frame = (SIT_Frame) w;
	REAL      max, val;
	if (w->title)
		w->layout.flags |= LAYF_NoBorders;

	/* padLeft: max of: border-radius, padding, 1em */
	max = MAX(w->layout.majorRadius.top, w->layout.majorRadius.bottom);
	if (max < (val = w->padding[0])) max = val;
/*	val = w->style.font.size;
	if (max < val) max = val;
*/	frame->padLeft = max;

	return 1;
}

/* create and initialize a group box */
static Bool SIT_InitFrame(SIT_Widget w, va_list args)
{
	int SIT_FrameRender(SIT_Widget w, APTR cd, APTR ud);

	w->flags |= SITF_RenderChildren;
	w->optimalWidth = SIT_FrameMeasure;
	w->layout.flags |= LAYF_IgnoreWords;
	w->render = SIT_FrameRender;

	SIT_AddCallback(w, SITE_OnResize, SIT_FrameNoBorders, NULL);
	SIT_ParseTags(w, args, w->attrs = WidgetClass);
	layoutCalcBox(w);

	if (w->title)
		layoutParseHTML(w, w->title);
	return TRUE;
}


static void SIT_AdjustTabOrder(SIT_Widget w, STRPTR ctrl)
{
	SIT_Widget * prev;
	SIT_Widget   parent, list, next;

	for (parent = w->parent; (parent->flags & SITF_TopLevel) == 0; parent = parent->parent)
		if (parent->flags & SITF_PrivateChildren) return;

	w->flags &= ~SITF_InFocusRing;
	if (w->flags & SITF_InitDone)
	{
		/* remove from focus ring */
		for (prev = &FOCUSRING(parent), list = *prev; list != w; prev = &list->nextCtrl, list = *prev);

		if (list)
			*prev = list->nextCtrl, w->flags &= ~SITF_InFocusRing;
	}
	if (ctrl == NULL)
		w->nextCtrl = NoFocusRing;
	else switch (FindInList("NONE,FIRST,LAST", ctrl, 0)) {
	case 0: /* none */
		w->nextCtrl = NoFocusRing;
		break;
	case 1: /* first */
		w->flags |= SITF_InFocusRing;
		w->nextCtrl = FOCUSRING(parent);
		FOCUSRING(parent) = w;
		break;
	default:
		next = SIT_GetById(parent, ctrl);
		if (next && focusStop[next->type] == 0)
		{
			/* scan children for first focus ring */
			for (list = HEAD(next->children); list && (list->flags & SITF_InFocusRing) == 0; NEXT(list));
			if (list) next = list;
		}
		if (next)
		{
			for (prev = &FOCUSRING(parent), list = *prev; list && list != next;
			     prev = &list->nextCtrl, list = *prev);
			if (list)
			{
				w->flags |= SITF_InFocusRing;
				w->nextCtrl = *prev;
				*prev = w;
				break;
			}
		}
		// no break;
	case 2: /* last */
		for (prev = &FOCUSRING(parent), list = *prev; list; prev = &list->nextCtrl, list = *prev);
		w->flags |= SITF_InFocusRing;
		w->nextCtrl = NULL;
		*prev = w;
	}
}

DLLIMP SIT_Widget SIT_CreateWidget(STRPTR name, SIT_TYPE type, SIT_Widget parent, ...)
{
	if (! IsDef(name)) return NULL; /* mandatory */

	/* extra memory associated to widget, that will be free()'ed automatically */
	int extra = type >> 16;
	type &= 0xffff;

	if (type == SIT_APP) extra = 128; else /* 128 = monitor resol buffer, need to be a pointer because of SIT_GetValues() */
	if (parent == NULL) return NULL;

	int        len = strlen(name)+1;
	int        sz  = sizeof_widgets[type<<1];
	int        cbs = sizeof_widgets[(type<<1)+1];
	SIT_Widget w   = calloc(sz + cbs + len + extra, 1); /* prealloc as much as possible */
	va_list    args;

	vector_init(w->layout.wordwrap, sizeof (struct WordWrap_t));

	if (parent && parent->type == SIT_DIALOG && ((SIT_Dialog)parent)->clientArea)
		parent = ((SIT_Dialog)parent)->clientArea;

	if (type == SIT_DIALOG)
	{
		parent = sit.root;
	}
	else if (type == SIT_FILESELECT || type == SIT_DIRSELECT)
	{
		while ((parent->flags & SITF_TopLevel) == 0)
			parent = parent->parent;
	}

	w->name      = (STRPTR) w + sz + cbs + extra;   /* id for CSS */
	w->tagName   = widgetNames[type];               /* tag name for CSS */
	w->type      = type;
	w->parent    = parent;
	w->attrs     = WidgetClass;
	w->setValue  = SIT_SetWidgetValue;
	w->enabled   = True;
	w->ptrEvents = True;
	w->visible   = type != SIT_DIALOG;
	w->cbSlot    = cbs;
	w->slots     = (APTR) w + sz;

	memset(w->layout.crc32, 0xff, sizeof w->layout.crc32);
	strcpy(w->name, name);

	SIT_SplitName(w);

	w->minBox.width = w->minBox.height = -1;
	w->optimalBox   = w->maxBox = w->minBox;
	if (extra > 0) w->userData = (STRPTR) w->slots + cbs;

	va_start(args, parent);

	Bool ok;
	switch (type) {
	case SIT_APP:        ok = SIT_InitApp(w, args);        break;
	case SIT_DIALOG:     ok = SIT_InitDialog(w, args);     break;
	case SIT_LABEL:      ok = SIT_InitLabel(w, args);      break;
	case SIT_BUTTON:     ok = SIT_InitButton(w, args);     break;
	case SIT_EDITBOX:    ok = SIT_InitEditBox(w, args);    break;
	case SIT_FRAME:      ok = SIT_InitFrame(w, args);      break;
	case SIT_LISTBOX:    ok = SIT_InitListBox(w, args);    break;
	case SIT_CANVAS:     ok = SIT_InitCanvas(w, args);     break;
	case SIT_SCROLLBAR:  ok = SIT_InitScrollBar(w, args);  break;
	case SIT_SLIDER:     ok = SIT_InitSlider(w, args);     break;
	case SIT_PROGRESS:   ok = SIT_InitProgress(w, args);   break;
	case SIT_COMBOBOX:   ok = SIT_InitComboBox(w, args);   break;
	case SIT_TAB:        ok = SIT_InitTab(w, args);        break;
	case SIT_TOOLTIP:    ok = SIT_InitTooltip(w, args);    break;
	case SIT_FILESELECT: ok = SIT_InitFileDialog(w, args); break;
	case SIT_DIRSELECT:  ok = SIT_InitDirDialog(w, args);  break;
	case SIT_HTMLTAG:    if (! w->tagName) w->tagName = name; SIT_ParseTags(w, args, WidgetClass);
	default:             ok = True;
	}
	va_end(args);

	if (! ok)
	{
		free(w);
		return NULL;
	}

	parent = w->parent;
	if (parent)
	{
		if (type == SIT_SCROLLBAR && (sz = FindInList("vscroll,hscroll", w->tagName, 0)) >= 0)
			(&parent->vscroll)[sz] = w;

		ListAddTail(&parent->children, &w->node);
		if (parent == sit.root)
			parent->flags |= SITF_GeometryChanged;
		else
			SIT_InitiateReflow(w);
	}
	else
	{
		w->maxBox.width  = sit.scrWidth;
		w->maxBox.height = sit.scrHeight;
	}

	if ((w->style.flags & CSSF_APPLIED) == 0)
		layoutCalcBox(w);

	if (focusStop[type] && w->nextCtrl == NULL && (w->flags & SITF_InFocusRing) == 0)
		SIT_AdjustTabOrder(w, "LAST");

	w->flags |= SITF_InitDone;
	w->flags &= ~SITF_RecalcStyles;

	return w;
}

void SIT_AddTitle(SIT_Widget w, STRPTR text, int pos)
{
	STRPTR cur = w->title;
	int    len = cur ? strlen(cur) : 0;
	int    add = strlen(text);

	if (pos < 0) pos += len+1;
	cur = realloc(cur, len + add + 1);
	if (cur == NULL) return;
	if (pos < len)
	{
		memmove(cur + pos + add, cur + pos, len - pos + 1);
		memcpy(cur + pos, text, add);
	}
	else strcpy(cur + len, text);
	w->title = cur;
}

DLLIMP float SIT_EmToReal(SIT_Widget w, uint32_t val)
{
	REAL size = w->parent ? w->parent->style.font.size : sit.defFontHeight;

	if (val & (1<<31))
	{
		val &= 0x7fffffff;
		if (val & (1<<30)) val |= (1<<31); /* sign ext */
		return roundf(size * ((int) val * (1/8192.f)));
	}
	else
	{
		if (val & (1<<30)) val |= (1<<31); /* sign ext */
		return (int) val;
	}
}

STRPTR RawDoFmt(STRPTR format, va_list *);

void SIT_ParseTags(SIT_Widget w, va_list vargs, TagList classArgs)
{
	KeyVal list = NULL;
	int    tag;

	next: for (;;)
	{
		/* Look in classArgs for definition of that tag */
		TagList stack[5];
		TagList args;
		ULONG   usage = 0;
		int     format;

		if (list) tag = list->tag;
		else      tag = va_arg(vargs, int);
		format = tag & XfMt ? True : False;
		tag &= ~ XfMt;

		if (tag == SIT_TagEnd) break;
		if (tag == SIT_TagList) { list = list ? (KeyVal)list->key.ptr : va_arg(vargs, KeyVal); continue; }
		if (tag >= SIT_TagUser)
		{
			SIT_OnVal cd = {.stage = SITV_Set, .tag = tag};
			if (list) cd.ptr = (APTR) &list->key, list ++;
			else      cd.vararg = &vargs;
			tag = SIT_ApplyCallback(w, &cd, SITE_OnSetOrGet);
			if (tag & 1) w->flags |= SITF_GeometryChanged;
//			if (tag & 2) w->flags |= SITF_PostProcess;
			continue;
		}

		/* optimization: since enum and WidgetClass are sorted: we can perform a O(1) lookup on WidgetClass */
		if (tag < SIT_EndCommonTags /* end of widget class tags */)
		{
			args = WidgetClass + tag - 2;
			goto found;
		}

		for (args = classArgs; args->tl_TagID != SIT_TagEnd; )
		{
			union SIT_Variant_t value = {0};

			/* super class */
			if (args->tl_TagID == SIT_SuperClass)
			{
				stack[usage ++] = args + 1;
				args = (TagList) args->tl_Arg;
				continue;
			}
			if (args->tl_TagID != tag)
			{
				args ++;
				if (args->tl_TagID == 0 && usage > 0)
					args = stack[-- usage];
				continue;
			}
			found:
			if (args->tl_Type == SIT_ABBR)
				tag = args->tl_Arg, args -= (tag & 0xff) - ABBRBASE, tag >>= 8;
			else
				tag = 0;

			for (;;)
			{
				if (list)
				{
					switch (args->tl_Type) {
					case SIT_ABBR: break;
					case SIT_REAL: value.real = list->key.real; break;
					case SIT_CTRL: case SIT_PTR: value.pointer = list->key.ptr; break;
					case SIT_BOOL: case SIT_INT: value.integer = list->key.val; break;
					case SIT_STR:  value.string = list->key.ptr; list ++; goto assign_str;
					case SIT_UNIT: value.integer = list->key.val; list ++; goto assign_unit;
					}
					list ++;
					if (list->tag == SIT_TagList)
						list = list->key.ptr;
				}
				else switch (args->tl_Type) {
				case SIT_ABBR: break;
				case SIT_CTRL:
				case SIT_PTR:  value.pointer = va_arg(vargs, void *); break;
				case SIT_BOOL:
				case SIT_INT:  value.integer = va_arg(vargs, int);    break;
				case SIT_REAL: value.real    = va_arg(vargs, double); break;
				case SIT_UNIT:
					value.integer = va_arg(vargs, int);
					assign_unit:
					format = args->tl_TagID - SIT_Height;
					if (0 <= format && format < 6)
					{
						if (value.integer & (1<<31))
						{
							w->layout.flags |= LAYF_RelUnit << format;
							(&w->style.height)[format] = value.integer;
						}
						else w->layout.flags &= ~(LAYF_RelUnit << format);
					}
					value.real = SIT_EmToReal(w, value.integer);
					break;
				case SIT_STR: /* strings have to be malloced ... */
					value.string = va_arg(vargs, STRPTR);
					assign_str:
					/* ... but there are a few exceptions */
					switch (args->tl_TagID) {
					case SIT_Title:
						switch (w->type) {
						case SIT_EDITBOX:
							/* title is entirely managed by the control, no need to alloc anything here */
							SIT_TextEditSetText(w, value.string);
							goto next;
						case SIT_DIALOG:
							/* will be either transfered to a label or discarded, no need to double malloc stuff */
							w->title = value.string;
							goto next;
						default: break;
						}
						break;
					case SIT_ToolTip:
						/* will be manage by SIT_ToolTip widget, no need to allocate mem here */
						SIT_TooltipAuto(w, value.string);
						goto next;
					case SIT_NextCtrl:
						/* string is only needed for this function: no need to alloc anything */
						SIT_AdjustTabOrder(w, value.string);
						goto next;
					}
					if (IsDef(value.string))
						value.string = format ? RawDoFmt(value.string, &vargs) : strdup(value.string);
					else
						value.string = NULL;
				}
				w->setValue(w, args, &value);
				if (tag == 0) break;
				args -= (tag & 0xff) - ABBRBASE; tag >>= 8;
			}
			goto next;
		}
		/* not good: tag not in definition list */
		va_arg(vargs, int);
	}
}

DLLIMP Bool SIT_AddCallback(SIT_Widget w, int type, SIT_CallProc proc, APTR data)
{
	SIT_Callback cb;
	uint8_t priority = type >> 8;
	type &= 0xff;

	if (w == NULL) return False;

	if (type == SITE_OnClickMove)
	{
		/* register 2 events at once */
		return SIT_AddCallback(w, SITE_OnClick,     proc, data) &&
		       SIT_AddCallback(w, SITE_OnMouseMove, proc, data);
	}

	/* check that callback didn't exist yet */
	for (cb = HEAD(w->callbacks); cb && ! (cb->sc_CB == proc &&
	     cb->sc_UserData == data && cb->sc_Event == type); NEXT(cb));

	if (cb == NULL && proc)
	{
		uint8_t alloced = 0;
		if (w->cbSlot > 0)
		{
			for (cb = w->slots; cb->sc_CB; cb ++);
			w->cbSlot -= sizeof *cb;
		}
		else cb = malloc(sizeof *cb), alloced = 1;

		if (cb)
		{
			cb->sc_CB       = proc;
			cb->sc_UserData = data;
			cb->sc_Event    = type;
			cb->sc_Malloc   = alloced;
			cb->sc_Priority = priority;
			w->evtFlags    |= SET_EVT(type);

			/* keep them ordered in descreasing priority */
			SIT_Callback ins;
			for (ins = HEAD(w->callbacks); ins && ins->sc_Priority > priority; NEXT(ins));
			if (ins) ListInsert(&w->callbacks, &cb->sc_Node, ins->sc_Node.ln_Prev);
			else ListAddTail(&w->callbacks, &cb->sc_Node);

			if (type == SITE_OnDropFiles)
			{
				/* need some house keeping */
				SIT_AppAllowDnD();
			}

			return True;
		}
	}
	return False;
}

static void SIT_DelCB(SIT_Widget w, SIT_Callback cb)
{
	ListRemove(&w->callbacks, &cb->sc_Node);
	w->evtFlags &= ~(1 << cb->sc_Event);
	if (cb->sc_Malloc == 0)
		w->cbSlot += sizeof *cb, cb->sc_CB = NULL;
	else
		free(cb);
}

DLLIMP void SIT_DelCallback(SIT_Widget w, int type, SIT_CallProc proc, APTR data)
{
	SIT_Callback cb, next;

	if (w == NULL) return;

	if (proc)
	{
		for (cb = HEAD(w->callbacks); cb && !(cb->sc_CB == proc &&
			 cb->sc_UserData == data && cb->sc_Event == type); NEXT(cb));

		if (cb) SIT_DelCB(w, cb);
	}
	else for (cb = next = HEAD(w->callbacks); cb; cb = next)
	{
		/* delete all cb register for this event type */
		NEXT(next);
		if (cb->sc_Event == type) SIT_DelCB(w, cb);
	}
	if (type == SITE_OnDropFiles) SIT_AppDelDnD();
}

/* remove all callbacks (reusable canvas) */
DLLIMP void SIT_ClearCallbacks(SIT_Widget w)
{
	if (w == NULL) return;
	while (HEAD(w->callbacks))
		SIT_DelCB(w, HEAD(w->callbacks));
}

void SIT_FreeCSS(SIT_Widget node)
{
	Background bg;
	int count;
	for (bg = node->style.background, count = node->style.bgCount; count > 0; count --, bg ++)
		if (bg->image) SIT_UnloadImg(bg->image);

	/* note: background-image and image.src are in the image cache, do not free them here */
	if (node->style.background)  free(node->style.background);
	if (node->style.shadow)      free(node->style.shadow);
	if (node->style.boxShadow)   free(node->style.boxShadow);
	if (node->style.borderImg)   free(node->style.borderImg);
	if (node->layout.wordwrap.buffer)
		free(node->layout.wordwrap.buffer);
}

static void SIT_RemoveFromFocus(SIT_Widget w)
{
	SIT_Widget * prev;
	SIT_Widget   list;
	for (list = w->parent; (list->flags & SITF_TopLevel) == 0; list = list->parent);
	for (prev = &FOCUSRING(list), list = *prev; list && list != w; prev = &list->nextCtrl, list = *prev);
	if (list) *prev = list->nextCtrl;
}

/* free memory and resources allocated for widget */
void SIT_DestroyWidget(SIT_Widget w)
{
	SIT_Callback next, cbl;

	if (HAS_EVT(w, SITE_OnFinalize))
	{
		/* need to be done as early as possible while we still have some internal consistency */
		SIT_ApplyCallback(w, NULL, SITE_OnFinalize);
		w->evtFlags &= ~(1 << SITE_OnFinalize);
	}

	if (w->flags & SITF_IsLocked)
	{
		SIT_Widget parent = w->parent;
		w->flags |= SITF_BeingDestroyed;
		/*
		 * if parent is being also destroyed, we need to unlink from it now, because when we
		 * got back here later, memory referenced by parent will be freed.
		 */
		if (parent)
		{
			if (w->flags & SITF_InFocusRing)
				SIT_RemoveFromFocus(w);
			w->parent = NULL;
			ListRemove(&parent->children, &w->node);
			ListAddTail(&sit.pendingDel, &w->node);
		}
		return;
	}

	if (w->flags & SITF_GeomNotified)
	{
		/* in sit.geomList, need to be removed */
		SIT_Widget * prev;
		SIT_Widget   list;
		for (prev = &sit.geomList, list = *prev; list && list != w; prev = &list->geomChanged, list = *prev);
		if (list) *prev = list->geomChanged;
	}
	if (w->flags & SITF_CustomClasses)
		free(w->classes);

	if (sit.activeDlg  == w) sit.activeDlg  = sit.root;
	if (sit.curTooltip == w) sit.curTooltip = NULL;
	if (sit.active     == w) sit.active     = NULL;
	if (sit.hover      == w) sit.hover      = NULL;

	if (sit.focus == w)
	{
		SIT_ApplyCallback(w, NULL, SITE_OnBlur);
		sit.focus = NULL;
	}

	if (w->finalize == NULL)
	{
		if (w->attrs && w->attrs != WidgetClass)
		{
			TagList tags;
			/* if no finalize cb, free all SIT_STR fields */
			for (tags = w->attrs; tags->tl_TagID != SIT_TagEnd; tags ++)
			{
				if (tags->tl_Type != SIT_STR) continue;
				STRPTR * str = (APTR) w + tags->tl_Arg;
				if (*str) free(*str), *str = NULL;
			}
		}
	}
	else w->finalize(w, NULL, NULL);

	if (w->title) free(w->title);
	if (w->inlineStyles) free(w->inlineStyles);

	if (w->layout.flags & LAYF_HasImg)
	{
		/* inlilne image needs to be released to be clear from the cache at some point */
		CSSImage img;
		switch (w->type) {
		case SIT_HTMLTAG: img = (CSSImage) w->manage; break;
		case SIT_LABEL:   img = ((SIT_Label)w)->image; break;
		default:          img = NULL;
		}
		if (img) SIT_UnloadImg(img);
	}

	/* unregister events */
	for (cbl = HEAD(w->callbacks); cbl; cbl = next)
	{
		next = (APTR) cbl->sc_Node.ln_Next;
		if (cbl->sc_Malloc)
			free(cbl);
	}

	if (w->parent)
	{
		SIT_Widget parent = w->parent;
		/* remove from focus ring chain */
		if (w->flags & SITF_InFocusRing)
			SIT_RemoveFromFocus(w);

		if (w->type == SIT_TOOLTIP && parent->tooltip == w)
			parent->tooltip = NULL;
		ListRemove(&parent->children, &w->node);
	}

	SIT_FreeCSS(w);
	free(w);
}

/* remove properties added to window for keeping widget class */
void SIT_DestroyChildren(SIT_Widget w)
{
	SIT_Widget child;

	while ((child = (APTR) ListRemHead(&w->children)))
	{
		/* locked children will detach from 'child' that way */
		child->flags |= SITF_BeingDestroyed;
		if (child->children.lh_Head)
			SIT_DestroyChildren(child);

		SIT_DestroyWidget(child);
	}
}

void SIT_GeomRemoveChildrenOf(SIT_Widget * first, SIT_Widget w)
{
	SIT_Widget prev, list, parent;
	for (prev = NULL, list = *first; list; )
	{
		for (parent = list->parent; parent && parent != w; parent = parent->parent);
		if (parent)
		{
			/* children will be reflowed with parent */
			parent = list->geomChanged;
			if (prev) prev->geomChanged = parent;
			else *first = parent;
			list->geomChanged = NULL;
			list = parent;
		}
		else prev = list, list = list->geomChanged;
	}
}

/* geometric changes done on widget: check neighbor if more changes are needed */
void SIT_InitiateReflow(SIT_Widget w)
{
	// setting height to dialog does nothing
	//	if (w->optimalWidth == NULL)
	//		return;
	if (w->flags & SITF_GeomNotified)
	{
		SIT_Widget inlist;
		for (inlist = sit.geomList; inlist && inlist != w; inlist = inlist->geomChanged);
		if (inlist) return;
	}
	/* check if part of a private list: will be managed by the control itself */
	if ((w->flags & SITF_TopLevel) == 0)
	{
		SIT_Widget parent;
		for (parent = w->parent; parent; parent = parent->parent)
		{
			int flags = parent->flags;
			if ((flags & SITF_PrivateChildren) && parent->vscroll != w && parent->hscroll != w) return;
			if (flags & SITF_GeomNotified)
			{
				/* reflow this control and all of its children */
				SIT_GeomRemoveChildrenOf(&sit.geomList, parent);
				return;
			}
			if (flags & SITF_TopLevel)
			{
				if (flags & SITF_GeometryChanged) return;
				break;
			}
		}
		/* if <w> already has a children being reflow, reflow the whole control */
		for (parent = HEAD(w->children); parent; NEXT(parent))
		{
			if (parent->flags & SITF_GeomNotified)
			{
				SIT_GeomRemoveChildrenOf(&sit.geomList, w);
				break;
			}
		}
	}
	w->flags |= SITF_GeomNotified;
	w->geomChanged = sit.geomList;
	sit.geomList = w;
}

/* was originally a macro :-( */
Bool IsVisible(SIT_Widget w)
{
	while (w->visible == 1 && (w->flags & SITF_TopLevel) == 0)
		w = w->parent;

	return w->visible == 1;
}

/* very important function: allow user to set widget's property at runtime */
DLLIMP void SIT_SetValues(SIT_Widget w, ...)
{
	va_list args;

	if (w == NULL) return;

	va_start(args, w);
	SIT_ParseTags(w, args, w->attrs);
	va_end(args);

	if (w->postProcess)
	{
		w->postProcess(w, NULL, NULL);
		/* avoid reusing the callback */
		w->postProcess = NULL;
	}

	if (w->flags & SITF_RecalcStyles)
	{
		layoutUpdateStyles(w);
		w->flags &= ~SITF_RecalcStyles;
	}

	/* check for geometry changes */
	if (w->flags & SITF_GeometryChanged)
	{
		if (w->type == SIT_HTMLTAG)
			layoutSetSize(w);
		else
			SIT_InitiateReflow(w);
		if (w->parent)
			w->flags &= ~ SITF_GeometryChanged;
	}
}

DLLIMP void SIT_GetValues(SIT_Widget w, ...)
{
	va_list vargs;
	int     tag, usage = 0;

	if (w == NULL) return;
	va_start(vargs, w);
	while ((tag = va_arg(vargs, int)))
	{
		/* look in classArgs for definition of that tag */
		TagList stack[5];
		TagList args;

		if (tag >= SIT_TagUser)
		{
			SIT_OnVal cd = {.tag = tag, .stage = SITV_Get};
			cd.ptr = va_arg(vargs, APTR);
			SIT_ApplyCallback(w, &cd, SITE_OnSetOrGet);
			continue;
		}

		if (tag < SIT_EndCommonTags)
		{
			/* direct lookup on widget class */
			args = WidgetClass + tag - 2;
			goto found;
		}

		for (args = w->attrs; args->tl_TagID != SIT_TagEnd; )
		{
			/* super class */
			if (args->tl_TagID == SIT_SuperClass)
			{
				stack[usage ++] = args + 1;
				args = (TagList) args->tl_Arg;
				continue;
			}
			if (args->tl_TagID != tag)
			{
				args ++;
				if (args->tl_TagID == 0 && usage > 0)
					args = stack[-- usage];
				continue;
			}

			found:
			tag = (args->tl_Type == SIT_ABBR ? args->tl_Arg : 0);

			do {
				args -= tag & 0xff; tag >>= 8;
				APTR field = (STRPTR) w + args->tl_Arg;
				/* tags that require some special processing */
				switch (args->tl_TagID) {
				case SIT_X:
				case SIT_Y:       va_arg(vargs, REAL *)[0] = (&w->box.left)[SIT_X - args->tl_TagID]; continue;
				case SIT_AbsX:
				case SIT_AbsY:    va_arg(vargs, REAL *)[0] = (&w->box.left)[args->tl_TagID - SIT_AbsX] + (&w->offsetX)[args->tl_TagID - SIT_AbsX]; continue;
				case SIT_Width:
				case SIT_Height:  va_arg(vargs, REAL *)[0] = (&w->box.right)[SIT_Width - args->tl_TagID] - (&w->box.left)[SIT_Width - args->tl_TagID]; continue;
				case SIT_Visible: va_arg(vargs, Bool *)[0] = IsVisible(w); continue;
				case SIT_ClientRect:
				{	int * rect = va_arg(vargs, int *);
					rect[0] = w->layout.pos.left + w->offsetX;
					rect[1] = w->layout.pos.top  + w->offsetY;
					rect[2] = w->layout.pos.width;
					rect[3] = w->layout.pos.height;
					if (w->type == SIT_LISTBOX)
					{
						SIT_ListBox list = (SIT_ListBox) w;
						if (list->viewMode == SITV_ListViewReport)
						{
							rect[1] += (int) list->hdrHeight;
							rect[3] -= (int) list->hdrHeight;
						}
					}
				}	continue;
				case SIT_NVGcontext:
					field = sit.nvgCtx;
					nvgFillColorRGBA8(field, w->style.color.rgba);
					nvgTextLetterSpacing(field, w->layout.letterSpacing);
					nvgFontFaceId(field, w->style.font.handle);
					nvgFontSize(field, w->style.font.size);
					va_arg(vargs, APTR *)[0] = field;
					continue;
				case SIT_Padding:
				{	int * padding = va_arg(vargs, int *);
					padding[0] = w->padding[0];
					padding[1] = w->padding[1];
					padding[2] = w->padding[2];
					padding[3] = w->padding[3];
				}	continue;
				case SIT_CompositedAreas:
					va_arg(vargs, DATA8 *)[0] = sit.compoIds;
					continue;
				/* yeah, virtual functions would be better, but there are too few properties that require special processing */
				case SIT_SelectedIndex:
					if (w->type == SIT_LISTBOX)
					{
						SIT_ListBox list = (SIT_ListBox) w;
						*va_arg(vargs, int *) = list->selIndex < 0 ? -1 : list->selIndex / list->columnCount;
						continue;
					}
					break;
				case SIT_ItemCount:
					if (w->type == SIT_LISTBOX)
					{
						*va_arg(vargs, int *) = SIT_ListGetItemCount(w);
						continue;
					}
					break;
				case SIT_TargetRow:
					if (w->type == SIT_LISTBOX)
						((SIT_ListBox)w)->curRow = va_arg(vargs, int);
					continue;
				case SIT_RowTagArg:
				case SIT_RowSelArg:
					if (w->type == SIT_LISTBOX)
						SIT_ListGetArg(w, args->tl_TagID == SIT_RowTagArg, va_arg(vargs, APTR *));
					else
						va_arg(vargs, APTR);
					continue;
				case SIT_CurrentDir: SIT_AppGetCWD(w); break;
					if (w->type == SIT_EDITBOX)
					{
					}
					break;
				case SIT_StartSel:
				case SIT_EndSel:
				case SIT_EditLength:
				case SIT_MaxLines:
				case SIT_Title:
					if (w->type == SIT_EDITBOX)
					{
						/* if nothing selected use cursor pos */
						SIT_EditBox edit = (SIT_EditBox) w;
						switch (args->tl_TagID) {
						case SIT_StartSel:
						case SIT_EndSel:
							if (edit->selStart == edit->selEnd)
							{
								va_arg(vargs,int *)[0] = edit->cursor;
								continue;
							}
							break;
						case SIT_EditLength:
							va_arg(vargs, int *)[0] = edit->length;
							continue;
						case SIT_MaxLines:
							va_arg(vargs, int *)[0] = edit->rowCount;
							continue;
						case SIT_Title:
							/* direct low-level pointer access :-/ */
							va_arg(vargs, STRPTR *)[0] = edit->text;
							continue;
						}
					}
					break;
				}
				switch (args->tl_Type) {
				case SIT_ABBR: break;
				case SIT_CTRL:
				case SIT_PTR:  va_arg(vargs, APTR *)[0]   = * (APTR *)   field; break;
				case SIT_UNIT: va_arg(vargs, REAL *)[0]   = * (REAL *)   field; break;
				case SIT_INT:  va_arg(vargs, int *)[0]    = * (int *)    field; break;
				case SIT_REAL: va_arg(vargs, double *)[0] = * (double *) field; break;
				case SIT_BOOL: va_arg(vargs, Bool *)[0]   = * (DATA8)    field; break;
				case SIT_STR:  va_arg(vargs, STRPTR *)[0] = * (STRPTR *) field;
				}
			}
			while (tag);
			break;
		}
	}
	va_end(vargs);
}

void SIT_MoveWidgets(SIT_Widget w)
{
	SIT_Widget c;
	for (c = HEAD(w->children); c; NEXT(c))
	{
		c->offsetX = w->offsetX + w->box.left;
		c->offsetY = w->offsetY + w->box.top;
		if (c->children.lh_Head)
			SIT_MoveWidgets(c);
	}
}

void SIT_CenterDialog(SIT_Widget w)
{
	float curX = w->layout.pos.left - w->box.left;
	float curY = w->layout.pos.top  - w->box.top;
	w->currentBox.width  = w->box.right - w->box.left;
	w->currentBox.height = w->box.bottom - w->box.top;
	SIT_LayoutWidget(sit.root, w, 0, FitUsingCurrentBox);
	SIT_LayoutWidget(sit.root, w, 1, FitUsingCurrentBox);
	w->layout.pos.left = curX + w->box.left;
	w->layout.pos.top  = curY + w->box.top;
	w->currentBox.width  = w->box.right  - w->box.left;
	w->currentBox.height = w->box.bottom - w->box.top;

	if (memcmp(&w->currentBox, &w->childBox, sizeof w->currentBox))
		SIT_LayoutWidgets(w, FitUsingCurrentBox);
}

/* finishes dialog initialization */
DLLIMP int SIT_ManageWidget(SIT_Widget w)
{
	if (w == NULL)
		return 0;
	if (w->manage == NULL)
	{
		/* manage first its children */
		if (SIT_LayoutWidgets(w, w->visible ? KeepDialogSize : FitUsingInitialBox) == False)
			SIT_Log(SIT_CRITICAL, "Error laying out widgets from '%s'", w->name);

		if (w->type == SIT_DIALOG && w->visible == 0)
		{
			SIT_CenterDialog(w);
			SIT_MoveWidgets(w);

			w->visible = True;
		}
		if (sit.activeDlg->type == SIT_DIALOG)
		{
			/* keep last widget focus */
			((SIT_Dialog)sit.activeDlg)->lastFocus = sit.focus;
		}
		w->flags &= ~SITF_GeometrySet;
		sit.active = sit.hover = NULL;
		sit.activeDlg = w;
		SIT_Widget focus = sit.focus;
		while (focus && focus != w)
			focus = focus->parent;
		if (focus == NULL)
		{
			/* current focus is not a descandent of current dialog: remove it */
			SIT_SetFocus(NULL);
		}
		return 1;
	}
	else return w->manage(w, NULL, NULL); /* custom manage (comdlg) */
}

DLLIMP void SIT_RemoveWidget(SIT_Widget w)
{
	SIT_Widget d;
	Bool msg = False;

	if (w == NULL || (w->flags & SITF_BeingDestroyed)) return;
	for (d = w; d && ! (d->type & SITF_TopLevel); d = d->parent);

	/* Remove from MAX constraint chain */
	SIT_Widget prev = (APTR) w->max.ln_Prev;
	SIT_Widget next = (APTR) w->max.ln_Next;

	if (prev) prev->max.ln_Next = (APTR) next, msg = True;
	if (next) next->max.ln_Prev = (APTR) prev, msg = True;

	if ((SIT_ReassignAttachments(w) || msg) && (d->flags & SITF_GeomNotified) == 0)
		d->flags |= SITF_GeomNotified;

	SIT_DestroyChildren(w);
	SIT_DestroyWidget(w);
	sit.dirty = 1;
}
