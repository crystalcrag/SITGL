/*
 * SIT_App.c : root of all widget hierarchy.
 *
 * written by T.Pierron, apr 2020.
 */

#include "platform.h"
#include <malloc.h>
#include <stdlib.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	struct TagList_t AppClass[] = {
		{ NULL, SIT_DefSBArrows,     _SG, SIT_INT,  OFFSET(SIT_App, defSBArrows) },
		{ NULL, SIT_DefSBSize,       _SG, SIT_UNIT, OFFSET(SIT_App, defSBSize) },
		{ NULL, SIT_RefreshMode,     _SG, SIT_INT,  OFFSET(SIT_App, refreshMode) },
		{ NULL, SIT_CurrentDir,      _SG, SIT_PTR,  OFFSET(SIT_App, currentDir) },
		{ NULL, SIT_ScreenWidth,     __G, SIT_INT,  OFFSET(SIT_App, screen.width) },
		{ NULL, SIT_ScreenHeight,    __G, SIT_INT,  OFFSET(SIT_App, screen.height) },
		{ NULL, SIT_TagPrivate+2,    _S_, SIT_PTR,  0 }, /* font-file */
		{ NULL, SIT_TagPrivate+1,    _S_, SIT_PTR,  OFFSET(SIT_App, fontName) },
		{ NULL, SIT_AddFont,         _S_, SIT_ABBR, ABBR(1, 1, 0, 0) },
		{ NULL, SIT_AccelTable,      _SG, SIT_PTR,  OFFSET(SIT_App, accel) },
		{ NULL, SIT_StyleSheet,      _S_, SIT_PTR,  OFFSET(SIT_App, styles) },
		{ NULL, SIT_ExitCode,        _SG, SIT_PTR,  OFFSET(SIT_App, exitCode) },
		{ NULL, SIT_MonitorResol,    __G, SIT_PTR,  OFFSET(SIT_App, resolution) },
		{ NULL, SIT_SetAppIcon,      _S_, SIT_INT,  0 },
		{ NULL, SIT_CompositedAreas, __G, SIT_PTR,  0 },
		{ NULL, SIT_FontScale,       _SG, SIT_INT,  0 },
		{ NULL, SIT_TagEnd }
	};
	static WNDPROC mainWndProc;
	HANDLE mainWnd;

void SIT_AppGetCWD(SIT_Widget w)
{
	SIT_App app = (SIT_App) w;
	uint8_t id[12];

	HANDLE dfh = CreateFileW(L".", GENERIC_READ | FILE_READ_EA |
		FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ |
		FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
		FILE_FLAG_BACKUP_SEMANTICS, NULL
	);

	memset(id, 0, sizeof id); id[0] = 1;
	if (dfh != INVALID_HANDLE_VALUE)
	{
		BY_HANDLE_FILE_INFORMATION info;

		if (GetFileInformationByHandle(dfh, &info))
		{
			memcpy(id+0,  &info.dwVolumeSerialNumber, 4);
			memcpy(id+4,  &info.nFileIndexHigh, 4);
			memcpy(id+8,  &info.nFileIndexLow, 4);
		}
		CloseHandle(dfh);
	}

	/* same CWD than last request: do not reallocate anything */
	if (memcmp(id, app->CWDID, sizeof id))
	{
		int    len = GetCurrentDirectory(0, NULL);
		LPWSTR str = alloca(sizeof *str * len);
		GetCurrentDirectory(len, str);
		len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL) + 1;
		STRPTR utf8 = realloc(((SIT_App)w)->currentDir, len);
		if (utf8) WideCharToMultiByte(CP_UTF8, 0, str, -1, ((SIT_App)w)->currentDir = utf8, len, NULL, NULL), strcat(utf8, "\\");
		memcpy(app->CWDID, id, sizeof id);
	}
}

/* very crude font mapper :-/ */
static Bool RemoveToken(LPWSTR value, LPWSTR token)
{
	if (token == NULL) return True;
	LPWSTR p = wcsstr(value, token);
	int    l = wcslen(token);

	if (p == NULL && wcscmp(token, L"italic") == 0)
		p = wcsstr(value, L"oblique");

	if (p == NULL) return False;
	while (p > value && p[-1] == ' ') p --, l ++;
	wcscpy(p, p + l);
	return True;
}

static STRPTR SIT_GetFontFile(STRPTR fmt, STRPTR dest)
{
	STRPTR p;
	STRPTR fontName = STRDUPA(fmt);
	int    flag = 0;
	HKEY   hkey = NULL;

	for (p = strchr(fontName, '/'); p; )
	{
		STRPTR next = strchr(p + 1, '/');
		*p++ = 0;
		if (next) *next = 0;
		if (strcasecmp(p, "Bold") == 0) flag |= 1;
		if (strcasecmp(p, "Italic") == 0) flag |= 2;
		if (strcasecmp(p, "Boldest") == 0) flag |= 4;
		p = next;
	}

	if (strcasecmp(fontName, "system") == 0)
	{
		NONCLIENTMETRICS ncm;
		ncm.cbSize = sizeof ncm;
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
		int len = WideCharToMultiByte(CP_UTF8, 0, ncm.lfMessageFont.lfFaceName, -1, NULL, 0, NULL, NULL);
		fontName = alloca(len);
		WideCharToMultiByte(CP_UTF8, 0, ncm.lfMessageFont.lfFaceName, -1, fontName, len, NULL, NULL);
	}

	dest[0] = 0;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_QUERY_VALUE, &hkey) == 0)
	{
		wchar_t szValueName[128];
		DWORD   dwValueNameSize = 128;
		BYTE    szValueData[128];
		DWORD   dwValueDataSize = 128;
		DWORD   dwType = 0;
		int     index = MultiByteToWideChar(CP_UTF8, 0, fontName, -1, NULL, 0);
		LPWSTR  font;

		font = alloca(index * sizeof *font);
		MultiByteToWideChar(CP_UTF8, 0, fontName, -1, font, index);

		CharLowerBuffW(font, index);
		index = 0;

		while (RegEnumValueW(hkey, index, szValueName, &dwValueNameSize, NULL, &dwType, szValueData, &dwValueDataSize) == 0)
		{
			CharLowerBuffW(szValueName, dwValueNameSize);
			dwValueDataSize = dwValueNameSize = 128; index ++;
			RemoveToken(szValueName, L"regular");
			if (! RemoveToken(szValueName, L"(truetype)")) continue;
			if (! RemoveToken(szValueName, font)) continue;
			if ((flag & 1) && ! RemoveToken(szValueName, L"bold")) continue;
			if ((flag & 2) && ! RemoveToken(szValueName, L"italic")) continue;
			if ((flag & 4) && ! RemoveToken(szValueName, L"black")) continue;
			if (szValueName[0] == 0)
			{
				snprintf(dest, 128, "%s\\Fonts\\%S", getenv("SystemRoot"), (LPWSTR) szValueData);
				RegCloseKey(hkey);
				return dest;
			}
		}
		RegCloseKey(hkey);
	}
	return NULL;
}

void SIT_ChangeChildrenStyle(SIT_Widget root, int flags)
{
	SIT_Widget list;
	for (list = root; ; )
	{
		layoutClearStyles(list, flags);
		if (list->type == SIT_LISTBOX)
			SIT_ListClearStyles(list, flags);

		if (! list->children.lh_Head)
		{
			if (list == root)
			{
				layoutRecalcWords(list);
				break;
			}
			while (list->node.ln_Next == NULL)
			{
				/* can only be done once chiildren have their styles cleared */
				layoutRecalcWords(list);
				list = list->parent;
				if (list == NULL || list == root) goto layout;
			}
			layoutRecalcWords(list);
			list = (SIT_Widget) list->node.ln_Next;
		}
		else list = (SIT_Widget) list->children.lh_Head;
	}

	/* need to compute CSS box first */
	layout:
	for (list = root; ; )
	{
		if (list->flags & SITF_TopLevel)
			SIT_LayoutWidgets(list, flags & 2 ? FitUsingInitialBox : FitUsingCurrentBox);

		if (! list->children.lh_Head)
		{
			if (list == root) break;
			while (list->node.ln_Next == NULL)
			{
				list = list->parent;
				if (list == NULL || list == root) return;
			}
			list = (SIT_Widget) list->node.ln_Next;
		}
		else list = (SIT_Widget) list->children.lh_Head;
	}
}

/* replace the entire stylesheet */
void SIT_ChangeStyleSheet(STRPTR path, int mode)
{
	if (path)
	{
		SIT_NukeCSS();
		sit.dirty = 1;

		/* don't care if it fails */
		if (! cssParse(path, True))
			SIT_SetValues(sit.root, SIT_Style, "background: white", NULL);
	}

	/* reapply new styles to all widgets */
	sit.geomList = NULL;
	int flags = 0;
	if (path != NULL) flags |= 1;
	if (mode == FitUsingInitialBox) flags |= 2;
	SIT_ChangeChildrenStyle(sit.root, flags);
}


static int SIT_AppSetValues(SIT_Widget w, APTR call_data, APTR user_data)
{
	SIT_Variant val = user_data;
	SIT_App     app = (SIT_App) w;
	TagList     tag = call_data;

	switch (tag->tl_TagID) {
	case SIT_TagPrivate+2:
		/* second arg to SIT_AddFont */
		if (val->pointer == NULL)
		{
			/* XXX nanovg does have a way to delete font */
			return 0;
		}
		if (! FileExists(val->pointer))
		{
			TEXT sysfont[128];
			if (SIT_GetFontFile(val->pointer, sysfont) && nvgCreateFont(sit.nvgCtx, app->fontName, sysfont) >= 0)
				layoutFindFont(w);
		}
		else if (nvgCreateFont(sit.nvgCtx, app->fontName, val->pointer) >= 0)
		{
			layoutFindFont(w);
		}
		break;
	case SIT_TagPrivate+1:
		app->fontName = val->pointer;
		break;
	case SIT_RefreshMode:
		sit.refreshMode = app->refreshMode = val->integer;
		break;
	case SIT_StyleSheet:
		SIT_ChangeStyleSheet(val->pointer, FitUsingCurrentBox);
		break;
	case SIT_CurrentDir:
		/* note: value->string is a user-supplied buffer (SIT_PTR), not a copy: do not modify */
		if (val->string)
		{
			LPWSTR str;
			allocaUTF8ToUTF16(val->string, str);
			SetCurrentDirectory(str);
		}
		break;
	case SIT_SetAppIcon:
		/* SDL1 does not set the icon of the title bar window to the application's icon */
		SetClassLongPtr(mainWnd, GCLP_HICON, (LONG_PTR) (val->integer == 0 ?
			LoadIcon(NULL, IDI_APPLICATION) :
			LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(val->integer)))
		);
		break;
	case SIT_FontScale:
		val->real32 = val->integer * 0.01f;
		if (fabsf(val->real32 - sit.fontScale) > EPSILON)
		{
			sit.fontScale = val->real32;
			SIT_ChangeStyleSheet(NULL, FitUsingInitialBox);
		}
		break;
	default:
		SIT_SetWidgetValue(w, call_data, user_data);
	}
	return 0;
}

typedef struct Param_t *   Param;
struct Param_t
{
	int  processId;
	HWND main;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	Param params = (APTR) lParam;
	DWORD processId = 0;

	GetWindowThreadProcessId(hwnd, &processId);

	if (processId == params->processId)
	{
		params->main = hwnd;
		return 0;
	}
	return 1;
}

static LRESULT CALLBACK SIT_AppSubClassHandler(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DROPFILES)
	{
		HANDLE hDrop = (HANDLE) wParam;

		/* compute size for storing list of path */
		int i, sz, max, count = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

		for (i = sz = max = 0; i < count; i ++)
		{
			int bytes = (DragQueryFile(hDrop, i, NULL, 0) << 1) + 2;
			if (max < bytes) max = bytes;
			sz += bytes;
		}

		int    tblsz  = (count + 1) * sizeof (STRPTR);
		STRPTR buffer = malloc(tblsz + sz), p;
		LPWSTR temp   = alloca(max);
		int    remain = sz;

		for (p = buffer + tblsz, i = 0; i < count; i ++)
		{
			/* get UTF16 encoded path, stored in UTF8 */
			DragQueryFile(hDrop, i, temp, max);
			int len = WideCharToMultiByte(CP_UTF8, 0, temp, -1, p, remain, NULL, 0);
			while (len == 0) /* buffer too small */
			{
				/*
				 * sz was the size in UTF16, we alloced the same amount for UTF8
				 * therefore there is a slight chance UTF8 might be bigger.
				 */
				buffer = realloc(buffer, tblsz + (sz += 256));
				p = buffer + tblsz; remain += 256;
				len = WideCharToMultiByte(CP_UTF8, 0, temp, -1, p, remain, NULL, 0);
			}
			remain -= len;
			p += len;
		}
		DragFinish(hDrop);
		/* set table */
		for (i = 0, p = buffer + tblsz; i < count; i ++, p = strchr(p, 0) + 1)
			((STRPTR *)buffer)[i] = p;
		((STRPTR *)buffer)[i] = NULL;
		SIT_ApplyCallback(sit.root, buffer, SITE_OnDropFiles);
		free(buffer);
		return 0;
	}
	else return mainWndProc(wnd, msg, wParam, lParam);
}

void SIT_AppAllowDnD(void)
{
	if (mainWnd)
	{
		DragAcceptFiles(mainWnd, True);

		/* hi-jacking main window proc (should work with any framework) */
		mainWndProc = (APTR) GetWindowLong(mainWnd, GWL_WNDPROC);
		SetWindowLong(mainWnd, GWL_WNDPROC, (LONG) SIT_AppSubClassHandler);
	}
}

void SIT_AppDelDnD(void)
{
	if (mainWndProc)
	{
		DragAcceptFiles(mainWnd, False);
		SetWindowLong(mainWnd, GWL_WNDPROC, (LONG) mainWndProc);
		mainWndProc = NULL;
	}
}

static int SIT_AppFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_App app = (SIT_App) w;
	SIT_AppDelDnD();
	free(app->currentDir);
	return 1;
}


struct FullScreen_t
{
	uint8_t enabled;
	RECT    oldWndRect;
};

static struct FullScreen_t fullScreen;

DLLIMP void SIT_ToggleFullScreen(int width, int height)
{
	if (fullScreen.enabled == 0)
	{
		/* switch to full screen */
		DEVMODE devmode = {
			.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT,
			.dmBitsPerPel = 32,
			.dmPelsWidth = width,
			.dmPelsHeight = height,
			.dmSize = sizeof devmode
		};
		int ret = ChangeDisplaySettings(&devmode, CDS_FULLSCREEN);
		if (ret == DISP_CHANGE_SUCCESSFUL)
		{
			GetWindowRect(mainWnd, &fullScreen.oldWndRect);
			int style = GetWindowLong(mainWnd, GWL_STYLE);
			/* can't modify GWL_STYLE, will break surface :-/ */
			RECT rect = {.bottom = height, .right = width};
			AdjustWindowRect(&rect, style, False);
			SetWindowPos(mainWnd, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
		}
		else return;
	}
	else
	{
		SIT_App app = (SIT_App) sit.root;
		RECT rect;
		DEVMODE devmode = {
			.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT,
			.dmBitsPerPel = 32,
			.dmPelsWidth = app->screen.width,
			.dmPelsHeight = app->screen.height,
			.dmSize = sizeof devmode
		};
		ChangeDisplaySettings(&devmode, 0);
		/* back to window mode */
		rect = fullScreen.oldWndRect;
		SetWindowPos(mainWnd, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
	}
	fullScreen.enabled = ! fullScreen.enabled;
}

Bool SIT_InitApp(SIT_Widget w, va_list args)
{
	SIT_App app = (SIT_App) w;

	struct Param_t params;

	params.processId = GetCurrentProcessId();
	params.main = NULL;

	EnumWindows(EnumWindowsProc, (LPARAM) &params);
	mainWnd = params.main;

	w->setValue = SIT_AppSetValues;
	w->finalize = SIT_AppFinalize;

	/* default monitor size */
	MONITORINFO info;
	POINT pt;
	info.cbSize = sizeof info;
	GetCursorPos(&pt);
	HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(hmon, &info);
	app->screen.width  = info.rcMonitor.right  - info.rcMonitor.left;
	app->screen.height = info.rcMonitor.bottom - info.rcMonitor.top;

	w->attrs = AppClass;
	SIT_ParseTags(w, args, AppClass);
	w->box.right  = w->fixed.width  = w->maxBox.width  = sit.scrWidth;
	w->box.bottom = w->fixed.height = w->maxBox.height = sit.scrHeight;
	w->flags |= SITF_FixedHeight | SITF_FixedWidth | SITF_TopLevel;

	/* retrieve list of supported monitor resolutions */
	DEVMODE dm = {.dmSize = sizeof dm};
	DATA16  resol = w->userData;
	int     mode, max = 126/2, i;

	w->userData = NULL;
	app->resolution = resol;
	resol[0] = 0;
	for(mode = 0; EnumDisplaySettings(NULL, mode, &dm) != 0; mode ++)
	{
		DATA16 list;
		if (dm.dmBitsPerPel != 32) continue;
		for (list = resol + 1, i = 0; i < resol[0] && ! (list[0] == dm.dmPelsWidth && list[1] == dm.dmPelsHeight); i ++, list += 2);
		if (i == resol[0])
		{
			/* not yet in the list */
			if (resol[0] == max)
			{
				/* not enough space: discard earlier entries (lower resolutions) */
				memmove(resol + 2, resol + 4, (max - 1) * 2);
				list -= 2;
			}
			resol[0] ++;
			list[0] = dm.dmPelsWidth;
			list[1] = dm.dmPelsHeight;
		}
	}

	return True;
}
