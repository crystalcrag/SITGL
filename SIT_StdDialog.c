/*
 * SIT_StdDialog.c : predefined dialog from comdlg32.lib: file and
 * directory (handled through SHBrowseForFolder).
 *
 * Written by T.Pierron, Nov 2009.
 */

#include "platform.h"
#include <malloc.h>
#include <shlobj.h>
#include <stdio.h>
#include "SIT_P.h"

	struct TagList_t FileDialogClass[] = {
		{ "filters",   SIT_Filters,   C__, SIT_STR, OFFSET(SIT_FileDialog, filters) },
		{ "selFilter", SIT_SelFilter, _SG, SIT_INT, OFFSET(SIT_FileDialog, selFilter) },
		{ "initPath",  SIT_InitPath,  _SG, SIT_STR, OFFSET(SIT_FileDialog, initPath) },
		{ "dlgFlags",  SIT_DlgFlags,  _SG, SIT_INT, OFFSET(SIT_FileDialog, flags) },
		{ NULL,        SIT_SelPath,   __G, SIT_PTR, OFFSET(SIT_FileDialog, filePtr) },
		{ NULL,        SIT_NbSelect,  __G, SIT_INT, OFFSET(SIT_FileDialog, nbSelect) },
		{ NULL,        SIT_TagEnd }
	};

	struct TagList_t FolderDialogClass[] = {
		{ "initPath", SIT_InitPath, _SG, SIT_STR, OFFSET(SIT_FolderSel, initPath) },
		{ NULL,       SIT_TagEnd }
	};

	static int SIT_ManageFileDialog(SIT_Widget, APTR, APTR);
	static int SIT_ManageFolderDialog(SIT_Widget, APTR, APTR);

	static HANDLE comdlg;
	static HANDLE ole32;
	extern HANDLE mainWnd;

	BOOL (*pGetSaveFileName)(LPOPENFILENAMEW);
	BOOL (*pGetOpenFileName)(LPOPENFILENAMEW);
	WINAPI void  (*pCoTaskMemFree)(PVOID);

/*
 * initialize a file dialog. There no Win32 API involved yet, only when dialog
 * will be managed, using GetOpenFileName().
 */
static int SIT_SetFileValues(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Variant    value = ud;
	SIT_FileDialog dlg   = (APTR) w;

	if (((TagList)cd)->tl_TagID == SIT_InitPath)
	{
		STRPTR p, base;
		if (dlg->initPath)
		{
			p = alloca(strlen(dlg->initPath) + strlen(value->string) + 5);
			strcpy(p, dlg->initPath);
			base = BaseName(p);
			if (*base) ParentDir(p);
			AddPart(p, value->string, 1<<20);
			free(dlg->initPath);
			dlg->initPath = realloc(value->string, strlen(p) + 1);
			strcpy(dlg->initPath, p);
		}
		else dlg->initPath = value->string;
		for (p = dlg->initPath; *p; p ++)
			if (*p == '/') *p = '\\';
		return 1;
	}
	else return SIT_SetWidgetValue(w, cd, ud);
}

Bool SIT_InitFileDialog(SIT_Widget w, va_list args)
{
	if (! comdlg)
	{
		comdlg = LoadLibrary(L"comdlg32.dll");
		if (comdlg)
		{
			pGetSaveFileName = (APTR) GetProcAddress(comdlg, "GetSaveFileNameW");
			pGetOpenFileName = (APTR) GetProcAddress(comdlg, "GetOpenFileNameW");
		}
	}

	w->setValue = SIT_SetFileValues;
	SIT_ParseTags(w, args, w->attrs = FileDialogClass);
	w->flags   = SITF_TopLevel;
	w->visible = False;
	w->manage  = SIT_ManageFileDialog;
	return TRUE;
}

Bool SIT_InitDirDialog(SIT_Widget w, va_list args)
{
	if (! ole32)
	{
		ole32 = LoadLibrary(L"ole32.dll");
		if (ole32)
		{
			WINAPI HRESULT (*coinit)(PVOID);
			coinit = (APTR) GetProcAddress(ole32, "CoInitialize");
			coinit(NULL);

			pCoTaskMemFree = (APTR) GetProcAddress(ole32, "CoTaskMemFree");
		}
	}
	SIT_ParseTags(w, args, w->attrs = FolderDialogClass);
	w->flags   = SITF_TopLevel;
	w->visible = False;
	w->manage  = SIT_ManageFolderDialog;
	return TRUE;
}

static int AddPartWtoMB(STRPTR buffer, int max, LPWSTR part, Bool dir)
{
	int sz = UTF16ToUTF8(buffer, max, (STRPTR) part, -1);

	if (sz < 0 || ! dir) return sz; buffer += sz-2;
	if (*buffer != '\\' && *buffer != '/') buffer[1] = '\\';
	else sz --;
	return sz;
}

static int SIT_ManageFileDialog(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_FileDialog dlg = (SIT_FileDialog) w;
	Bool ret;

	memset(&dlg->ofn, 0, sizeof dlg->ofn);
	dlg->ofn.lStructSize = sizeof dlg->ofn;
	dlg->ofn.hwndOwner   = mainWnd;
	dlg->ofn.lpstrFile   = dlg->fileName;
	dlg->ofn.nMaxFile    = DIM(dlg->fileName);
	dlg->ofn.Flags       = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	dlg->filePtr         = (STRPTR) dlg->fileName;
	dlg->fileName[0]     = 0;

	if (dlg->flags & SITV_FileMultiSelect)   dlg->ofn.Flags |= OFN_ALLOWMULTISELECT;
	if (dlg->flags & SITV_FileAskCreate)     dlg->ofn.Flags |= OFN_CREATEPROMPT;
	if (dlg->flags & SITV_FileMustExist)     dlg->ofn.Flags |= OFN_FILEMUSTEXIST;
	if (dlg->flags & SITV_FileWarnOverwrite) dlg->ofn.Flags |= OFN_OVERWRITEPROMPT;

	/* title of dialog */
	if (IsDef(w->title))
		allocaUTF8ToUTF16(w->title, dlg->ofn.lpstrTitle);

	/* parse filter string */
	if (dlg->filters)
	{
		int len = MultiByteToWideChar(CP_UTF8, 0, dlg->filters, -1, NULL, 0) + 2;
		LPWSTR p, filters = alloca(len * sizeof *filters);

		MultiByteToWideChar(CP_UTF8, 0, dlg->filters, -1, filters, len);
		memset(filters + len - 2, 0, 2 * sizeof *filters);

		for (p = filters; *p; p ++) {
			if (*p == '\t' || *p == '\n') *p = 0;
			if (*p == '\r') wcscpy(p, p + 1);
		}
		dlg->ofn.lpstrFilter  = filters;
		dlg->ofn.nFilterIndex = dlg->selFilter + 1;
	}

	/* initial dir and file */
	if (dlg->initPath)
	{
		STRPTR base = BaseName(dlg->initPath);

		MultiByteToWideChar(CP_UTF8, 0, base, -1, dlg->fileName, DIM(dlg->fileName));

		if (base > dlg->initPath)
		{
			TEXT ch = base[-1];
			base[-1] = 0;
			allocaUTF8ToUTF16(dlg->initPath, dlg->ofn.lpstrInitialDir);
			base[-1] = ch;
		}
	}

	/* XXX for whatever reason, this function is SUPER slow to init dialog when program is started within gdb */
	if (dlg->flags & SITV_FileSave)
		ret = pGetSaveFileName(&dlg->ofn);
	else
		ret = pGetOpenFileName(&dlg->ofn);

	if (ret)
	{
		/* user selected one or more files: convert from UTF16 to UTF8 */
		LPWSTR p, s;
		STRPTR out = (STRPTR) dlg->fileName;
		int    len, sz, i;

		for (p = dlg->fileName, len = 0; *p; ++len, p = wcschr(p, 0) + 1);
		if (! (dlg->flags & SITV_FileMultiSelect))
			len = 1;

		sz = (STRPTR) p - (STRPTR) dlg->fileName;
		s = alloca(sz);
		memcpy(s, dlg->fileName, sz);
		dlg->nbSelect = (len == 1 ? 1 : len - 1);
		if (dlg->ofn.nFilterIndex > 0)
			dlg->selFilter = dlg->ofn.nFilterIndex - 1;

		/* append first file selected */
		for (i = sz = 0, p = s; i < len; i ++)
		{
			if (i > 1)
				sz += AddPartWtoMB(out + sz, sizeof dlg->fileName - sz, s, True);
			sz += AddPartWtoMB(out + sz, sizeof dlg->fileName - sz, p, i == 0 && len > 1);
			p = wcschr(p, 0) + 1;
		}
		/* keep last selected directory */
		SIT_SetValues(w, SIT_InitPath, dlg->fileName, NULL);

		return True;
	}
	else /* user cancelled */
	{
		dlg->nbSelect = 0;
		return False;
	}
}

/* preselect user directory */
static int CALLBACK SIT_PostInitBrowse(HWND wnd, UINT msg, LPARAM lParam, LPARAM lpData)
{
	if (msg == BFFM_INITIALIZED)
	{
		SIT_FolderSel f = (APTR) lpData;
		LPWSTR dir;

		/* obnoxious API :-/ */
		STRPTR p;
		for (p = f->initPath; *p; p ++)
			if (*p == '/') *p = '\\';
		allocaUTF8ToUTF16(f->initPath, dir);

		SendMessage(wnd, BFFM_SETSELECTION, TRUE, (LPARAM) dir);

		/* set focus to tree view */
		SetFocus(FindWindowEx(wnd, NULL, L"SysTreeView32", NULL));
	}
	return 0;
}

/* std folder chooser dialog initialization */
static int SIT_ManageFolderDialog(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_FolderSel f = (APTR) w;
	LPBROWSEINFO  bi = (APTR) f->info;

	memset(bi, 0, sizeof *bi);
	bi->hwndOwner      = mainWnd;
	bi->pszDisplayName = f->path;
	bi->lpfn           = SIT_PostInitBrowse;
	bi->lParam         = (LPARAM) w;
	bi->ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	if (IsDef(w->title))
		allocaUTF8ToUTF16(w->title, bi->lpszTitle);

	ITEMIDLIST * pidl = SHBrowseForFolder(bi);

	if (pidl)
	{
		SHGetPathFromIDList(pidl, f->path);
		pCoTaskMemFree(pidl);
		int len = WideCharToMultiByte(CP_UTF8, 0, f->path, -1, NULL, 0, NULL, NULL);

		if (f->initPath) free(f->initPath);
		f->initPath = malloc(len);
		WideCharToMultiByte(CP_UTF8, 0, f->path, -1, f->initPath, len, NULL, NULL);

		return True;
	}
	return False;
}
