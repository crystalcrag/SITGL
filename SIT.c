/*
 * SIT.c : Simple Interface Toolkit - simple C framework for building GUI
 *         applications on top of nanovg, inspired by X Intrinsics API.
 *
 * Written by Thierry Pierron, licensed under BSD terms (a.k.a 2-clause
 * BSD license or FreeBSD license).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define UNICODE
#include <windows.h>
#include <math.h>
#include <malloc.h>
#include <time.h>
#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "glad.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
#include "SIT_P.h"
#include "SIT_CSSParser.h"
#include "SIT_CSSLayout.h"


struct SITContext_t sit;

DLLIMP SIT_Widget SIT_Init(int nvgFlags, int width, int height, STRPTR theme, int _1_if_theme_is_path_0_if_string)
{
	if (! gladLoadGL())
		return NULL;

	/* not the best PRNG, but good enough to randomize graphics */
	srand(time(NULL));

	/* will be needed for high frequency timer */
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	QueryPerformanceFrequency((PLARGE_INTEGER) &sit.QPCfreq);
	sit.QPCfreqinv = 1000 / (double) sit.QPCfreq;
	sit.QPCstart = start.QuadPart;

	sit.nvgCtx = nvgCreateGL3(nvgFlags);
	sit.lastRule = -1;
	sit.scrWidth = width;
	sit.scrHeight = height;
	sit.nextAction = INFINITY;
	sit.dblClickMS = GetDoubleClickTime();
	sit.caretBlinkMS = GetCaretBlinkTime();

	if (! sit.nvgCtx)
	{
		sit.errorCode = INIT_ERR_NVG;
		return NULL;
	}

	/* default font size for em unit */
	TEXTMETRIC metric;
	HDC hdc = GetDC(NULL);
	HFONT old = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));

	GetTextMetrics(hdc, &metric);

	SelectObject(hdc, old);
	ReleaseDC(NULL, hdc);

	sit.defFontHeight = metric.tmHeight;

	if (IsDef(theme) && ! CSS_Init(theme, _1_if_theme_is_path_0_if_string))
	{
		/* nothing will work without this */
		sit.cssFile = theme;
		sit.errorCode = INIT_ERR_THEME;
		return NULL;
	}

	sit.root = sit.focus = sit.activeDlg = SIT_CreateWidget("html", SIT_APP, NULL, NULL);
	/* initially hidden to prevent useless reflow as hierarchy is built */
	sit.root->visible = 2;
	return sit.root;
}

/* if something fail, better report something meaningful to the user */
DLLIMP STRPTR SIT_GetError(void)
{
	switch (sit.errorCode) {
	case INIT_ERR_DLL:
		snprintf(sit.relPath, sizeof sit.relPath, "%s cannot be loaded", sit.cssFile);
		break;
	case INIT_ERR_OPENGL:
		snprintf(sit.relPath, sizeof sit.relPath, "need opengl 3+, missing function %s", sit.cssFile);
		break;
	case INIT_ERR_NVG:
		snprintf(sit.relPath, sizeof sit.relPath, "nanovg context creation failed");
		break;
	case INIT_ERR_THEME:
		snprintf(sit.relPath, sizeof sit.relPath, "cannot locate file %s", sit.cssFile);
		break;
	default:
		sit.relPath[0] = 0;
	}
	return sit.relPath;
}

void SIT_ClearGL(void)
{
	glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
}

Bool SIT_LoadImg(CSSImage img, STRPTR path, int len, int flags)
{
	int himg;
	if (flags) flags = NVG_IMAGE_NEAREST;
	flags |= NVG_IMAGE_REPEATX|NVG_IMAGE_REPEATY;
	if (img->bpp == 8) flags |= NVG_IMAGE_MASK;
	if (path)
	{
		if (len == 0)
		{
			if (sit.relPath[0])
			{
				AddPart(sit.relPath, path, sizeof sit.relPath);
				himg = nvgCreateImage(sit.nvgCtx, sit.relPath, flags);
				sit.relPath[sit.relPathSz] = 0;
			}
			else himg = nvgCreateImage(sit.nvgCtx, path, flags);
		}
		else /* "data:" URI */
			himg = nvgCreateImageMem(sit.nvgCtx, flags|NVG_IMAGE_GENERATE_MIPMAPS, (DATA8) path, len);
		if (! himg) return False;
		nvgImageSize(sit.nvgCtx, himg, &img->width, &img->height);
	}
	else /* gradients */
	{
		/* gradients are generated at their exact dimension: no need for mipmaps */
		himg = nvgCreateImageRGBA(sit.nvgCtx, img->width, img->height, flags, img->bitmap);
		if (! himg) return False;
	}
	img->handle = himg;
	return True;
}

void SIT_GetImageSize(CSSImage img)
{
	nvgImageSize(sit.nvgCtx, img->handle, &img->width, &img->height);
}

static int SIT_FreeImg(SIT_Widget w, APTR cd, APTR ud)
{
	CSSImage img, next;
	for (img = next = HEAD(sit.images); img; img = next)
	{
		NEXT(next);
		if (img->usage > 0) continue;
		if (img->externAlloc == 0) nvgDeleteImage(sit.nvgCtx, img->handle);
		ListRemove(&sit.images, &img->node);
		free(img);
	}
	sit.imageCleanup = NULL;
	return 0;
}

void SIT_UnloadImg(CSSImage img)
{
	img->usage --;
	if (img->usage == 0 && ! sit.imageCleanup)
		/* don't delete image: might be needed a short time after */
		sit.imageCleanup = SIT_ActionAdd(NULL, sit.curTime + 30000, -1, SIT_FreeImg, NULL);
}

/* retrieve the value of given HTML attribute */
DLLIMP STRPTR SIT_GetHTMLAttr(SIT_Widget node, STRPTR key)
{
	if (node == NULL)
		return NULL;
	switch (FindInList("class,id,name", key, 0)) {
	case 0: return node->classes;
	case 1: return node->name;
	case 2: return node->tagName;
	default:
		if (node->type == SIT_HTMLTAG && node->userData)
		{
			STRPTR * list;
			if (*key == 1) key ++;
			for (list = node->userData; list[0]; list += 2)
				if (strcasecmp(list[0], key) == 0) return list[1];
		}
	}
	return NULL;
}

/* move widget near area defined by <XYWH>: typically used by tooltip to follow mouse without leaving the screen */
DLLIMP void SIT_MoveNearby(SIT_Widget ctrl, int XYWH[4], int defAlign)
{
	int x, y, w, h, X2, Y2, try;

	if (ctrl == NULL) return;

	XYWH[0] -= XYWH[2] >> 1;
	XYWH[1] -= XYWH[3] >> 1;
	x = y = 0;
	w = ctrl->box.right  - ctrl->box.left;
	h = ctrl->box.bottom - ctrl->box.top;
	X2 = XYWH[0] + XYWH[2];
	Y2 = XYWH[1] + XYWH[3];

	/* try to place widget at prefered location */
	for (try = 0; try < 2; try ++, defAlign >>= 8)
	{
		switch (defAlign&12) {
		case SITV_AlignLeft:    x = XYWH[0] - ctrl->attachment[2].sa_Offset - w; break;
		case SITV_AlignRight:   x = X2      + ctrl->attachment[0].sa_Offset; break;
		case SITV_AlignHCenter: x = XYWH[0] - ((w - XYWH[2]) >> 1); break;
		default:                x = XYWH[0] + ctrl->attachment[2].sa_Offset;
		}

		switch (defAlign&3) {
		case SITV_AlignTop:     y = XYWH[1] - ctrl->attachment[3].sa_Offset - h; break;
		case SITV_AlignBottom:  y = Y2      + ctrl->attachment[1].sa_Offset; break;
		case SITV_AlignVCenter: y = XYWH[1] - ((h - XYWH[3]) >> 1); break;
		default:                y = XYWH[1] + ctrl->attachment[3].sa_Offset;
		}
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		if (x + w > sit.scrWidth)  x = sit.scrWidth  - w;
		if (y + h > sit.scrHeight) y = sit.scrHeight - h;

		int old = defAlign;
		if (x+w > XYWH[0] && x < X2 && y+h > XYWH[1] && y < Y2)
		{
			/* overlap */
			if ((defAlign & 3) == SITV_AlignBottom)
			{
				defAlign &= ~3;
				defAlign |= y == 0 ? SITV_AlignBottom : SITV_AlignTop;
			} else {
				defAlign &= ~12;
				defAlign |= x == 0 ? SITV_AlignRight : SITV_AlignLeft;
			}
		}
		if (old == defAlign) break;
	}

	x -= ctrl->offsetX;
	y -= ctrl->offsetY;

//	fprintf(stderr, "setting ctrl to %d, %d - [%d, %d, %d, %d]\n", x, y, XYWH[0], XYWH[1], XYWH[2], XYWH[3]);

	SIT_SetValues(ctrl, SIT_X, x, SIT_Y, y, NULL);
}

static int SIT_MessageBox(STRPTR text, STRPTR title, int flags)
{
	extern HANDLE mainWnd;
	LPWSTR text16, title16;

	UTF8ToUTF16(text, text16);
	UTF8ToUTF16(title, title16);

	return MessageBox(mainWnd, text16, title16, flags);
}

/* report an error and quit */
DLLIMP void SIT_Log(int level, STRPTR fmt, ...)
{
	static int flags[] = {
		MB_OK | MB_ICONERROR,
		MB_OK | MB_ICONERROR,
		MB_OK | MB_ICONWARNING,
		MB_OK | MB_ICONINFORMATION
	};
	TEXT    buffer[256];
	va_list args;

	va_start(args, fmt);
	switch (level) {
	case SIT_CRITICAL:
	case SIT_ERROR:
	case SIT_WARN:
	case SIT_INFO:
		vsprintf(buffer, fmt, args);
		SIT_MessageBox(buffer, "Error", flags[level-SIT_CRITICAL]);
		break;
	}
	va_end(args);

	if (level == SIT_CRITICAL)
		exit(1);
}

/* simple function to copy block of UTF-8 text to clipboard */
DLLIMP Bool SIT_CopyToClipboard(STRPTR text, int size)
{
	LPWSTR mem;
	if (size == 0) return False;
	int len = MultiByteToWideChar(CP_UTF8, 0, text, size, NULL, 0);
	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, (len+1) * sizeof *mem);

	if (hmem)
	{
		mem = GlobalLock(hmem);

		MultiByteToWideChar(CP_UTF8, 0, text, size, mem, len);
		if (size > 0) mem[len] = 0;

		GlobalUnlock(hmem);

		if (OpenClipboard(NULL))
		{
			EmptyClipboard();
			SetClipboardData(CF_UNICODETEXT, hmem);
			CloseClipboard();
			return True;
		}
		else GlobalFree(hmem);
	}
	return False;
}

/* check if clipboard contains text */
DLLIMP STRPTR SIT_GetFromClipboard(int * size)
{
	STRPTR ret = NULL;

	if (OpenClipboard(NULL))
	{
		GLOBALHANDLE hmem = GetClipboardData(CF_UNICODETEXT);

		if (hmem)
		{
			APTR mem = GlobalLock(hmem);
			int  len = GlobalSize(hmem);

			len = WideCharToMultiByte(CP_UTF8, 0, mem, -1, NULL, 0, NULL, NULL);
			ret = malloc(len);
			if (size) *size = len;
			if (ret)
				WideCharToMultiByte(CP_UTF8, 0, mem, -1, ret, len, NULL, NULL);
			GlobalUnlock(hmem);
		}
		CloseClipboard();
	}
	return ret;
}

/* find a control using breadth-first iterative search */
APTR SIT_FindControl(SIT_Widget parent, STRPTR utf8, int len, Bool recursive)
{
	SIT_Widget root, c;

	if (parent == NULL)
		return NULL;
	if (utf8[0] == '/')
	{
		/* starts at root of hieraarchy */
		while ((parent->flags & SITF_TopLevel) == 0)
			parent = parent->parent;
		utf8 ++;
	}
	else while (strncmp(utf8, "../", 3) == 0 && parent->parent)
		parent = parent->parent, utf8 += 3, len -= 3;
	if (len <= 1) return parent;

	root = parent;

	if (utf8[len-1])
	{
		/* cannot modify string */
		STRPTR name = alloca(len);
		CopyString(name, utf8, len);
		utf8 = name;
	}

	if (strcmp(parent->name, utf8) == 0)
		return parent;

	for (;;)
	{
		for (c = HEAD(parent->children); c && strcmp(c->name, utf8);
			NEXT(c));

		if (c == NULL)
		{
			if (! recursive) return NULL;

			for (c = HEAD(parent->children); ; c = (APTR) parent->node.ln_Next, parent = parent->parent)
			{
				for (; c && (c->children.lh_Head == NULL || (c->flags & SITF_TopLevel)); NEXT(c));
				if (c) { parent = c; break; }
				if (root == parent) return NULL;
			}
		}
		else return c;
	}
}

DLLIMP SIT_Widget SIT_GetById(SIT_Widget parent, STRPTR utf8name)
{
	return SIT_FindControl(parent, utf8name, strlen(utf8name) + 1, True);
}

DLLIMP void SIT_SetFocus(SIT_Widget w)
{
	SIT_Widget old = sit.focus;
	if (old && w != old)
	{
		old->oldState = old->state;
		old->state &= ~(STATE_FOCUS|STATE_KBDFOCUS);
		sit.focus = NULL;
		layoutUpdateStyles(old);
		if (HAS_EVT(old, SITE_OnBlur) || HAS_EVT(sit.root, SITE_OnBlur))
			SIT_ApplyCallback(old, NULL, SITE_OnBlur);
	}
	if (w)
	{
		sit.focus = w;
		sit.dirty = 1;
		w->oldState = w->state;
		w->state = STATE_FOCUS|STATE_KBDFOCUS;
		layoutUpdateStyles(w);
		if (HAS_EVT(w, SITE_OnFocus) || HAS_EVT(sit.root, SITE_OnFocus))
			SIT_ApplyCallback(w, (APTR) 1, SITE_OnFocus);
	}
}

DLLIMP void SIT_ForceRefresh(void)
{
	sit.dirty = 1;
}

DLLIMP int SIT_NeedRefresh(void)
{
	return sit.dirty;
}

DLLIMP SIT_Widget SIT_GetFocus(void)
{
	return sit.focus;
}

/* we are not in control from the event loop, but we can signal when to exit from it */
DLLIMP void SIT_Exit(int exit)
{
	int * code = ((SIT_App)sit.root)->exitCode;

	if (code) *code = exit;
}

/* this function allow applications using SITGL to use a different C runtime */
DLLIMP void SIT_Free(APTR pointer)
{
	free(pointer);
}

DLLIMP int SIT_InitDrag(SIT_CallProc cb)
{
	if (cb && sit.curTooltip)
		SIT_SetValues(sit.curTooltip, SIT_Visible, False, NULL);
	sit.dragCb = cb;
	return sit.mouseX | (sit.mouseY << 16);
}

/*
 * big cleanup
 */
DLLIMP void SIT_Nuke(int what)
{
	SIT_Action act;
	CSSImage   img;

	if (sit.focus) SIT_SetFocus(NULL);
	SIT_DestroyChildren(sit.root);
	sit.hover = sit.active = sit.focus = sit.composited = sit.geomList = sit.curTooltip = NULL;
	sit.activeDlg = sit.root;
	sit.toolTip = NULL;

	while ((act = HEAD(sit.actions)))
		SIT_ActionReschedule(act, -1, -1);

	while (sit.pendingDel.lh_Head)
	{
		SIT_Widget w = (SIT_Widget) ListRemHead(&sit.pendingDel);
		w->flags &= ~ SITF_IsLocked;
		SIT_DestroyWidget(w);
	}

	switch (what) {
	case SITV_NukeCtrl:
		/* remove inline styles */
		if (sit.root->styles[0] > 0)
		{
			cssClear(sit.root);
			cssApply(sit.root);
			layoutCalcBox(sit.root);
		}
		FOCUSRING(sit.root) = NULL;
		sit.root->tooltip = NULL;
		sit.themeSize = sit.themeLast;
		sit.dirty = True;
		break;
	case SITV_NukeAll:
		SIT_DestroyWidget(sit.root);
		cssFreeGlobals();
		sit.activeDlg = sit.root = NULL;
		// no break;
	case SITV_NukeTheme:
		if (sit.imageCleanup) SIT_ActionReschedule(sit.imageCleanup, -1, -1);
		for (img = HEAD(sit.images); img; img->usage = 0, NEXT(img));
		SIT_FreeImg(NULL, NULL, NULL);
		free(sit.cssFile); sit.cssFile = NULL;
		free(sit.theme);   sit.theme   = NULL;
		sit.themeMax = 0;
		sit.lastRule = -1;
		SIT_DebugMalloc(1);
	}
}

/* utility function */
DLLIMP Bool SIT_ParseCSSColor(STRPTR cssColor, uint8_t ret[4])
{
	return cssParseColor(&cssColor, (CSSColor *) ret);
}

#if 0
static int SIT_CalcFade(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Action act = cd;
	int alpha = roundf((sit.curTime - act->start) * 255 / (act->end - act->start));
	if (alpha > 255) alpha = 255;
	sit.fadeToRGBA[3] = alpha;
	return 0;
}

/* very common feature */
DLLIMP void SIT_InitFade(DATA8 rgb, float time_in_ms)
{
	if (sit.fade) return;
	memcpy(sit.fadeToRGBA, rgb, 3);
	sit.fadeToRGBA[3] = 0;
	sit.fade = 1;
	SIT_ActionAdd(NULL, sit.curTime, sit.curTime + time_in_ms, SIT_CalcFade, SITV_AssignAction);
}
#endif

/*
 * async actions
 */
APTR SIT_ActionAdd(SIT_Widget w, float start, float end, SIT_CallProc proc, APTR ud)
{
	SIT_Action act, prev;
	SIT_Action slot = NULL;

	/* check for a free slot */
	for (act = sit.actionbuf; act < EOT(sit.actionbuf); act ++)
		if (act->cb == NULL) { slot = act; break; }

	if (slot == NULL)
		slot = malloc(sizeof *slot);

	slot->start = start;
	slot->end   = end < 0 ? start : end;
	slot->cb    = proc;
	slot->ud    = ud == SITV_AssignAction ? slot : ud;
	slot->ctrl  = w;

	for (act = HEAD(sit.actions), prev = NULL; act && start > act->start; prev = act, NEXT(act));

	ListInsert(&sit.actions, &slot->node, &prev->node);
	if (prev == NULL)
		sit.nextAction = start;

	//fprintf(stderr, "action add [%d]: %d - %d: %p\n", slot-sit.actionbuf, (int) start, (int) end, proc);

	return slot;
}

void SIT_ActionReschedule(SIT_Action act, float start, float end)
{
	SIT_Action next = (SIT_Action) act->node.ln_Next;
	if (start < act->start)
	{
		/* remove action */
		ListRemove(&sit.actions, &act->node);
		//fprintf(stderr, "action rem [%d]: %g < %g: %p\n", act-sit.actionbuf, start, act->start, act->cb);

		/* action finished */
		if (sit.actionbuf <= act && act < EOT(sit.actionbuf))
			act->cb = NULL;
		else
			free(act);
		if (act->node.ln_Prev == NULL)
			sit.nextAction = next ? next->start : INFINITY;
	}
	else
	{
		//fprintf(stderr, "action moved [%d]: %g < %g: %p\n", act-sit.actionbuf, start, act->start, act->cb);
		act->start = start;
		act->end   = end;
		if (next && act->start > next->start)
		{
			/* need to keep them sorted by ascending start time */
			SIT_Action ins, prev;
			ListRemove(&sit.actions, &act->node);
			for (ins = prev = next, NEXT(ins); ins && ins->start < act->start; prev = ins, NEXT(ins));
			ListInsert(&sit.actions, &act->node, &prev->node);
		}
		act = HEAD(sit.actions);
		sit.nextAction = act->start;
	}
}

/* simply dispatch to registered callbacks */
void SIT_ActionDispatch(float time)
{
	SIT_Action act, next;
	for (act = next = HEAD(sit.actions); act && act->start < time; act = next)
	{
		SIT_CallProc proc = act->cb;
		NEXT(next);
		int extend = proc(act->ctrl, (APTR) (act->end < time), act->ud);
		if (extend > 0)
		{
			float next = time + extend;
			SIT_ActionReschedule(act, next, next + (act->end - act->start));
			continue;
		}

		if (act->end < time)
			SIT_ActionReschedule(act, -1, -1);
	}
}

/*
 * high performance FPS counter
 */
static struct
{
	int  * delays;
	int    frame, total;
	int    fps;
	int    avgsleep;
	LARGE_INTEGER next;

}	curframe;

DLLIMP int FrameSetFPS(int fps)
{
	if (fps > curframe.fps)
	{
		if (curframe.delays) free(curframe.delays);
		curframe.fps = fps;
		curframe.delays = malloc(fps * sizeof (int));
	}
	int old, i;
	/* fps will likely not be an integral divisisor of freq */
	for (i = 1, old = 0; i <= fps; i ++)
	{
		/* spread the error over next frames */
		curframe.delays[i-1] = sit.QPCfreq * i / fps - old;
		old += curframe.delays[i-1];
	}
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	/* sleep 1 ms: usually it will take longer, but any amount < delays[i], will avoid hogging the CPU */
	Sleep(1);
	QueryPerformanceCounter(&curframe.next);
	curframe.avgsleep = curframe.next.QuadPart - start.QuadPart;
	return 1;
}

DLLIMP void FrameWaitNext(void)
{
	uint64_t wait = curframe.next.QuadPart + curframe.delays[curframe.frame];
	curframe.frame ++;
	curframe.total ++;
	if (curframe.frame >= curframe.fps)
		curframe.frame = 0;

	QueryPerformanceCounter(&curframe.next);

	/* infinite loop */
	do {
		QueryPerformanceCounter(&curframe.next);
		/* use a combination of active/passive wait */
		if (curframe.next.QuadPart + curframe.avgsleep < wait)
			Sleep(1);
	}
	while (curframe.next.QuadPart < wait);
}


DLLIMP double FrameGetTime(void)
{
	LARGE_INTEGER time;
	if (sit.QPCpause > 0)
		time.QuadPart = sit.QPCpause;
	else
		QueryPerformanceCounter(&time);

	/* remove start time to keep numbers low in case they are converted to integer */
	return (time.QuadPart - sit.QPCstart) * sit.QPCfreqinv;
}

DLLIMP void FramePauseUnpause(Bool pause)
{
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	if (! pause)
	{
		/* ignore the time between pause */
		sit.QPCstart += time.QuadPart - sit.QPCpause;
		sit.QPCpause = 0;
	}
	else sit.QPCpause = time.QuadPart;
}

/*
 * dynamic load opengl functions, only the strict subset we need in this library
 */
PFNGLBLENDFUNCSEPARATEPROC glad_glBlendFuncSeparate;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
PFNGLCREATESHADERPROC glad_glCreateShader;
PFNGLSHADERSOURCEPROC glad_glShaderSource;
PFNGLCOMPILESHADERPROC glad_glCompileShader;
PFNGLGETSHADERIVPROC glad_glGetShaderiv;
PFNGLATTACHSHADERPROC glad_glAttachShader;
PFNGLBINDATTRIBLOCATIONPROC glad_glBindAttribLocation;
PFNGLLINKPROGRAMPROC glad_glLinkProgram;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
PFNGLDELETESHADERPROC glad_glDeleteShader;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
PFNGLGETUNIFORMBLOCKINDEXPROC glad_glGetUniformBlockIndex;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
PFNGLGENBUFFERSPROC glad_glGenBuffers;
PFNGLUNIFORMBLOCKBINDINGPROC glad_glUniformBlockBinding;
PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap;
PFNGLBINDBUFFERRANGEPROC glad_glBindBufferRange;
PFNGLSTENCILOPSEPARATEPROC glad_glStencilOpSeparate;
PFNGLUSEPROGRAMPROC glad_glUseProgram;
PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
PFNGLBINDBUFFERPROC glad_glBindBuffer;
PFNGLBUFFERDATAPROC glad_glBufferData;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
PFNGLUNIFORM1IPROC glad_glUniform1i;
PFNGLUNIFORM2FVPROC glad_glUniform2fv;
PFNGLUNIFORM1FPROC glad_glUniform1f;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;

PFNGLBINDTEXTUREPROC glad_glBindTexture;
PFNGLSTENCILMASKPROC glad_glStencilMask;
PFNGLSTENCILFUNCPROC glad_glStencilFunc;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures;
PFNGLGETERRORPROC glad_glGetError;
PFNGLGETINTEGERVPROC glad_glGetIntegerv;
PFNGLFINISHPROC glad_glFinish;
PFNGLGENTEXTURESPROC glad_glGenTextures;
PFNGLPIXELSTOREIPROC glad_glPixelStorei;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri;
PFNGLTEXSUBIMAGE2DPROC glad_glTexSubImage2D;
PFNGLENABLEPROC glad_glEnable;
PFNGLCOLORMASKPROC glad_glColorMask;
PFNGLDISABLEPROC glad_glDisable;
PFNGLDRAWARRAYSPROC glad_glDrawArrays;
PFNGLSTENCILOPPROC glad_glStencilOp;
PFNGLCULLFACEPROC glad_glCullFace;
PFNGLFRONTFACEPROC glad_glFrontFace;
PFNGLVIEWPORTPROC glad_glViewport;
PFNGLCLEARCOLORPROC glad_glClearColor;
PFNGLCLEARPROC glad_glClear;

/* nanovg_gl_utils.h */
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers;
PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer;
PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;
PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers;
PFNGLGETTEXLEVELPARAMETERIVPROC glad_glGetTexLevelParameteriv;

typedef void* (APIENTRYP PFNGLXGETPROCADDRESSPROC_PRIVATE)(const char*);
PFNGLXGETPROCADDRESSPROC_PRIVATE gladGetProcAddressPtr;

HANDLE opengl;
void * load(const char * func)
{
	void * result = NULL;
	if (gladGetProcAddressPtr)
		result = gladGetProcAddressPtr(func);
	if (result == NULL)
		result = GetProcAddress(opengl, func);
	return result;
}

int gladLoadGL(void)
{
	opengl = LoadLibraryA(sit.cssFile = "opengl32.dll");

	if (opengl)
	{
		STRPTR name;
		gladGetProcAddressPtr = (void *) GetProcAddress(opengl, "wglGetProcAddress");
		if ((glad_glBlendFuncSeparate  = load(name = "glBlendFuncSeparate"))
		 && (glad_glGetShaderInfoLog   = load(name = "glGetShaderInfoLog"))
		 && (glad_glGetProgramInfoLog  = load(name = "glGetProgramInfoLog"))
		 && (glad_glCreateProgram      = load(name = "glCreateProgram"))
		 && (glad_glCreateShader       = load(name = "glCreateShader"))
		 && (glad_glShaderSource       = load(name = "glShaderSource"))
		 && (glad_glCompileShader      = load(name = "glCompileShader"))
		 && (glad_glGetShaderiv        = load(name = "glGetShaderiv"))
		 && (glad_glAttachShader       = load(name = "glAttachShader"))
		 && (glad_glBindAttribLocation = load(name = "glBindAttribLocation"))
		 && (glad_glLinkProgram        = load(name = "glLinkProgram"))
		 && (glad_glGetProgramiv       = load(name = "glGetProgramiv"))
		 && (glad_glDeleteProgram      = load(name = "glDeleteProgram"))
		 && (glad_glDeleteShader       = load(name = "glDeleteShader"))
		 && (glad_glGetUniformLocation = load(name = "glGetUniformLocation"))
		 && (glad_glGenVertexArrays    = load(name = "glGenVertexArrays"))
		 && (glad_glGenBuffers         = load(name = "glGenBuffers"))
		 && (glad_glGenerateMipmap     = load(name = "glGenerateMipmap"))
		 && (glad_glBindBufferRange    = load(name = "glBindBufferRange"))
		 && (glad_glStencilOpSeparate  = load(name = "glStencilOpSeparate"))
		 && (glad_glUseProgram         = load(name = "glUseProgram"))
		 && (glad_glActiveTexture      = load(name = "glActiveTexture"))
		 && (glad_glBindBuffer         = load(name = "glBindBuffer"))
		 && (glad_glBufferData         = load(name = "glBufferData"))
		 && (glad_glBindVertexArray    = load(name = "glBindVertexArray"))
		 && (glad_glUniform1i          = load(name = "glUniform1i"))
		 && (glad_glUniform2fv         = load(name = "glUniform2fv"))
		 && (glad_glUniform1f          = load(name = "glUniform1f"))
		 && (glad_glDeleteBuffers      = load(name = "glDeleteBuffers"))
		 && (glad_glDeleteVertexArrays = load(name = "glDeleteVertexArrays"))
		 && (glad_glBindTexture        = load(name = "glBindTexture"))
		 && (glad_glStencilMask        = load(name = "glStencilMask"))
		 && (glad_glStencilFunc        = load(name = "glStencilFunc"))
		 && (glad_glDeleteTextures     = load(name = "glDeleteTextures"))
		 && (glad_glGetError           = load(name = "glGetError"))
		 && (glad_glGetIntegerv        = load(name = "glGetIntegerv"))
		 && (glad_glFinish             = load(name = "glFinish"))
		 && (glad_glGenTextures        = load(name = "glGenTextures"))
		 && (glad_glPixelStorei        = load(name = "glPixelStorei"))
		 && (glad_glTexImage2D         = load(name = "glTexImage2D"))
		 && (glad_glTexParameteri      = load(name = "glTexParameteri"))
		 && (glad_glTexSubImage2D      = load(name = "glTexSubImage2D"))
		 && (glad_glEnable             = load(name = "glEnable"))
		 && (glad_glColorMask          = load(name = "glColorMask"))
		 && (glad_glDisable            = load(name = "glDisable"))
		 && (glad_glDrawArrays         = load(name = "glDrawArrays"))
		 && (glad_glStencilOp          = load(name = "glStencilOp"))
		 && (glad_glCullFace           = load(name = "glCullFace"))
		 && (glad_glFrontFace          = load(name = "glFrontFace"))
		 && (glad_glViewport           = load(name = "glViewport"))
		 && (glad_glClearColor         = load(name = "glClearColor"))
		 && (glad_glClear              = load(name = "glClear"))
		 && (glad_glGetTexLevelParameteriv   = load(name = "glGetTexLevelParameteriv"))
		 && (glad_glGetUniformBlockIndex     = load(name = "glGetUniformBlockIndex"))
		 && (glad_glUniformBlockBinding      = load(name = "glUniformBlockBinding"))
		 && (glad_glEnableVertexAttribArray  = load(name = "glEnableVertexAttribArray"))
		 && (glad_glVertexAttribPointer      = load(name = "glVertexAttribPointer"))
		 && (glad_glDisableVertexAttribArray = load(name = "glDisableVertexAttribArray"))
		 && (glad_glGenFramebuffers          = load(name = "glGenFramebuffers"))
		 && (glad_glBindFramebuffer          = load(name = "glBindFramebuffer"))
		 && (glad_glGenRenderbuffers         = load(name = "glGenRenderbuffers"))
		 && (glad_glBindRenderbuffer         = load(name = "glBindRenderbuffer"))
		 && (glad_glRenderbufferStorage      = load(name = "glRenderbufferStorage"))
		 && (glad_glFramebufferTexture2D     = load(name = "glFramebufferTexture2D"))
		 && (glad_glFramebufferRenderbuffer  = load(name = "glFramebufferRenderbuffer"))
		 && (glad_glCheckFramebufferStatus   = load(name = "glCheckFramebufferStatus"))
		 && (glad_glDeleteFramebuffers       = load(name = "glDeleteFramebuffers"))
		 && (glad_glDeleteRenderbuffers      = load(name = "glDeleteRenderbuffers")))
		{
			sit.cssFile = NULL;
			return 1;
		}
		sit.errorCode = INIT_ERR_OPENGL;
		sit.cssFile = name;
	}
	else sit.errorCode = INIT_ERR_DLL;
	return 0;
}

#ifdef DEBUG_MALLOC
#define DEBUG_IMPL
#include "debugMem.h"
#endif
