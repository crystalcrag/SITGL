/*
 * SIT_App.c : root of all widget hierarchy.
 *
 * written by T.Pierron, apr 2020.
 */

#include "platform.h"
#include <malloc.h>
#include <stdlib.h>
#include "SIT_P.h"
#include "SIT_CSSLayout.h"
#include "nanovg.h"

	TagList AppClass[] = {
		{ SIT_DefSBArrows,     "defSBArrows", _SG, SIT_INT,  OFFSET(SIT_App, defSBArrows) },
		{ SIT_DefSBSize,       "defSBSize",   _SG, SIT_UNIT, OFFSET(SIT_App, defSBSize) },
		{ SIT_RefreshMode,     "refreshMode", _SG, SIT_INT,  OFFSET(SIT_App, refreshMode) },
		{ SIT_CurrentDir,      "currentDir",  _SG, SIT_PTR,  OFFSET(SIT_App, currentDir) },
		{ SIT_ScreenWidth,     NULL,          __G, SIT_INT,  OFFSET(SIT_App, screen.width) },
		{ SIT_ScreenHeight,    NULL,          __G, SIT_INT,  OFFSET(SIT_App, screen.height) },
		{ SIT_TagPrivate+2,    NULL,          _S_, SIT_PTR,  0 }, /* font-file */
		{ SIT_TagPrivate+1,    NULL,          _S_, SIT_PTR,  OFFSET(SIT_App, fontName) },
		{ SIT_AddFont,         "addFont",     _S_, SIT_ABBR, ABBR(1, 1, 0, 0) },
		{ SIT_AccelTable,      "accelTable",  _SG, SIT_PTR,  OFFSET(SIT_App, accel) },
		{ SIT_StyleSheet,      "styleSheet",  _S_, SIT_PTR,  OFFSET(SIT_App, styles) },
		{ SIT_ExitCode,        "exitCode",    _SG, SIT_PTR,  OFFSET(SIT_App, exitCode) },
		{ SIT_SetAppIcon,      NULL,          _S_, SIT_INT,  0 },
		{ SIT_CompositedAreas, NULL,          __G, SIT_PTR,  0 },
		{ SIT_TagEnd }
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

/* replace the entire stylesheet */
void SIT_ChangeStyleSheet(SIT_Widget w, STRPTR path, int mode)
{
	if (path)
	{
		SIT_NukeCSS();
		sit.dirty = 1;

		/* don't care if it fails */
		cssParse(path, True);
		if (! cssParse(path, True))
			SIT_SetValues(sit.root, SIT_Style, "background: white", NULL);
	}

	/* reapply new styles to all widgets */
	sit.geomList = NULL;
	SIT_Widget list;
	for (list = sit.root; ; )
	{
		memset(list->layout.crc32, 0xff, sizeof list->layout.crc32);
		list->layout.curCRC32 = -1;
		list->style.flags &= ~CSSF_APPLIED;
		list->flags &= ~SITF_GeomNotified;
		list->layout.pos.width = 0;
		layoutCalcBox(list);
		layoutRecalcWords(list);
		if (list->parent)
		{
			list->minBox.width = list->minBox.height = -1;
			list->optimalBox   = list->maxBox = list->minBox;
			if ((list->flags & SITF_TopLevel) == 0 || mode == FitUsingInitialBox)
				list->currentBox = list->childBox = list->minBox;
		}

		if (! list->children.lh_Head)
		{
			while (list->node.ln_Next == NULL)
			{
				list = list->parent;
				if (list == NULL) goto layout;
			}
			list = (SIT_Widget) list->node.ln_Next;
		}
		else list = (SIT_Widget) list->children.lh_Head;
	}

	/* need to compute CSS box first */
	layout:
	for (list = sit.root; ; )
	{
		if (list->flags & SITF_TopLevel)
			SIT_LayoutWidgets(list, mode);

		if (! list->children.lh_Head)
		{
			while (list->node.ln_Next == NULL)
			{
				list = list->parent;
				if (list == NULL) return;
			}
			list = (SIT_Widget) list->node.ln_Next;
		}
		else list = (SIT_Widget) list->children.lh_Head;
	}
}


static int SIT_AppSetValues(SIT_Widget w, APTR call_data, APTR user_data)
{
	SIT_Variant * val = user_data;
	SIT_App       app = (SIT_App) w;
	TagList *     tag = call_data;

	switch (tag->tl_TagID) {
	case SIT_LocaleInfo: return 0; /* read-only */
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
		SIT_ChangeStyleSheet(w, val->pointer, FitUsingCurrentBox);
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
	default:
		SIT_SetWidgetValue(w, call_data, user_data);
	}
	return 0;
}

static LocaleInfo SIT_AllocLocale(void)
{
	static short locale[] = { /* stuff we'll extract from GetLocaleInfo() */
		LOCALE_SNATIVELANGNAME,    LOCALE_SNATIVECTRYNAME,    LOCALE_SENGLANGUAGE,
		LOCALE_SENGCOUNTRY,        LOCALE_SISO3166CTRYNAME,   LOCALE_SISO639LANGNAME,
		LOCALE_SCURRENCY,          LOCALE_SENGCURRNAME,       LOCALE_SINTLSYMBOL,
		LOCALE_SSHORTDATE,         LOCALE_SLONGDATE,          LOCALE_SDECIMAL,
		LOCALE_SMONTHNAME1,        LOCALE_SMONTHNAME2,        LOCALE_SMONTHNAME3,
		LOCALE_SMONTHNAME4,        LOCALE_SMONTHNAME5,        LOCALE_SMONTHNAME6,
		LOCALE_SMONTHNAME7,        LOCALE_SMONTHNAME8,        LOCALE_SMONTHNAME9,
		LOCALE_SMONTHNAME10,       LOCALE_SMONTHNAME11,       LOCALE_SMONTHNAME12,
		LOCALE_SABBREVMONTHNAME1,  LOCALE_SABBREVMONTHNAME2,  LOCALE_SABBREVMONTHNAME3,
		LOCALE_SABBREVMONTHNAME4,  LOCALE_SABBREVMONTHNAME5,  LOCALE_SABBREVMONTHNAME6,
		LOCALE_SABBREVMONTHNAME7,  LOCALE_SABBREVMONTHNAME8,  LOCALE_SABBREVMONTHNAME9,
		LOCALE_SABBREVMONTHNAME10, LOCALE_SABBREVMONTHNAME11, LOCALE_SABBREVMONTHNAME12,
		LOCALE_SDAYNAME1,          LOCALE_SDAYNAME2,          LOCALE_SDAYNAME3,
		LOCALE_SDAYNAME4,          LOCALE_SDAYNAME5,          LOCALE_SDAYNAME6,
		LOCALE_SDAYNAME7,          LOCALE_SABBREVDAYNAME1,    LOCALE_SABBREVDAYNAME2,
		LOCALE_SABBREVDAYNAME3,    LOCALE_SABBREVDAYNAME4,    LOCALE_SABBREVDAYNAME5,
		LOCALE_SABBREVDAYNAME6,    LOCALE_SABBREVDAYNAME7,    LOCALE_ILANGUAGE,
		LOCALE_ICOUNTRY,           LOCALE_STIMEFORMAT
	};
	LPWSTR buffer, p;
	STRPTR d, * s;
	int    i, j, len;

	for (i = len = 0; i < DIM(locale); len += GetLocaleInfo(LOCALE_USER_DEFAULT,
	     locale[i], NULL, 0), i ++);

	buffer = alloca(len * sizeof *buffer);

	for (p = buffer, i = j = len = 0; i < DIM(locale); i ++, p += j) {
		j = GetLocaleInfo(LOCALE_USER_DEFAULT, locale[i], p, 0xffff);
		len += WideCharToMultiByte(CP_UTF8, 0, p, -1, NULL, 0, NULL, NULL);
		if (locale[i] == LOCALE_SISO639LANGNAME) p[-1] = '_';
	}
	LocaleInfo loc = malloc(sizeof *loc + len);

	for (s = &loc->nlsLang, d = (STRPTR) (loc+1), p = buffer, i = 0; i < DIM(locale) - 1; i ++, s ++) {
		d += WideCharToMultiByte(CP_UTF8, 0, p, -1, *s = d, len, NULL, NULL);
		p = wcschr(p, 0) + 1;
	}
	return loc;
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
	free(app->locale);
	free(app->currentDir);
	return 1;
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
	app->screen.width  = info.rcWork.right  - info.rcWork.left;
	app->screen.height = info.rcWork.bottom - info.rcWork.top;

	w->attrs = AppClass;
	SIT_ParseTags(w, args, AppClass);
	w->box.right  = w->fixed.width  = w->maxBox.width  = sit.scrWidth;
	w->box.bottom = w->fixed.height = w->maxBox.height = sit.scrHeight;
	w->flags |= SITF_FixedHeight | SITF_FixedWidth | SITF_TopLevel;

	app->locale = SIT_AllocLocale();

	return True;
}
