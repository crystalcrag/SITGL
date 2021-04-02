/*
 * SIT.h : main header for Simple Interface Toolkit GL-edition.
 *
 * written by T.Pierron, apr 2020.
 */

#ifndef	SITGLLIB_H
#define	SITGLLIB_H

#ifndef DLLIMP
#ifdef DEBUG_SIT
# define DLLIMP
#elif BUILDING_DLL
# define DLLIMP __declspec (dllexport)
#else
# define DLLIMP __declspec (dllimport)
#endif
#endif

#include "UtilityLibLite.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
#define	SENTINEL                     __attribute__ ((sentinel))
#define PRINTFWARN(posfmt, posarg)   __attribute__ ((format(printf,posfmt,posarg)))
#else
#define SENTINEL
#define PRINTFWARN(posfmt, posarg)
#endif

/* basic datatypes (plus those defined in UtilityLib) */
typedef struct SIT_Widget_t *    SIT_Widget;
typedef struct KeyVal_t *        KeyVal;
typedef struct SIT_Accel_t       SIT_Accel;

/* datatypes passed as second argument for callback events */
typedef struct SIT_OnKey_t       SIT_OnKey;      /* OnVanillaKey, OnRawKey */
typedef struct SIT_OnMouse_t     SIT_OnMouse;    /* OnClick, OnMouseMove */
typedef struct SIT_OnSort_t      SIT_OnSort;     /* OnSortItem */
typedef struct SIT_OnPaint_t     SIT_OnPaint;    /* OnPaint */
typedef struct SIT_OnVal_t       SIT_OnVal;      /* OnSetOrGet */
typedef struct LocaleInfo_t *    LocaleInfo;     /* value stored for SIT_LocaleInfo tag */

/* generic callback prototype for events */
typedef int (*SIT_CallProc)(SIT_Widget, APTR call_data, APTR user_data);

typedef enum
{
	SIT_APP,               /* main app widget */
	SIT_DIALOG,            /* top level shell dialog */
	SIT_LABEL,             /* label with HTML tags (inline only) */
	SIT_BUTTON,            /* push button, toggle button, check box */
	SIT_EDITBOX,           /* single/multi-line edit field (text, password, number or float) */
	SIT_FRAME,             /* like HTML <fieldset> */
	SIT_LISTBOX,           /* list of objects that can be viewed as icon or column */
	SIT_CANVAS,            /* user-defined content */
	SIT_SCROLLBAR,         /* horizontal/vertical scroll bar */
	SIT_SLIDER,            /* select a value using a slider */
	SIT_PROGRESS,          /* progress indicator */
	SIT_COMBOBOX,          /* drop down list or editable list */
	SIT_TAB,               /* tab pane */
	SIT_TOOLTIP,           /* popup windows with small caption text */
	SIT_FILESELECT,        /* file selection/save (native widget) */
	SIT_DIRSELECT,         /* directory selection (native widget) */
	SIT_HTMLTAG            /* generic HTML tag (mostly used for internal stuff) */
}	SIT_TYPE;

typedef enum               /* return code from SIT_RenderNodes() */
{
	SIT_RenderNothing,     /* back buffer not updated, no need to swap GL buffer */
	SIT_RenderDone,        /* render is complete, should swap GL buffer */
	SIT_RenderComposite    /* composited area remaining, check SIT_CompositedArea on SIT_App to see areas safe to draw */
}	SIT_RENDER;

/*
 * all public functions of this library, check main documentation for details
 */
DLLIMP SIT_Widget SIT_Init(int nvgFlags, int width, int height, STRPTR theme, int _1_if_theme_is_path_0_if_string);
DLLIMP SIT_Widget SIT_CreateWidget(STRPTR name, SIT_TYPE type, SIT_Widget parent, ...) SENTINEL;
DLLIMP SIT_Widget SIT_GetById(SIT_Widget parent, STRPTR name);
DLLIMP SIT_Widget SIT_GetFocus(void);
DLLIMP STRPTR     SIT_GetError(void);
DLLIMP void       SIT_Nuke(int what);
DLLIMP void       SIT_Exit(int code);
DLLIMP void       SIT_Free(APTR pointer);
DLLIMP void       SIT_Log(int level, STRPTR fmt, ...) PRINTFWARN(2,3);
DLLIMP SIT_RENDER SIT_RenderNodes(float time_in_ms);
DLLIMP void       SIT_RenderNode(SIT_Widget root);
DLLIMP Bool       SIT_CreateWidgets(SIT_Widget parent, STRPTR fmt, ...);
DLLIMP void       SIT_RemoveWidget(SIT_Widget);
DLLIMP Bool       SIT_AddCallback(SIT_Widget, int type, SIT_CallProc, APTR data);
DLLIMP void       SIT_DelCallback(SIT_Widget, int type, SIT_CallProc, APTR data);
DLLIMP void       SIT_ClearCallbacks(SIT_Widget w);
DLLIMP int        SIT_ApplyCallback(SIT_Widget, APTR cd, int type);
DLLIMP void       SIT_SetValues(SIT_Widget, ...) SENTINEL;
DLLIMP void       SIT_GetValues(SIT_Widget, ...) SENTINEL;
DLLIMP STRPTR     SIT_GetHTMLAttr(SIT_Widget node, STRPTR key);
DLLIMP void       SIT_SetAttributes(SIT_Widget parent, STRPTR fmt, ...);
DLLIMP void       SIT_SetFocus(SIT_Widget);
DLLIMP void       SIT_CloseDialog(SIT_Widget);
DLLIMP int        SIT_ManageWidget(SIT_Widget);
DLLIMP int        SIT_ComboInsertItem(SIT_Widget, int index, STRPTR item, APTR rowTag);
DLLIMP int        SIT_ComboDeleteItem(SIT_Widget, int index);
DLLIMP APTR       SIT_ComboGetRowTag(SIT_Widget, int nth, STRPTR * label);
DLLIMP SIT_Widget SIT_TabGetNth(SIT_Widget, int nth);
DLLIMP void       SIT_TabSplice(SIT_Widget, int pos, int del, ...) SENTINEL;
DLLIMP int        SIT_ListInsertItem(SIT_Widget, int row, APTR rowTag, ...);
DLLIMP void       SIT_ListDeleteRow(SIT_Widget, int row);
DLLIMP SIT_Widget SIT_ListInsertControlIntoCell(SIT_Widget, int row, int cell);
DLLIMP void       SIT_ListFinishInsertControl(SIT_Widget);
DLLIMP int        SIT_TextGetWithSoftline(SIT_Widget, STRPTR buffer, int max);
DLLIMP void       SIT_MoveNearby(SIT_Widget, int XYWH[4], int defAlign);
DLLIMP void       SIT_ForceRefresh(void);
DLLIMP int        SIT_NeedRefresh(void);
DLLIMP int        SIT_InitDrag(SIT_CallProc);

DLLIMP void       SIT_ListSetCell(SIT_Widget w, int row, int col, STRPTR text, APTR rowTag);
DLLIMP Bool       SIT_ListSetColumn(SIT_Widget, int col, int width, int align, STRPTR label);

DLLIMP STRPTR     SIT_GetFromClipboard(STRPTR type, int * size);
DLLIMP Bool       SIT_CopyToClipboard(STRPTR text, int size);

/* pump events to the library */
DLLIMP void       SIT_ProcessMouseMove(float x, float y);
DLLIMP int        SIT_ProcessClick(float x, float y, int button, int pressed);
DLLIMP int        SIT_ProcessKey(int key, int modifier, int pressed);
DLLIMP int        SIT_ProcessChar(int codePoint, int modifier);
DLLIMP void       SIT_ProcessResize(int width, int height);

/* special value for width, align and label of SIT_ListSetColumn() */
#define	DontChange               (-1)
#define	DontChangePtr            ((APTR)-1)
#define	AutoArrange              (-2)  // 'width' only
#define	DeleteAllRows            (-2)  // SIT_ListDeleteRow

/* special value for SIT_ListInsertItem() */
#define SITV_TDSubChild          ((STRPTR)-1)


/*
 * can be or'ed or added to type parameter of SIT_CreateWidget() to allocate an extra amount
 * of memory with the widget; use SIT_UserData to get a pointer to the beginning of the buffer
 * block will be zeroed at creation, 32bit aligned
 */
#define SIT_EXTRA(amount)       ((amount)<<16)

enum /* possible parameters for 'level' of SIT_Log() */
{
	SIT_CRITICAL,
	SIT_ERROR,
	SIT_WARN,
	SIT_INFO,
	SIT_DEBUG
};

enum /* possible parameters for 'what' of SIT_Nuke() */
{
	SITV_NukeCtrl,           /* want to load a completely new interface */
	SITV_NukeTheme,          /* want to restart everything from scratch */
	SITV_NukeAll             /* free all possible memory allocated by SITGL */
};

/* tag list for SIT_CreateWidget */
enum
{
	SIT_TagEnd           = 0,    /* mark end of tag list */
	SIT_TagList          = 1,    /* KeyVal */

	/* generic tags   -   Note: C = Create, S = Set, G = Get, R = Geometric reflow if set */
	/* note: order of these tags matter up to SIT_TagPrivate3: they must match declaration of WidgetClass[] is SIT_Wigdet.c */
	SIT_Title            = 2,    /* CSGR: String */
	SIT_Style            = 3,    /* CSGR: String (inline CSS styles) */
	SIT_Name             = 4,    /* __G_: String */
	SIT_Classes          = 5,    /* CSGR: String */
	SIT_UserData         = 6,    /* CSG_: Pointer */
	SIT_ResizePolicy     = 7,    /* CSGR: Enum */
	SIT_Enabled          = 8,    /* CSG_: Bool */
	SIT_Visible          = 9,    /* CSGR: Bool */
	SIT_NextCtrl         = 10,   /* CSG_: String */
	SIT_TabNum           = 11,   /* CSG_: Int */
	SIT_Height           = 12,   /* CSGR: Int (unit) */
	SIT_Width            = 13,   /* CSGR: Int (unit) */
	SIT_Y                = 14,   /* CSGR: Int (unit) */
	SIT_X                = 15,   /* CSGR: Int (unit) */
	SIT_Rect             = 16,   /* CSGR: Int,Int,Int,Int (X,Y,W,H) (unit) */
	SIT_Padding          = 17,   /* __G_: Int[4] (L,T,R,B) */
	SIT_AbsX             = 18,   /* CSGR: Int (unit) */
	SIT_AbsY             = 19,   /* CSGR: Int (unit) */
	SIT_Parent           = 20,   /* __G_: SIT_Widget */
	SIT_NextSibling      = 21,   /* __G_: SIT_Widget */
	SIT_PrevSibling      = 22,   /* __G_: SIT_Widget */
	SIT_FirstChild       = 23,   /* __G_: SIT_Widget */
	SIT_LastChild        = 24,   /* __G_: SIT_Widget */
	SIT_CtrlType         = 25,   /* __G_: Int */
	SIT_Composited       = 26,   /* CSG_: Int */
	SIT_NVGcontext       = 27,   /* __G_: NVGcontext * */
	SIT_ToolTip          = 28,   /* CS__: String */
	SIT_OuterRect        = 29,   /* __G_: Int[4] or Rect */
	SIT_LeftOffset       = 30,   /* CSGR: Int (unit) */
	SIT_TopOffset        = 31,   /* CSGR: Int (unit) */
	SIT_RightOffset      = 32,   /* CSGR: Int (unit) */
	SIT_BottomOffset     = 33,   /* CSGR: Int (unit) */
	SIT_LeftObject       = 34,   /* CSGR: Pointer */
	SIT_TopObject        = 35,   /* CSGR: Pointer */
	SIT_RightObject      = 36,   /* CSGR: Pointer */
	SIT_BottomObject     = 37,   /* CSGR: Pointer */
	SIT_LeftAttachment   = 38,   /* CSGR: Enum */
	SIT_TopAttachment    = 39,   /* CSGR: Enum */
	SIT_RightAttachment  = 40,   /* CSGR: Enum */
	SIT_BottomAttachment = 41,   /* CSGR: Enum */
	SIT_Left             = 42,   /* CSGR: Int,Pointer,Int (unit) */
	SIT_Top              = 43,   /* CSGR: Int,Pointer,Int (unit) */
	SIT_Right            = 44,   /* CSGR: Int,Pointer,Int (unit) */
	SIT_Bottom           = 45,   /* CSGR: Int,Pointer,Int (unit) */
	SIT_Margins          = 46,   /* CSGR: Int,Int,Int,Int (T,R,B,L) (unit) */
	SIT_MaxWidth         = 47,   /* CSGR: SIT_Widget */
	SIT_MinWidth         = 48,   /* CSGR: Int (unit) */
	SIT_MinHeight        = 49,   /* CSGR: Int (unit) */
	SIT_MaxBoxWidth      = 50,   /* CSGR: Int (unit) */
	SIT_MaxBoxHeight     = 51,   /* CSGR: Int (unit) */
	SIT_TagPrivate3      = 52,   /* Sentinel, not a tag */

	/* App */
	SIT_DefSBArrows      = 53,   /* CSG: Enum, see SIT_ArrowType */
	SIT_DefSBSize        = 54,   /* CSG: Int (unit) */
	SIT_RefreshMode      = 55,   /* CSG: Enum */
	SIT_LocaleInfo       = 56,   /* __G: LocaleInfo * */
	SIT_CurrentDir       = 57,   /* CSG: String */
	SIT_ScreenWidth      = 58,   /* __G: Int */
	SIT_ScreenHeight     = 59,   /* __G: Int */
	SIT_AddFont          = 60,   /* _S_: String, String */
	SIT_AccelTable       = 61,   /* CSG: SIT_Accel * */
	SIT_ExitCode         = 62,   /* CSG: Int * */
	SIT_StyleSheet       = 63,   /* _S_: String */
	SIT_CompositedArea   = 64,   /* __G: Int */

	/* Dialog tags */
	SIT_DialogStyles     = 65,   /* C__: Bitfield */

	/* Label tags */
	SIT_Overflow         = 67,   /* C___: Enum */
	SIT_ImagePath        = 68,   /* CSGR: String */
	SIT_LabelSize        = 69,   /* CSGR: Int (see macro SITV_LabelSize) */

	/* Push/Toggle button */
	SIT_ButtonType       = 70,   /* C___: Enum */
	SIT_CheckState       = 71,   /* CSG_: Enum */
	SIT_RadioGroup       = 72,   /* C___: Int */
	SIT_CurValue         = 73,   /* CSG_: Pointer */
	SIT_RadioID          = 74,   /* C___: Int */

	/* Edit field */
	SIT_EditType         = 75,   /* C__: Int */
	SIT_ReadOnly         = 76,   /* CSG: Bool */
	SIT_PlaceHolder      = 77,   /* CSG: String */
	SIT_HotKey           = 78,   /* CSG: Int */
	SIT_StartSel         = 79,   /* CSG: Int */
	SIT_EndSel           = 80,   /* CSG: Int */
	SIT_EditBuffer       = 81,   /* C__: String */
	SIT_EditLength       = 82,   /* C__: Int */
	SIT_MaxUndo          = 83,   /* C__: Int */
	SIT_MaxLines         = 84,   /* C__: Int */
	SIT_WordWrap         = 85,   /* C__: Enum (SITV_WW*) */
//	SIT_TabStyle         = 118,  /* _SG: Int (defined for Tab: see enum SITV_TabEdit* */

	/* List box */
	SIT_ListBoxFlags     = 86,   /* C__: Enum */
	SIT_ColumnCount      = 87,   /* __G: Int */
	SIT_ColumnNames      = 88,   /* CSG: String */
	SIT_ColumnWidths     = 89,   /* CSG: Int */
	SIT_ColumnAlign      = 90,   /* CSG: String */
	SIT_SortColumn       = 91,   /* CSG: Int */
	SIT_TargetRow        = 92,   /* ___: Int (private, use SIT_RowTag() or SIT_RowSel()) */
	SIT_RowTagArg        = 93,   /* _SG: Pointer (private) */
	SIT_RowSelArg        = 94,   /* _SG: Bool (private) */
	SIT_MakeVisible      = 96,   /* _S_: Int */
//	SIT_ItemCount        = 117,  /* __G: Int (defined in ComboBox: same datatype, same semantic) */
	SIT_ViewMode         = 98,   /* C__: Enum */

	/* Scrollbar, Slider and Progress */
	SIT_MinValue         = 99,   /* CSG: Int */
	SIT_MaxValue         = 100,  /* CSG: Int */
	SIT_StepValue        = 101,  /* CSG: Real (double) */
	SIT_PageSize         = 102,  /* CSG: Int */
	SIT_LineHeight       = 103,  /* CSG: Int */
	SIT_ScrollPos        = 104,  /* CSG: Int */
	SIT_HorizScroll      = 105,  /* C__: Bool */
	SIT_ThumbThick       = 106,  /* CSG: Int */
	SIT_ThumbHeight      = 107,  /* CSG: Int */
	SIT_GaugePadding     = 108,  /* CSG: Int */
	SIT_BuddyEdit        = 109,  /* C__: SIT_Widget */
	SIT_IsDragged        = 110,  /* __G: Bool */
	SIT_ArrowType        = 112,  /* C__: Enum */
	SIT_WheelMult        = 113,  /* CSG: Int */

	/* ComboBox */
	SIT_InitialValues    = 114,  /* CSG: String */
	SIT_SelectedIndex    = 115,  /* CSG: Int */
	SIT_AutoComplete     = 116,  /* C__: Int (0: none, 1: text, 2: text and show list) */
	SIT_ItemCount        = 117,  /* __G: Int */

	/* Tab */
	SIT_TabStyle         = 118,  /* C__: Enum */
	SIT_TabStr           = 119,  /* CSG: String */
	SIT_TabCount         = 120,  /* __G: Int */
	SIT_TabSpace         = 121,  /* CSG: Int (unit) */
	SIT_TabActive        = 122,  /* CSG: Int */

	/* ToolTip */
	SIT_DisplayTime      = 123,  /* CSG: Int (millisec) */
	SIT_DelayTime        = 124,  /* CSG: Int (millisec) */
	SIT_ToolTipAnchor    = 125,  /* CSG: Enum */

	/* FileDialog */
	SIT_Filters          = 126,  /* CSG: String */
	SIT_SelFilter        = 127,  /* CSG: Int */
	SIT_InitPath         = 128,  /* CSG: String */
	SIT_DlgFlags         = 129,  /* CSG: Int */
	SIT_SaveState        = 130,  /* __G: String */
	SIT_SelPath          = 131,  /* __G: String */
	SIT_NbSelect         = 132,  /* __G: Int */

	SIT_TagPrivate       = 0x10000,
	SIT_TagUser          = 0x100000
};

/* aliases */
#define	SIT_SliderPos        SIT_ScrollPos
#define	SIT_ProgressPos      SIT_ScrollPos

#define	SIT_RowTag(row)      SIT_TargetRow, row, SIT_RowTagArg
#define	SIT_RowSel(row)      SIT_TargetRow, row, SIT_RowSelArg

#define SITV_LabelSize(w,h)  (int) ((w) | ((h) << 16))

enum         /* SIT_RefreshMode */
{
	SITV_RefreshAlways,      /* default: redraw interface at each frame */
	SITV_RefreshAsNeeded     /* only when something has changed */
};

enum         /* SIT_Overflow */
{
	SITV_Multiline,
	SITV_Hidden,
	SITV_EllipsisRight,
	SITV_EllipsisPath,
	SITV_EllipsisLeft
};

/* alias */
#define SITV_EllipsisMiddle     	SITV_EllipsisPath

typedef enum /* SIT_Attachment */
{
	SITV_AttachNone,
	SITV_AttachForm,
	SITV_AttachPosition,
	SITV_AttachWidget,
	SITV_AttachOpposite,
	SITV_AttachMiddle,
	SITV_AttachNoOverlap,
#ifdef SIT_P_H
	SITV_AttachFixed     /* Private geometry attachment */
#endif
}	SIT_AttachType;

#define	SITV_AttachPos(percent)      ((ULONG)((percent) * 65536 / 100))
#define	SITV_OffsetCenter            0x3fffffff /* SIT_AttachPosition only */

enum         /* SIT_ButtonType */
{
	SITV_ActionButton,
	SITV_CancelButton,
	SITV_DefaultButton,
	SITV_CheckBox,
	SITV_3StateCB,
	SITV_RadioButton,
	SITV_ToggleButton
};

enum         /* SIT_EditType */
{
//	SITV_Multiline,     // already defined for SIT_Overflow
	SITV_TextBox  = 1,  // default
	SITV_Password = 2,
	SITV_Integer  = 3,
	SITV_Float    = 4,
	SITV_Double   = 5
};

enum         /* SIT_CheckState */
{
	SITV_Unchecked,
	SITV_Checked,
	SITV_Indeterminate
};

enum         /* SIT_ResizePolicy */
{
	SITV_Auto,          /* enlarge/reduce as needed */
	SITV_Fixed,         /* compute once */
	SITV_Optimal        /* (dialog only) get optimal box */
};

enum         /* SIT_ListBoxFlags */
{
	SITV_SelectMultiple = 0x01,
	SITV_SelectAlways   = 0x02,
	SITV_SelectNone     = 0x04,
	SITV_NoHeaders      = 0x08,
	SITV_DoSort         = 0x10,
};

/* SIT_SortColumn */
#define	SITV_SortColumn(column, direction)   (direction < 0 ? -column-2 : column)

/* special value for rowTag parameter of SIT_ListInsertItem() */
#define SITV_CategoryRow          ((APTR)-1)

enum         /* SIT_ViewMode */
{
	SITV_ListViewReport,   /* default value */
	SITV_ListViewIcon,
};

enum         /* SIT_DialogStyles */
{
	SITV_Resizable   = 0x01,
	SITV_NoActivate  = 0x02,
	SITV_Movable     = 0x04,
	SITV_Modal       = 0x08,
	SITV_Plain       = 0x10
};

enum         /* SIT_DlgFlags */
{
	SITV_FileSave          = 0x01,
	SITV_FileMultiSelect   = 0x02,
	SITV_FileAskCreate     = 0x04,
	SITV_FileMustExist     = 0x08,
	SITV_FileWarnOverwrite = 0x10
};

enum         /* SIT_TabStyle (SIT_TAB) */
{
	SITV_TabSingleLine,
	SITV_TabInvisible
};

enum         /* SIT_TabStyle (SIT_EDITBOX) */
{
	SITV_TabEditNormal,      /* default: include tab in multi-line text */
	SITV_TabEditToSpace,     /* convert tab to space */
	SITV_TabEditForbid       /* don't use tab in multi-line text (like single line) */
};

/* use this flag for SIT_TabStyle to use a bitfield instead of a tab number as first parameter for SIT_TabNum() */
#define SITV_TabVisiblityBitField      0x10

enum         /* SIT_ArrowType */
{
	SITV_NoArrows,           /* default style */
	SITV_ArrowsTopBottom,    /* will be left/right if hscroll */
	SITV_ArrowsTop,          /* or left */
	SITV_ArrowsBottom        /* or right */
};

enum         /* SIT_ToolTipAnchor */
{
	SITV_TooltipNearParent,  /* default style */
	SITV_TooltipFollowMouse
};

enum         /* SIT_WordWrap */
{
	SITV_WWNone,
	SITV_WWChar,
	SITV_WWWord
};

enum         /* SIT_MoveNearby() defAlign parameter bitfield */
{
	SITV_AlignTop     = 1,
	SITV_AlignBottom  = 2,
	SITV_AlignVCenter = 3,
	SITV_AlignLeft    = 4,
	SITV_AlignRight   = 8,
	SITV_AlignHCenter = 12
};

/* special value for SIT_DisplayTime */
#define SITV_ResetTime      2000000000

enum /* event type (SIT_AddCallback) */
{	                     // Call data
	SITE_OnClose,        // Bool *
	SITE_OnActivate,     // NULL
	SITE_OnChange,       // STRPTR
	SITE_OnFocus,        // 1
	SITE_OnBlur,         // 0
	SITE_OnFinalize,     // NULL
	SITE_OnClick,        // SIT_OnMouse *
	SITE_OnMouseMove,    // SIT_OnMouse *
	SITE_OnResize,       // int [2]
	SITE_OnPaint,        // GC
	SITE_OnRawKey,       // SIT_OnKey *
	SITE_OnVanillaKey,   // SIT_OnKey *
	SITE_OnScroll,       // ULONG
	SITE_OnMenu,         // ULONG
	SITE_OnSortItem,     // SIT_OnSort *
	SITE_OnSetOrGet,     // SIT_OnVal *
	SITE_OnGeometrySet,  // int [3]
	SITE_OnDropFiles,    // STRPTR * (null-terminated)
	SITE_OnMouseOut,     // NULL
	SITE_LastEvent,
	SITE_OnClickMove     // SIT_OnMouse *  (register cb both for OnClick and OnMouseMove) */
};

struct SIT_OnKey_t
{
	int     keycode;  /* unicode code point [1 - 0x10FFFF] */
	int     flags;    /* SITK_Flag* */
	uint8_t utf8[6];
};

struct SIT_OnMouse_t     /* note: sizeof (struct SIT_OnMouse_t) must be 8 */
{
	enum                 /* unusual construct to get debug symbols */
	{
		SITOM_ButtonLeft,
		SITOM_ButtonMiddle,
		SITOM_ButtonRight,
		SITOM_ButtonWheelUp,
		SITOM_ButtonWheelDown
	}	button:8;
	enum
	{
		SITOM_ButtonPressed,
		SITOM_ButtonReleased,
		SITOM_Move,
		SITOM_CaptureMove
	}	state:8;
	unsigned flags:16;  /* SITK_Flag* : qualifier keys held before mouse event */
	int16_t  x, y;      /* relative to top left corner of control */
};

struct SIT_OnSort_t
{
	APTR item1;    /* rowTag parameter of SIT_ListInsertItem() */
	APTR item2;
	int  column;   /* 0 based */
};

struct SIT_OnPaint_t
{
	float x, y;    /* recommended area to refresh */
	float w, h;
	APTR  nvg;     /* nanovg context */
};

struct SIT_OnVal_t
{
	enum
	{
		SITV_Set,
		SITV_Get,
		SITV_PostProcess
	}         stage;
	int       tag;
	APTR      ptr;    /* access those 2 fields using SIT_GET or SIT_SET */
	va_list * vararg;
};

struct SIT_Accel_t
{
	int          key;
	int          event;
	STRPTR       target;
	SIT_CallProc cb;
};

#define SIT_GET(cd, t)       ((cd)->vararg ? va_arg(*(cd)->vararg, t) : * (t *) (cd)->ptr)
#define	SIT_SET(cd, val, t)  (* (t *)(((SIT_OnVal *)(cd))->ptr) = val)

enum /* possible return codes from SITE_OnSortItem callback */
{
	SIT_SortBefore   = -1,
	SIT_SortEqual    =  0,
	SIT_SortAfter    =  1,
	SIT_SortAlpha    =  2,    /* sort will be done by the library (default if no cb) */
	SIT_SortBinary   =  3,    /* like strcmp() */
	SIT_SortNumeric  =  4,    /* decimal integer sort */
	SIT_SortAlphaNum =  5     /* mix between alpha and numeric */
};

#define	SITV_Em(em)        (((int)(em*8192) & 0x7fffffff) | (1<<31))
#define	SITV_Px(px)        (px & ~(1<<31)) /* needed for negative number */
#define SITV_NoPad         262144
#define SITV_BorderBox     262143

typedef struct KeyVal_t /* SIT_TagList value */
{
	int tag;
	union {
		long   val;
		APTR   ptr;
		double real;
	} key;
} KeyVal_t;

enum /* values for SIT_OnKey_t.flags */
{
	SITK_FlagShift   = 0x0100,
	SITK_FlagCtrl    = 0x0200,
	SITK_FlagAlt     = 0x0400,
	SITK_FlagCmd     = 0x0800,
	SITK_FlagUp      = 0x2000,   /* key is released */
	SITK_FlagCapture = 0x4000
};

#define RAWKEY(x)      (x << 21)

enum /* special values for SIT_OnKey_t.keycode field */
{
	SITK_Home        = RAWKEY(1),
	SITK_End         = RAWKEY(2),
	SITK_PrevPage    = RAWKEY(3),
	SITK_NextPage    = RAWKEY(4),
	SITK_Up          = RAWKEY(5),
	SITK_Down        = RAWKEY(6),
	SITK_Left        = RAWKEY(7),
	SITK_Right       = RAWKEY(8),
	SITK_LShift      = RAWKEY(9),
	SITK_RShift      = RAWKEY(10),
	SITK_LAlt        = RAWKEY(11),
	SITK_RAlt        = RAWKEY(12),
	SITK_LCtrl       = RAWKEY(13),
	SITK_RCtrl       = RAWKEY(14),
	SITK_LCommand    = RAWKEY(15),
	SITK_RCommand    = RAWKEY(16),
	SITK_AppCommand  = RAWKEY(17),
	SITK_Return      = RAWKEY(18),
	SITK_Caps        = RAWKEY(19),
	SITK_Insert      = RAWKEY(20),
	SITK_Delete      = RAWKEY(21),
	SITK_ScrollLock  = RAWKEY(22),
	SITK_NumLock     = RAWKEY(23),
	SITK_Impr        = RAWKEY(24),
	SITK_F1          = RAWKEY(25),
	SITK_F2          = RAWKEY(26),
	SITK_F3          = RAWKEY(27),
	SITK_F4          = RAWKEY(28),
	SITK_F5          = RAWKEY(29),
	SITK_F6          = RAWKEY(30),
	SITK_F7          = RAWKEY(31),
	SITK_F8          = RAWKEY(32),
	SITK_F9          = RAWKEY(33),
	SITK_F10         = RAWKEY(34),
	SITK_F11         = RAWKEY(35),
	SITK_F12         = RAWKEY(36),
	SITK_F13         = RAWKEY(37),
	SITK_F14         = RAWKEY(38),
	SITK_F15         = RAWKEY(39),
	SITK_F16         = RAWKEY(40),
	SITK_F17         = RAWKEY(41),
	SITK_F18         = RAWKEY(42),
	SITK_F19         = RAWKEY(43),
	SITK_F20         = RAWKEY(44),
	SITK_Tab         = RAWKEY(45),
	SITK_BackSpace   = RAWKEY(46),
	SITK_Escape      = RAWKEY(47),
	SITK_Space       = RAWKEY(48),
	SITK_Help        = RAWKEY(49),
	SITK_Shift       = RAWKEY(50),
	SITK_OEM         = 1UL << 31
};

struct LocaleInfo_t        // example:
{
	STRPTR nlsLang;        // Fran�ais
	STRPTR nlsCountry;     // France
	STRPTR engLang;        // French
	STRPTR engCountry;     // France
	STRPTR iso3166;        // FR_fr
	STRPTR currency;       // �
	STRPTR currencyName;   // Euro
	STRPTR currencyCode;   // EUR
	STRPTR shortDate;      // dd/MM/yyyy
	STRPTR longDate;       // dddd d MMMM yyyy
	STRPTR decimalSep;     // ,
	STRPTR months[12];     // janvier, f�vrier, ...
	STRPTR abbrMonths[12]; // janv., f�vr., ...
	STRPTR weekDays[7];    // lundi, mardi, ...
	STRPTR abbrWeek[7];    // lun., mar., mer., ...
	STRPTR langCode;       // 040C
	STRPTR intlPrefix;     // 33
	STRPTR timeStr;        // HH:MM:SS
};

/* special tag modifier to enable printf-like parsing for string parameter */
#define	XfMt            0x8000000

/* default NVG flags for SIT_Init() (no need to include nanovg.h) */
#define SIT_NVG_FLAGS   3

#ifdef __cplusplus
}
#endif

#endif
