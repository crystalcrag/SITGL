/*
 * SIT_Widget.c : base class for widget creation. Dispatch tags to custom class.
 *
 * written by T.Pierron, may 2020.
 */

#define UNICODE
#define SHOBJ
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"


	/* common properties for all widgets */
	TagList WidgetClass[] = {
		{ SIT_Title,        "title",        CSG, SIT_STR,  OFFSET(SIT_Widget, title) },
		{ SIT_Style,        "style",        CSG, SIT_STR,  OFFSET(SIT_Widget, inlineStyle) },
		{ SIT_Name,         "name",         __G, SIT_STR,  OFFSET(SIT_Widget, name) },
		{ SIT_Classes,      "classes",      CSG, SIT_STR,  OFFSET(SIT_Widget, classes) },
		{ SIT_UserData,     "userData",     _SG, SIT_PTR,  OFFSET(SIT_Widget, userData) },
		{ SIT_ResizePolicy, "resizePolicy", _SG, SIT_INT,  OFFSET(SIT_Widget, resizePolicy) },
		{ SIT_Enabled,      "enabled",      _SG, SIT_BOOL, OFFSET(SIT_Widget, enabled) },
		{ SIT_Visible,      "visible",      CSG, SIT_BOOL, OFFSET(SIT_Widget, visible) },
		{ SIT_NextCtrl,     "nextCtrl",     _SG, SIT_STR,  0},
		{ SIT_TabNum,       "tabNum",       _SG, SIT_INT,  OFFSET(SIT_Widget, tabOrder) },
		{ SIT_Height,       "height",       CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.height) },
		{ SIT_Width,        "width",        CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.width) },
		{ SIT_Y,            "y",            CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.top) },
		{ SIT_X,            "x",            CSG, SIT_UNIT, OFFSET(SIT_Widget, fixed.left) },
		{ SIT_Rect,         "rect",         CSG, SIT_ABBR, ABBR(1, 1, 1, 1) },
		{ SIT_Padding,      NULL,           __G, SIT_INT,  0 },
		{ SIT_AbsX,         "absX",         CSG, SIT_UNIT, 0 },
		{ SIT_AbsY,         "absY",         CSG, SIT_UNIT, 0 },
		{ SIT_Parent,       NULL,           __G, SIT_PTR,  OFFSET(SIT_Widget, parent) },
		{ SIT_NextSibling,  NULL,           __G, SIT_PTR,  OFFSET(SIT_Widget, node.ln_Next) },
		{ SIT_PrevSibling,  NULL,           __G, SIT_PTR,  OFFSET(SIT_Widget, node.ln_Prev) },
		{ SIT_FirstChild,   NULL,           __G, SIT_PTR,  OFFSET(SIT_Widget, children.lh_Head) },
		{ SIT_LastChild,    NULL,           __G, SIT_PTR,  OFFSET(SIT_Widget, children.lh_Tail) },
		{ SIT_CtrlType,     NULL,           __G, SIT_INT,  OFFSET(SIT_Widget, type) },
		{ SIT_Composited,   "composited",   _SG, SIT_BOOL, OFFSET(SIT_Widget, composited) },
		{ SIT_NVGcontext,   NULL,           __G, SIT_PTR,  0},
		{ SIT_ToolTip,      "toolTip",      C__, SIT_STR,  0},
		{ SIT_OuterRect,    NULL,           __G, SIT_PTR,  0},

		/* attachments */
		#define	ATTACH(side, field)    OFFSET(SIT_Widget, attachment[side].field)
		{ SIT_LeftOffset,       "leftOffset",       CSG, SIT_UNIT, ATTACH(0, sa_Offset) },
		{ SIT_TopOffset,        "topOffset",        CSG, SIT_UNIT, ATTACH(1, sa_Offset) },
		{ SIT_RightOffset,      "rightOffset",      CSG, SIT_UNIT, ATTACH(2, sa_Offset) },
		{ SIT_BottomOffset,     "bottomOffset",     CSG, SIT_UNIT, ATTACH(3, sa_Offset) },
		{ SIT_LeftObject,       "leftObject",       CSG, SIT_CTRL, ATTACH(0, sa_Arg) },
		{ SIT_TopObject,        "topObject",        CSG, SIT_CTRL, ATTACH(1, sa_Arg) },
		{ SIT_RightObject,      "rightObject",      CSG, SIT_CTRL, ATTACH(2, sa_Arg) },
		{ SIT_BottomObject,     "bottomObject",     CSG, SIT_CTRL, ATTACH(3, sa_Arg) },
		{ SIT_LeftAttachment,   "leftAttachment",   CSG, SIT_INT,  ATTACH(0, sa_Type) },
		{ SIT_TopAttachment,    "topAttachment",    CSG, SIT_INT,  ATTACH(1, sa_Type) },
		{ SIT_RightAttachment,  "rightAttachment",  CSG, SIT_INT,  ATTACH(2, sa_Type) },
		{ SIT_BottomAttachment, "bottomAttachment", CSG, SIT_INT,  ATTACH(3, sa_Type) },
		{ SIT_Left,             "left",             CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ SIT_Top,              "top",              CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ SIT_Right,            "right",            CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ SIT_Bottom,           "bottom",           CSG, SIT_ABBR, ABBR(4, 4, 4, 0) },
		{ SIT_Margins,          "margins",          CSG, SIT_ABBR, ABBR(16,-2,-1,2) },
		{ SIT_MaxWidth,         "maxWidth",         CSG, SIT_CTRL, 0 },
		{ SIT_MinWidth,         "minWidth",         CSG, SIT_UNIT, OFFSET(SIT_Widget, minBox.width) },
		{ SIT_MinHeight,        "minHeight",        CSG, SIT_UNIT, OFFSET(SIT_Widget, minBox.height) },
		{ SIT_MaxBoxWidth,      "maxBoxWidth",      CSG, SIT_UNIT, OFFSET(SIT_Widget, maxBox.width) },
		{ SIT_MaxBoxHeight,     "maxBoxHeight",     CSG, SIT_UNIT, OFFSET(SIT_Widget, maxBox.height) },
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
		"slider", "progress", "combobox", "tab", "tooltip", NULL, NULL, NULL
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
	TagList *     tag = cd;
	SIT_Variant * val = ud;

	if ((w->visible || tag->tl_TagID == SIT_Visible || w->type == SIT_HTMLTAG) && tag->tl_Flags == CSG &&
	    /* resize policy set to fixed means that control cannot change by setting the title */
	    ! (tag->tl_TagID == SIT_Title && (w->resizePolicy == SITV_Fixed || w->type == SIT_DIALOG || w->type == SIT_EDITBOX)))
	{
		/* setting this property will require a geometric reflow */
		APTR p = (STRPTR)w + tag->tl_Arg;
		/* check first if value differs */
		switch (tag->tl_Type) {
		default:       if (* (int  *)    p == val->integer) return 0; break; /* SIT_INT */
		case SIT_LONG: if (* (long *)    p == val->longInt) return 0; break;
		case SIT_BOOL: if (* (uint8_t *) p == val->boolean) return 0; break;
		case SIT_UNIT: if (* (float *)   p == val->real)    return 0; break;
		case SIT_REAL: if (* (double *)  p == val->real)    return 0;
		/* no need to check SIT_CTRL, SIT_PTR, SIT_STR */
		}
		int flg = SITF_GeometryChanged;
		if (tag->tl_TagID == SIT_Title && w->resizePolicy == SITV_Fixed)
			flg = 0;
		if (w->flags & SITF_TopLevel)
		{
			/* optimize in case only position of dialog is changed: don't recalc whole geom */
			switch (tag->tl_TagID) {
			case SIT_X: flg = SITF_Style1Changed; break;
			case SIT_Y: flg = SITF_Style2Changed; break;
			}
		}
		w->flags |= flg;
	}

	if (tag->tl_Arg > 0)
	{
		STRPTR * str;
		#define	SET_VALUE(widget, type, val) \
			* (type *) ((char *)widget + tag->tl_Arg) = val

		if (tag->tl_TagID == SIT_Enabled)
			w->oldEna = w->enabled;
		if (tag->tl_Flags == C__)
		{
			if (w->flags & SITF_InitDone)
				fprintf(stderr, "warning: %s: setting create-only property '%s'\n", w->name, tag->tl_TagName);
		}
		else if ((tag->tl_Flags & _S_) == 0)
		{
			fprintf(stderr, "warning: %s: setting read-only property '%s'\n", w->name, tag->tl_TagName);
		}
		switch (tag->tl_Type) {
		case SIT_ABBR: break;
		case SIT_CTRL:
		case SIT_PTR:  SET_VALUE(w, void *,  val->pointer); break;
		case SIT_INT:  SET_VALUE(w, int,     val->integer); break;
		case SIT_LONG: SET_VALUE(w, long,    val->longInt); break;
		case SIT_REAL: SET_VALUE(w, double,  val->real);    break;
		case SIT_BOOL: SET_VALUE(w, uint8_t, !!val->boolean); /* we need 0 or 1 */ break;
		case SIT_UNIT: SET_VALUE(w, REAL,    val->real);    break;
		case SIT_STR:
			str = (STRPTR *) ((STRPTR)w + tag->tl_Arg);
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
		if (val->boolean == 0 && sit.curTooltip == w)
		{
			if (sit.toolTip)
				SIT_ActionReschedule(sit.toolTip, -1, -1);
			sit.curTooltip = NULL;
			sit.toolTip = NULL;
		}
		break;
	case SIT_Enabled:
		w->oldState = w->oldEna ? 0 : STATE_DISABLED;
		w->flags |= SITF_RecalcStyles;
		break;
	case SIT_Style:
		w->flags |= SITF_RecalcStyles;
		memset(w->layout.crc32, 0xff, sizeof w->layout.crc32);
		cssParseStyles(w->inlineStyle, w->styles);
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
		/* needed by Geometry reflow (label) 1 unittest */
		if (w->type == SIT_DIALOG) w->box.right = w->box.left + w->fixed.width, w->flags |= SITF_KeepDiagSize; // used by KeepDialogSize
		w->flags |= SITF_FixedWidth;
		break;
	case SIT_Height:
		if (w->type == SIT_DIALOG) w->box.bottom = w->box.top + w->fixed.height, w->flags |= SITF_KeepDiagSize;
		w->flags |= SITF_FixedHeight;
		break;
//	case SIT_TagPrivate1:
//		if (w->sw_BuddyText)
//			SIT_CreateBuddyLabel(w, w->buddyText, ((SIT_Variant *)ud)->pointer);
//		break;
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
	w->flags |= SITF_RenderChildren;
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
	SizeF     min;

	layoutMeasureWords(w, &min);
	frame->title.width  = roundf(min.width);
	frame->title.height = roundf(min.height);

	/* layout children */
	if (w->children.lh_Head && SIT_LayoutWidgets(w, (ULONG) mode))
	{
		float width = w->box.right  - w->box.left;
		if (min.width < width) min.width = width;
		min.height = w->box.bottom - w->box.top;
	}

	int i;
	for (i = 0; i < 4; i ++)
	{
		REAL * pad = w->padding + i;
		if (*pad < fh)
		{
			(&min.width)[i&1] +=  - *pad;
			*pad = fh;
		}
	}

	frame->title.width += roundf((w->padding[0] + w->padding[1]) * 0.25);

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
	val = w->style.font.size;
	if (max < val) max = val;
	frame->padLeft = max;

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
	switch (FindInList("NONE,FIRST,LAST", ctrl, 0)) {
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

	if (parent == NULL && type != SIT_APP) return NULL;

	int        len = strlen(name)+1;
	int        sz  = sizeof_widgets[type<<1];
	int        cbs = sizeof_widgets[(type<<1)+1];
	SIT_Widget w   = calloc(sz + cbs + len + extra, 1); /* prealloc as much as possible */
	va_list    args;

	vector_init(w->layout.wordwrap, sizeof (struct WordWrap_t));

	if (parent && parent->type == SIT_DIALOG && ((SIT_Dialog)parent)->clientArea)
		parent = ((SIT_Dialog)parent)->clientArea;

	w->name     = (APTR) w + sz + cbs;   /* id for CSS */
	w->tagName  = widgetNames[type];     /* tag name for CSS */
	w->type     = type;
	w->parent   = parent;
	w->attrs    = WidgetClass;
	w->setValue = SIT_SetWidgetValue;
	w->enabled  = True;
	w->visible  = type != SIT_DIALOG;
	w->cbSlot   = cbs;
	w->slots    = (APTR) w + sz;

	memset(w->layout.crc32, 0xff, sizeof w->layout.crc32);
	strcpy(w->name, name);

	SIT_SplitName(w);

	w->minBox.width = w->minBox.height = -1;
	w->optimalBox   = w->maxBox = w->minBox;
	if (extra > 0) w->userData = w->name + len;

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

float SIT_EmToReal(SIT_Widget w, uint32_t val)
{
	REAL size = w->parent ? w->parent->style.font.size : sit.defFontHeight;

	if (val & (1<<31))
	{
		val &= 0x7fffffff;
		if (val & (1<<30)) val |= (1<<31); /* sign ext */
		return roundf(size * ((int) val * (1/8192.)));
	}
	else
	{
		if (val & (1<<30)) val |= (1<<31); /* sign ext */
		return (int) val;
	}
}

STRPTR RawDoFmt(STRPTR format, va_list *);

void SIT_ParseTags(SIT_Widget w, va_list vargs, TagList * classArgs)
{
	KeyVal list = NULL;
	int    tag;

	next: for (;;)
	{
		/* Look in classArgs for definition of that tag */
		TagList *   stack[5];
		TagList *   args;
		SIT_Variant value;
		ULONG       usage = 0;
		Bool        format;

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
		if (tag < SIT_TagPrivate3 /* end of widget class tags */)
		{
			args = WidgetClass + tag - 2;
			goto found;
		}

		for (args = classArgs; args->tl_TagID != SIT_TagEnd; )
		{
			/* super class */
			if (args->tl_TagID == SIT_SuperClass)
			{
				stack[usage ++] = args + 1;
				args = (TagList *) args->tl_Arg;
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
					case SIT_LONG: case SIT_BOOL: case SIT_INT: value.integer = list->key.val; break;
					case SIT_UNIT: value.real = SIT_EmToReal(w, list->key.val); break;
					case SIT_STR:  value.string = list->key.ptr; list ++; goto assign_str;
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
				case SIT_LONG: value.longInt = va_arg(vargs, long);   break;
				case SIT_REAL: value.real    = va_arg(vargs, double); break;
				case SIT_UNIT: value.real    = SIT_EmToReal(w, va_arg(vargs, int)); break;
				case SIT_STR: /* strings have to be malloced ... */
					value.string = va_arg(vargs, STRPTR);
					assign_str:
					/* ... but there are a few exceptions */
					switch (args->tl_TagID) {
					case SIT_Style:
						if (w->flags & SITF_StaticStyles)
						{
							/* do not free */
							w->flags &= ~SITF_StaticStyles;
							w->inlineStyle = NULL;
						}
						break;
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
			w->evtFlags    |= SET_EVT(type);

			/* last In, first Apply order */
			ListAddHead(&w->callbacks, &cb->sc_Node);

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
	SIT_Callback cb;

	if (w == NULL) return;

	for (cb = HEAD(w->callbacks); cb && !(cb->sc_CB == proc &&
	     cb->sc_UserData == data && cb->sc_Event == type); NEXT(cb));

	if (cb) SIT_DelCB(w, cb);

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
	free(node);
}


/* free memory and resources allocated for widget */
void SIT_DestroyWidget(SIT_Widget w)
{
	SIT_Callback next, cbl;

	if (w->flags & SITF_IsLocked)
	{
		w->flags |= SITF_BeingDestroyed;
		/*
		 * if parent is being also destroyed, we need to unlink from it now, because when we
		 * got back here later, memory referenced by parent would have been already freed.
		 */
		if (w->parent && (w->parent->flags & SITF_BeingDestroyed))
			w->parent = NULL;
		return;
	}

	if (HAS_EVT(w, SITE_OnFinalize))
		SIT_ApplyCallback(w, NULL, SITE_OnFinalize);
	if (w->finalize == NULL)
	{
		if (w->attrs && w->attrs != WidgetClass)
		{
			TagList * tags;
			/* if no finalize cb, free all SIT_STR fields */
			for (tags = w->attrs; tags->tl_TagID != SIT_TagEnd; tags ++)
			{
				if (tags->tl_Type != SIT_STR) continue;
				STRPTR * str = (APTR) w + tags->tl_Arg;
				if (*str) free(*str);
			}
		}
	}
	else w->finalize(w, NULL, NULL);

	if (w->title) free(w->title);
	if (w->inlineStyle && (w->flags & SITF_StaticStyles) == 0)
		free(w->inlineStyle);

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
		{
			SIT_Widget * prev;
			SIT_Widget   list;
			while ((parent->flags & SITF_TopLevel) == 0)
				parent = parent->parent;

			for (prev = &FOCUSRING(parent), list = *prev; list && list != w; prev = &list->nextCtrl, list = *prev);
			if (list) *prev = list->nextCtrl;

			parent = w->parent;
		}
		if (w->type == SIT_TOOLTIP && parent->tooltip == w)
			parent->tooltip = NULL;
		ListRemove(&parent->children, &w->node);
	}

	SIT_FreeCSS(w);
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

static void SIT_GeomRemoveChildrenOf(SIT_Widget w)
{
	SIT_Widget prev, list, parent;
	for (prev = NULL, list = sit.geomList; list; )
	{
		for (parent = list->parent; parent && parent != w; parent = parent->parent);
		if (parent)
		{
			/* children will be reflowed with parent */
			parent = list->geomChanged;
			if (prev) prev->geomChanged = parent;
			else sit.geomList = parent;
			list->geomChanged = NULL;
			list = parent;
		}
		else prev = list, list = list->geomChanged;
	}
}

/* geometric changes done on widget: check neighbor if more changes are needed */
void SIT_InitiateReflow(SIT_Widget w)
{
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
				SIT_GeomRemoveChildrenOf(parent);
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
				SIT_GeomRemoveChildrenOf(w);
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
/*	else if (w->flags & SITF_PostProcess)
	{
		SIT_OnVal cd = {.stage = SITV_PostProcess};
		w->flags &= ~SITF_PostProcess;
		SIT_ApplyCallback(w, &cd, SITE_OnSetOrGet);
	} */

	if (w->flags & SITF_RecalcStyles)
	{
		layoutUpdateStyles(w);
		w->flags &= ~SITF_RecalcStyles;
	}

	/* check for geometry changes */
	if (w->flags & SITF_GeometryChanged)
	{
//		if (IsVisible(w))
		if (w->type == SIT_HTMLTAG)
			layoutSetSize(w);
		else
			SIT_InitiateReflow(w);
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
		TagList * stack[5];
		TagList * args;

		if (tag >= SIT_TagUser)
		{
			SIT_OnVal cd = {.tag = tag, .stage = SITV_Get};
			cd.ptr = va_arg(vargs, APTR);
			SIT_ApplyCallback(w, &cd, SITE_OnSetOrGet);
			continue;
		}

		if (tag < SIT_TagPrivate3)
		{
			/* Direct lookup on widget class */
			args = WidgetClass + tag - 2;
			goto found;
		}

		for (args = w->attrs; args->tl_TagID != SIT_TagEnd; )
		{
			/* Super class */
			if (args->tl_TagID == SIT_SuperClass)
			{
				stack[usage ++] = args + 1;
				args = (TagList *) args->tl_Arg;
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
				//case SIT_HotKey:      SIT_EditGetText((APTR)w); break;
				case SIT_X:
				case SIT_Y:           va_arg(vargs, REAL *)[0] = (&w->box.left)[SIT_X - args->tl_TagID]; continue;
				case SIT_AbsX:
				case SIT_AbsY:        va_arg(vargs, REAL *)[0] = (&w->box.left)[args->tl_TagID - SIT_AbsX] + (&w->offsetX)[args->tl_TagID - SIT_AbsX]; continue;
				case SIT_Width:
				case SIT_Height:      va_arg(vargs, REAL *)[0] = (&w->box.right)[SIT_Width - args->tl_TagID] - (&w->box.left)[SIT_Width - args->tl_TagID]; continue;
				case SIT_Visible:     va_arg(vargs, Bool *)[0] = IsVisible(w); continue;
				case SIT_NVGcontext:  va_arg(vargs, APTR *)[0] = sit.nvgCtx; continue;
				case SIT_Padding:
				{	int * padding = va_arg(vargs, int *);
					padding[0] = w->padding[0];
					padding[1] = w->padding[1];
					padding[2] = w->padding[2];
					padding[3] = w->padding[3];
				}	continue;
				case SIT_CompositedArea:
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
				case SIT_Title:
					if (w->type == SIT_EDITBOX)
					{
						/* direct low-level pointer access :-/ */
						va_arg(vargs, STRPTR *)[0] = ((SIT_EditBox)w)->text;
						continue;
					}
				}
				switch (args->tl_Type) {
				case SIT_ABBR: break;
				case SIT_CTRL:
				case SIT_PTR:  va_arg(vargs, APTR *)[0]   = * (APTR *)   field; break;
				case SIT_UNIT: va_arg(vargs, REAL *)[0]   = * (REAL *)   field; break;
				case SIT_INT:  va_arg(vargs, int *)[0]    = * (int *)    field; break;
				case SIT_LONG: va_arg(vargs, long *)[0]   = * (long *)   field; break;
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

void SIT_CenterDialog(SIT_Widget w, int flags)
{
	if (flags & 1)
	{
		REAL x = roundf((sit.scrWidth - w->box.right + w->box.left) * 0.5) - w->box.left;
		w->layout.pos.left += x;
		w->box.left  += x;
		w->box.right += x;
	}

	if (flags & 2)
	{
		REAL y = roundf((sit.scrHeight - w->box.bottom + w->box.top) * (2 / 5.)) - w->box.top;
		w->layout.pos.top += y;
		w->box.top    += y;
		w->box.bottom += y;
	}
}

/* finishes dialog initialization */
#define dialog  ((SIT_Dialog)w)
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
			SIT_CenterDialog(w, 3);
			SIT_MoveWidgets(w);

			w->visible = True;
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
#undef dialog

DLLIMP void SIT_RemoveWidget(SIT_Widget w)
{
	SIT_Widget d;
	Bool msg = False;

	if (w == NULL || (w->flags & SITF_BeingDestroyed)) return;
	for (d = w; d && ! (d->type & SITF_TopLevel); d = d->parent);
//	if (w->flags & SITF_IsLocked) {
//		PostMessageW(d->sw_Handle, WM_DELAYDESTROY, 0, (LPARAM) w->sw_Handle);
//		return;
//	}

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
