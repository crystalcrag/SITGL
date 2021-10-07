/*
 * SIT_P.h : private definition for SIT library used about everywhere.
 *
 * written by T.Pierron, may 2020.
 */

#ifndef SIT_P_H
#define SIT_P_H

#include "SIT.h"
#include "SIT_CSSStyles.h"
#include <stdio.h>

#ifdef DEBUG_MALLOC
#include "debugMem.h"
#else
#define SIT_DebugMalloc(x)
#endif

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wpointer-sign"    /* UTF8 string and libc prototypes... */
#endif

typedef struct TagList_t          TagList;
typedef struct SIT_Widget_t *     SIT_Widget;
typedef struct SIT_TextNode_t *   SIT_TextNode;
typedef struct SIT_Dialog_t *     SIT_Dialog;
typedef struct SIT_App_t *        SIT_App;
typedef struct SIT_Label_t *      SIT_Label;
typedef struct SIT_ListBox_t *    SIT_ListBox;
typedef struct SIT_Button_t *     SIT_Button;
typedef struct SIT_EditBox_t *    SIT_EditBox;
typedef struct SIT_ScrollBar_t *  SIT_ScrollBar;
typedef struct SIT_Slider_t *     SIT_Slider;
typedef struct SIT_Progress_t *   SIT_Progress;
typedef struct SIT_ComboBox_t *   SIT_ComboBox;
typedef struct SIT_Tab_t *        SIT_Tab;
typedef struct SIT_TabItem_t *    SIT_TabItem;
typedef struct SIT_Frame_t *      SIT_Frame;
typedef struct SIT_Tooltip_t *    SIT_Tooltip;
typedef struct SIT_FileDialog_t * SIT_FileDialog;
typedef struct SIT_FolderSel_t *  SIT_FolderSel;
typedef struct SIT_Attach_t       SIT_Attach;
typedef struct SIT_Callback_t *   SIT_Callback;
typedef union  SIT_Variant_t      SIT_Variant;
typedef struct SIT_Widget_t       SIT_Widget_t;
typedef struct SIT_CBRow_t *      SIT_CBRow;
typedef struct CSSImage_t *       CSSImage;
typedef struct SIT_Action_t       SIT_ActBuf;
typedef struct Cell_t *           Cell;
typedef uint16_t *                DATA16;

enum ResizePolicy_t /* values for 'adjust' parameter of SIT_LayoutWidgets() */
{
	KeepDialogSize,
	FitUsingOptimalBox,
	FitUsingCurrentBox,
	FitUsingInitialBox
};

typedef enum ResizePolicy_t   ResizePolicy;

int  CSS_Init(STRPTR theme, int isPath);
Bool SIT_LoadImg(CSSImage, STRPTR path, int len, int flags);
void SIT_UnloadImg(CSSImage);
void SIT_GetImageSize(CSSImage);
Bool SIT_InitApp(SIT_Widget, va_list args);
Bool SIT_InitLabel(SIT_Widget, va_list args);
Bool SIT_InitButton(SIT_Widget, va_list args);
Bool SIT_InitEditBox(SIT_Widget, va_list args);
Bool SIT_InitScrollBar(SIT_Widget, va_list args);
Bool SIT_InitListBox(SIT_Widget, va_list args);
Bool SIT_InitProgress(SIT_Widget, va_list args);
Bool SIT_InitDialog(SIT_Widget, va_list args);
Bool SIT_InitFileDialog(SIT_Widget, va_list args);
Bool SIT_InitDirDialog(SIT_Widget, va_list args);
Bool SIT_InitSlider(SIT_Widget, va_list args);
Bool SIT_InitComboBox(SIT_Widget, va_list args);
Bool SIT_InitTab(SIT_Widget, va_list args);
Bool SIT_InitTooltip(SIT_Widget, va_list args);
APTR SIT_FindControl(SIT_Widget, STRPTR utf8, int len, Bool recursive);
int  SIT_LayoutWidget(SIT_Widget, SIT_Widget w, int side /* 0: horiz, 1:vert */, ResizePolicy adjust);
Bool SIT_LayoutWidgets(SIT_Widget root, ResizePolicy mode);
void SIT_ReflowLayout(SIT_Widget list);
void SIT_ParseTags(SIT_Widget, va_list vargs, TagList * classArgs);
void SIT_AddTitle(SIT_Widget, STRPTR text, int pos);
int  SIT_SetWidgetValue(SIT_Widget, APTR cd, APTR ud);
void SIT_LayoutCSSSize(SIT_Widget);
void SIT_RenderNode(SIT_Widget);
Bool SIT_ReassignAttachments(SIT_Widget);
void SIT_ActionDispatch(double time);
void SIT_InitiateReflow(SIT_Widget);
void SIT_FreeCSS(SIT_Widget);
void SIT_DestroyWidget(SIT_Widget);
void SIT_DestroyChildren(SIT_Widget);
Bool SIT_CreateWidgetsVA(SIT_Widget, STRPTR fmt, va_list args);
void SIT_MoveWidgets(SIT_Widget);
void SIT_AppAllowDnD(void);
void SIT_AppDelDnD(void);
void SIT_AppGetCWD(SIT_Widget);
void SIT_TooltipAuto(SIT_Widget parent, STRPTR fmt);
int  SIT_TooltipTimer(SIT_Widget, APTR cd, APTR ud);
void SIT_CenterDialog(SIT_Widget);
void SIT_MeasureWidget(SIT_Widget);
int  SIT_ListGetItemCount(SIT_Widget);
void SIT_ListGetArg(SIT_Widget, int type, APTR arg);
void SIT_NukeCSS(void);

int  SIT_TextEditKey(SIT_EditBox, int key);
int  SIT_TextEditInsertText(SIT_EditBox, DATA8 utf8);
void SIT_TextEditSetText(SIT_Widget, STRPTR title);

int  CP2UTF8(DATA8 dest, int cp);

Bool renderWords(SIT_Widget, RectF * box, int shadowLayer);
void renderBorder(SIT_Widget, RectF * box, int flag);
void renderTextShadow(SIT_Widget, RectF * box);
void renderInitBox(SIT_Widget, RectF * out, Bool init, int flag);


#define SITV_AssignAction        ((APTR) 1)

struct SIT_Action_t
{
	ListNode     node;
	SIT_Widget   ctrl;
	double       start, end;
	SIT_CallProc cb;
	APTR         ud;
};

/* don't care if it is not defined: we will use it as an opaque pointer anyway */
typedef struct NVGcontext *      NVGCTX;

#define MAXCOMPO                 10

struct SITContext_t
{
	SIT_Widget   root;                 /* root window */
	SIT_Widget   hover;                /* current control mouse is over */
	SIT_Widget   active;               /* clicked */
	SIT_Widget   focus;                /* last clikced on */
	SIT_Widget   geomList;             /* pending changes to be applied */
	SIT_Widget   activeDlg;            /* dialog being active */
	SIT_Widget   curTooltip;           /* tooltip being displayed (only one at a time) */
	SIT_Widget   composited;           /* need to be rendered in another pass */
	SIT_Action   toolTip;              /* when to display, when to hide <curTooltip> */
	SIT_Action   imageCleanup;         /* cache cleanup async action */
	NVGCTX       nvgCtx;               /* main nanovg context for graphics API */
	int          defFontHeight;        /* relative value for em unit */
	int          scrWidth;             /* root window size */
	int          scrHeight;
	int          keyQual;              /* current key qualifier being pressed */
	uint32_t     dblClickMS;           /* double time in ms */
	uint32_t     caretBlinkMS;         /* caret blink in ms */
	uint16_t     mouseX, mouseY;       /* last mouse position */
	uint8_t      relPathSz;
	uint8_t      captureEvt;           /* capture mouse move in progress */
	uint8_t      dirty;                /* need redraw */
	uint8_t      refreshMode;
	uint8_t      errorCode;            /* error at init (INIT_ERR_*) */
	double       nextAction;           /* time in ms to wait before grabbing first item in <actions> field */
	double       curTime;
	STRPTR       cssFile;              /* keep memory we alloc */
	DATA8        theme;                /* CSSRule, CSSSel, STRPTR */
	uint16_t     themeMax;             /* mem allocated in <theme> */
	uint16_t     themeSize;            /* mem used */
	int          lastRule;
	ListHead     images;               /* CSSImage: keep in a cache, free after a while */
	ListHead     actions;              /* SIT_Action */
	ListHead     pendingDel;           /* SIT_Widget */
	TEXT         relPath[128];         /* external resources from CSS will be relative to where CSS file is */
	SIT_ActBuf   actionbuf[10];        /* pre-alloc some, malloc more if needed */
	REAL         compoArea[4*MAXCOMPO];
	uint8_t      compoIds[MAXCOMPO+1];
	int          compoCount;
	uint64_t     QPCfreq;
	uint64_t     QPCstart;
	uint64_t     QPCpause;
	uint64_t     QPCsave;
	double       QPCfreqinv;
	Lang         curLang;
	SIT_CallProc dragCb;
};

extern struct SITContext_t sit;

struct SIT_Attach_t
{
	ULONG          sa_Arg;
	SIT_AttachType sa_Type;
	REAL           sa_Offset;
};

/* base class for all widgets */
struct SIT_Widget_t
{
	ListNode     node;
	SIT_TYPE     type;
	SIT_Widget   parent;
	SIT_Widget   geomChanged;
	SIT_Widget   vscroll;
	SIT_Widget   hscroll;
	SIT_Widget   tooltip;
	SIT_Widget   nextCtrl;
	STRPTR       title, tagName;       /* public */
	STRPTR       name, classes;        /* public */
	STRPTR *     inlineStyles;         /* public */
	ListHead     children;
	ListHead     callbacks;
	int          evtFlags;             /* which evt has been set (quick filter): 1<<SITE_* */
	ListNode     max;                  /* public: SIT_MaxWidth chain */
	SizeF        optimalBox;           /* min size of control, border-box */
	SizeF        currentBox;           /* border-box */
	SizeF        minBox, maxBox;
	SizeF        childBox;             /* border-box */
	RectF        fixed;                /* explicit size requested by user (border-box) */
	struct {                           /* border-box */
		REAL     left, top;
		REAL     right, bottom;
	}	box;
	REAL         padding[4];           /* quoted from layout (padding+border): L, T, R, B */
	REAL         offsetX, offsetY;     /* box.left and box.top are relative to parent, these are not: offset + box = pos on screen */
	TagList *    attrs;
	uint8_t      visible;              /* public */
	uint8_t      oldEna;
	uint8_t      enabled;              /* public */
	uint8_t      state;                /* bit field STATE_* */
	uint8_t      oldState;             /* before changing state, transfer value here */
	uint8_t      cbSlot;
	uint8_t      resizePolicy;         /* public */
	uint8_t      composited;           /* public */
	int          flags;                /* SITF_* bitfield */
	int          tabOrder;             /* public: visibility flags for tab control */
	void *       userData;             /* public: retrieved using SIT_UserData */
	SIT_Attach   attachment[4];        /* public: left, top, right, bottom (margins are here) */
	Style        style;                /* CSS styles */
	Layout       layout;               /* computed styles */
	SIT_CallProc optimalWidth;
	SIT_CallProc setValue;
	SIT_CallProc finalize;
	SIT_CallProc render;
	SIT_CallProc postProcess;
	SIT_CallProc manage;
	SIT_Callback slots;                /* allocated after structure */
};

struct SIT_Callback_t
{
	ListNode     sc_Node;
	SIT_CallProc sc_CB;
	APTR         sc_UserData;
	uint8_t      sc_Event;
	uint8_t      sc_Malloc;
	uint8_t      sc_Priority;
	uint8_t      sc_Padding;
};

struct SIT_Dialog_t
{
	SIT_Widget_t super;
	SIT_Widget   focusRing;
	SIT_Accel *  accel;                /* public */
	SIT_Widget   clientArea;
	SIT_Widget   closeButton;
	SIT_Widget   defButton;
	SIT_Widget   cancelButton;
	SIT_Widget   lastFocus;
	SizeF        minSize;
	SizeF        maxSize;
	int          customStyles;         /* public: SITV_DialogStyles */
	uint8_t      cornerResize;
	uint8_t      cornerHover;
	int16_t      moveOffX;
	int16_t      moveOffY;
};

struct SIT_App_t
{
	SIT_Widget_t super;
	SIT_Widget   focusRing;
	SIT_Accel *  accel;                /* public */
	LocaleInfo   locale;               /* public */
	int          defSBArrows;          /* public */
	REAL         defSBSize;            /* public */
	int          refreshMode;          /* public */
	int *        exitCode;             /* public */
	STRPTR       styles;               /* public */
	STRPTR       currentDir;           /* public */
	STRPTR       fontName;
	STRPTR     * formats;              /* clipboard */
	uint8_t      CWDID[12];
	struct {
		int      width, height;
	}            screen;
};

struct SIT_Label_t
{
	SIT_Widget_t super;
	CSSImage     image;
	int          overflow;             /* public */
	int          labelSize;            /* public */
};

struct SIT_ListBox_t
{
	SIT_Widget_t super;
	SIT_Widget   td, tdSel, thead;
	SIT_CallProc cellPaint;            /* public: SG */
	SIT_CallProc finalizeItem;         /* public: SG */
	int          lbFlags;              /* public: SG */
	int          rowCount;
	int          sortColumn;           /* public: SG */
	int          columnCount;          /* public: G */
	int          viewMode;             /* public: SG */
	int          selIndex;             /* public: SG */
	int          curRow;               /* public: G */
	int          softColumn;
	int          recalcCell;
	int          maxRowVisible;        /* public: SG */
	uint16_t     msgX, msgY;           /* mousemove */
	STRPTR       columnNames;          /* public: S */
	STRPTR       columnWidths;         /* public: S */
	STRPTR       columnAlign;          /* public: S */
	uint8_t      catCount;
	uint8_t      catVisible;
	int8_t       defAlign;
	int8_t       autoScrollDir;
	ListHead     strPool;              /* StrPool */
	Cell         columns;
	Cell         rowTop;
	vector_t     cells;
	REAL *       realWidths;
	ULONG        borderCSSV,     borderCSSH;
	REAL         borderSpacingV, borderSpacingH;
	REAL         scrollTop, scrollPad, scrollHeight;
	REAL         formatTitle;
	REAL         lassoSX, lassoSY;
	REAL         lassoEX, lassoEY;
	REAL         maxCellW;             /* SITV_ListViewIcon */
	REAL         hdrHeight;            /* SITV_ListViewReport */
};

struct SIT_Button_t
{
	SIT_Widget_t super;
	int          type;                 /* public: SIT_ButtonType */
	int          state;                /* public: SIT_CheckState */
	int          group;                /* public: SIT_RadioGroup */
	int *        curValue;             /* public */
	int          radioID;              /* public: SIT_RadioID */
};

typedef struct DOMRow_t     DOMRow_t;
typedef struct DOMRow_t *   DOMRow;
struct DOMRow_t
{
	uint16_t bytes;
	uint16_t px;
};

struct SIT_EditBox_t
{
	SIT_Widget_t super;
	SIT_Widget   spinnerUp;
	SIT_Widget   spinnerDown;
	int          editType;             /* public */
	double       minValue;             /* public */
	double       maxValue;             /* public */
	double       stepValue;            /* public */
	int          hotKey;               /* public */
	APTR         curValue;             /* public */
	STRPTR       cueBanner;            /* public: placeholder text if empty */
	STRPTR       fixedBuffer;          /* public */
	int          fixedSize;            /* public */
	int          maxLines;             /* public */
	int          tabStyle;             /* public */
	int          roundTo;              /* public */
	CSSColor     caret;
	CSSColor     bgSel;
	int16_t      msgX, msgY;           /* last clicked pos */

	int          cursor;               /* public: character position of the text cursor within the string */
	int          selStart;             /* public: selection start/end point */
	int          selEnd;
	int          selInit;              /* for word selection */
	DATA8        text;                 /* main text buffer (NUL-terminated, utf-8 encoded) */
	int          maxText;
	int          length;               /* length (not including NUL byte) */
	DOMRow       rows;                 /* number of character in row */
	int          rowCount, rowTop;
	int          rowMax,   charTop;
	int          rowVisible;
	REAL         width, scrollPad;
	REAL         formatWidth;
	REAL         tabSize;              /* style.tabSize is in character, we want pixels */
	REAL         fh;                   /* font info */
	REAL         xpos;                 /* current cursor pos on screen */
	int          ypos;                 /* current line */
	REAL         offsetAlign;          /* right/center align */
	REAL         scrollX;              /* horizontal "scroll" */
	REAL         padLineY;
	REAL         extendT, extendB;     /* single edit: extend selection on top and bottom */
	union {
		struct {
			double ref;                /* used by numeric text field */
			int    step;               /* real value = ref + step * stepValue */
		} value;
		struct {
			int  total, length;        /* justified text */
			int  count;
		} space;
	};
	uint8_t      readOnly;             /* public */
	uint8_t      init;
	uint8_t      selectWord;           /* select text by word if >0 */
	uint8_t      wordWrap;             /* public: 0: none, 1: break at character, 2: break at word */
	uint8_t      flags;                /* FLAG_* bitfield */
	uint8_t      hasPreferredX;
	uint8_t      caretVisible;
	int8_t       autoScrollDir;
	SIT_Action   autoScroll;
	SIT_Action   caretBlink;
	REAL         preferredX;
	DATA8        undoLast;             /* undo/redo state */
	DATA8        redoLast;
	DATA8        undoBuffer;
	int          lenInsert;
	uint16_t     redoCount;
	uint16_t     undoCount;
	int          undoSize;
	DATA8        undoFixed;
};

struct SIT_ScrollBar_t
{
	SIT_Widget_t super;
	SIT_Widget   arrowUp, arrowDown, thumb;
	SIT_Action   autoScroll;
	uint8_t      isHoriz;              /* public */
	uint8_t      checkPos;
	uint16_t     isDragged;
	int          arrowType;            /* public */
	int          min, max;             /* public */
	int          pageSize;             /* public */
	int          lineHeight;           /* public */
	int          scrollPos;            /* public */
	int          wheelMult;            /* public */
	REAL         oldRange;
};

struct SIT_Progress_t
{
	SIT_Widget_t super;
	SIT_Widget   bar;
	int          min;                  /* public */
	int          max;                  /* public */
	int          progressPos;          /* public */
	uint8_t      isHoriz;              /* public */
	int          posCache;
	STRPTR       origTitle;
	TEXT         textCache[64];
};

struct SIT_Slider_t
{
	SIT_Widget_t super;
	SIT_Widget   gauge;
	SIT_Widget   thumb;
	int          minValue;             /* public */
	int          maxValue;             /* public */
	int          pageSize;             /* public */
	int          sliderPos;            /* public */
	REAL         thumbThick;           /* public */
	REAL         thumbHeight;          /* public */
	REAL         gaugePadding;         /* public */
	uint8_t      isDragged;
	uint8_t      isHoriz;              /* public */
	SIT_Widget   buddy;
};

struct SIT_CBRow_t
{
	STRPTR       entry;
	APTR         tag;
//	uint8_t      flags;
};

struct SIT_ComboBox_t
{
	SIT_Widget_t super;
	SIT_Widget   edit;
	SIT_Widget   popup;
	SIT_Widget   arrow;
	SIT_CBRow    rowTags;
	SIT_Action   anim;
	int          animTime;             /* public */
	int          animStop;
	STRPTR       values;               /* text entry mem */
	STRPTR       initValues;           /* public */
	int          autoComplete;         /* public */
	int          items;                /* public */
	int          count;
	int          selIndex;             /* public */
	int          oldSel;
	uint8_t      readOnly;             /* public */
	int8_t       animDir;
	TEXT         searchTxt[31];
	uint8_t      searchLen;
	struct {
		REAL     left, top;            /* relative to cb entry */
		REAL     hup, hdown;
		REAL     width, lineh;
	} popupRect;
};

struct SIT_TabItem_t
{
	SIT_Widget   label;
	APTR         userData;
	SizeF        clientArea;
};

struct SIT_Tab_t
{
	SIT_Widget_t super;
	SIT_TabItem  items;
	STRPTR       tabStr;               /* public */
	REAL         tabSpace;             /* public */
	int          tabStyle;             /* public */
	int          nbTab;                /* public */
	int          curTab;               /* public */
	int          visiBitField;
	REAL         maxHeight;
	REAL         padTop;
};

struct SIT_Frame_t
{
	SIT_Widget_t super;
	SizeF        title;
	REAL         padLeft;
};

struct SIT_Tooltip_t
{
	SIT_Widget_t super;
	SIT_Widget   relTo;
	uint8_t      autoTip;
	int          anchor;
	int          displayTime;
	int          delayTime;
};

#define TOOLTIP_DEFALIGN              ((SITV_AlignBottom | SITV_AlignRight) | ((SITV_AlignBottom | SITV_AlignLeft) << 8))

struct SIT_FileDialog_t
{
	SIT_Widget_t super;
	STRPTR       filters;
	int          selFilter;
	STRPTR       initPath;
	int          flags;
	STRPTR *     saveState;
	int          nbSelect;
	STRPTR       filePtr;
	#ifdef PLATFORM_H
	WCHAR        fileName[512];
	OPENFILENAME ofn;
	#endif
};

struct SIT_FolderSel_t
{
	SIT_Widget_t super;
	STRPTR       initPath;
	#ifdef PLATFORM_H
	WCHAR        path[MAX_PATH];
	TEXT         info[128];
	#endif
};

union SIT_Variant_t
{
	int      integer;
	uint16_t word;
	double   real;
	void *   pointer;
	Bool     boolean;
	STRPTR   string;
};

/* Private tags */
#define SIT_SuperClass      0x1000

enum SIT_Type_t /* types for ParseTags */
{
	SIT_INT,
	SIT_U16,
	SIT_BOOL,
	SIT_STR,
	SIT_REAL,
	SIT_PTR,
	SIT_CTRL,
	SIT_UNIT,
	SIT_ABBR
};

typedef enum SIT_Type_t       SIT_Type;

struct TagList_t
{
	int      tl_TagID;
	STRPTR   tl_TagName;
	int      tl_Flags;
	SIT_Type tl_Type;
	ULONG    tl_Arg;
};

extern TagList WidgetClass[];

enum /* bitfield for tl_Flags */
{
	___ = 0x0,    /* internal usage */
	__G = 0x1,    /* read only */
	_S_ = 0x2,    /* write only */
	_SG = 0x3,    /* no side effect property */
	C__ = 0x4,    /* create only */
	CSG = 0x7,    /* setting property will imply visual changes (geometry) */
};

#define ABBRBASE                      64
#define	ABBR(a, b, c, d)              ((a ? a+ABBRBASE : 0) | ((b ? b+ABBRBASE : 0) << 8) | ((c ? c+ABBRBASE : 0) << 16) | ((d ? d+ABBRBASE : 0) << 24))
#define	OFFSET(ptrType, fieldName)    ((ULONG) &((ptrType)0L)->fieldName)

/* check if an event is defined for given control */
#define	HAS_EVT(w, type)           ((w)->evtFlags & (1 << (type)))
#define HAS_1OF2EVT(w, e1, e2)     ((w)->evtFlags & ((1 << (e1)) | (1 << (e2))))
#define	SET_EVT(type)              (1 << type)
#define	HAS_TAB(w)                 (w->flags & SITF_InFocusRing)
#define FOCUSRING(w)               ((SIT_Dialog)w)->focusRing
#define NoFocusRing                ((APTR)1)

enum /* bitfield for flags */
{
	SITF_GeometrySet     = 0x00000001,
	SITF_GeometryChanged = 0x00000002,
	SITF_FixedX          = 0x00000004, /* inform layout manager */
	SITF_FixedY          = 0x00000008,
	SITF_FixedWidth      = 0x00000010,
	SITF_FixedHeight     = 0x00000020,
	SITF_IsLocked        = 0x00000040, /* an event has a pending process */
	SITF_BeingDestroyed  = 0x00000080, /* user ask to delete that control */
	SITF_CanResizeW      = 0x00000100, /* at least one control has a right attach on the form */
	SITF_CanResizeH      = 0x00000200, /* a control has a bottom attach on the form */
	SITF_KeepDiagSize    = 0x00000400, /* width/height changed on dialog - keep it */
	SITF_TopLevel        = 0x00000800, /* top level window, not part of children layout */
	SITF_GeomNotified    = 0x00001000, /* geometry modification message has been posted */
	SITF_AutoHeight      = 0x00002000, /* need to set control's size before triggering OnResize */
	SITF_RestoreSize     = 0x00004000,
	SITF_ReflowW         = 0x00008000,
	SITF_ReflowH         = 0x00010000,
	SITF_PrivateChildren = 0x00020000, /* children are managed by widget, not by geometry module */
	SITF_NoResetSize     = 0x00040000, /* for handling SITE_OnGemetrySet */
	SITF_RenderChildren  = 0x00080000,
	SITF_Style1Changed   = 0x00100000,
	SITF_Style2Changed   = 0x00200000,
	SITF_StylesChanged   = 0x00300000,
	SITF_InitDone        = 0x00800000, /* set at the end of SIT_CreateWidget() */
	SITF_ToggleButon     = 0x01000000, /* toggle button: ignore mouse button release event */
	SITF_ImmediateActive = 0x02000000, /* onclick = activate event */
	SITF_InFocusRing     = 0x10000000, /* will act as a tab stop */
	SITF_RecalcStyles    = 0x20000000,
	SITF_FallthroughEvt  = 0x40000000, /* mouse events are sent to widgets below this one */
	SITF_HasAccel        = 0x80000000
};


enum /* bitfield for 'layout.flags' */
{
	LAYF_IgnoreWords     = 0x0000001,  /* don't render node->title or node->layout.wordwrap */
	LAYF_NodeAdded       = 0x0000002,  /* added in rendering */
	LAYF_SizeChanged     = 0x0000004,  /* reflow necessary for this node and its children */
	LAYF_NoBorders       = 0x0000008,  /* skip border rendering without having to check the whole layout.border[] array */
	LAYF_NoRoundBorder   = 0x0000010,  /* no boder-radius */
	LAYF_NoOutline       = 0x0000020,  /* no outline - no need to call border rendering */
	LAYF_HasBoxShadow    = 0x0000040,
	LAYF_HasInset        = 0x0000080,
	LAYF_HasLeftAttach   = 0x0000100,
	LAYF_HasTopAttach    = 0x0000200,
	LAYF_HasRightAttach  = 0x0000400,
	LAYF_HasBottomAttach = 0x0000800,
	LAYF_HasAttach       = 0x0000f00,
	LAYF_NoTextShadow    = 0x0001000,
	LAYF_NoChanges       = 0x0002000,  /* private children have no changes when hovering */
	LAYF_HasImg          = 0x0004000,
	LAYF_CompactText     = 0x0008000,  /* will ignore descending part of font metric */
	LAYF_BImgLoaded      = 0x0010000,
	LAYF_AdjustHitRect   = 0x0020000,  /* use optimalWidth() callback to adjust hit box */
	LAYF_AdjustRect      = 0x0040000,  /* adjust render rect in renderNode() via optimalWidth() cb */
	LAYF_RenderWordBg    = 0x0080000,
};

enum /* bitfield for style.flags */
{
	CSSF_APPLIED     = 0x0001,       /* cssApply() has been called */
	CSSF_ANON        = 0x0002,       /* anonymous elements (no effect for css selector) */
	CSSF_INLINE      = 0x0004,       /* HTML "inline" elements (set after processing CSS rules) */
	CSSF_OBJECT      = 0x0008,       /* content of node requires special processing */
	CSSF_CLOSEP      = 0x0010,       /* this starting node must auto-close any opened <p> tag */
	CSSF_OPT         = 0x0020,       /* elements with an optional end tag */
	CSSF_IMG         = 0x0040,       /* need to load external ressources */
	CSSF_LINK        = 0x0080,       /* set on anchor and its descendant (used in render.c) */
	CSSF_BORDERIMG   = 0x0100,       /* cache for border-image is done */
	CSSF_TEXTSHADOW  = 0x0200,       /* style.shadow struct need recompute */
	CSSF_BOXSHADOW   = 0x0400        /* style.boxShadow need recompute */
};

enum /* bitfield for state */
{
	STATE_HOVER    = 1,
	STATE_ACTIVE   = 2,
	STATE_FOCUS    = 4,
	STATE_CHECKED  = 8,
	STATE_KBDFOCUS = 16,
	STATE_DISABLED = 32   /* only for oldState */
};

#define STATE_ACTIVATED    (STATE_ACTIVE|STATE_HOVER)

#define AdjustRenderRect    ((APTR)10)
#define EVT_PRIORITY(p)     ((p) << 8)

#define allocaUTF8ToUTF16(utf8, utf16) \
{ \
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0); \
\
	utf16 = alloca(len * sizeof *utf16); \
\
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (LPWSTR) utf16, len); \
}

enum /* error code from initialization */
{
	INIT_ERR_DLL = 1,
	INIT_ERR_OPENGL,
	INIT_ERR_NVG,
	INIT_ERR_THEME
};

#endif
