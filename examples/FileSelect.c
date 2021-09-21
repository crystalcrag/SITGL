/*
 * FileSelect.c: simple file selection dialog to select one or multiple files
 *
 * written by T.Pierron, May 2021.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "SIT.h"

struct FileSelect_t
{
	SIT_CallProc cb;
	TEXT         curDir[256];
	SIT_Widget   diag;
	SIT_Widget   list;
	SIT_Widget   path;
	SIT_Widget   edit;
	SIT_Widget   tip;
	APTR         ud;
	uint8_t      autoSel;
	uint8_t      flags;
	uint8_t      hasTip;
	uint8_t      accept;
	uint16_t     len;
};

typedef struct FileSelect_t *   FileSelect;

static void FSFillList(STRPTR path, SIT_Widget list)
{
	SIT_ListDeleteRow(list, DeleteAllRows);

	ScanDirData args;

	if (ScanDirInit(&args, path))
	{
		do
		{
			TEXT size[64];
			if (args.isDir)
			{
				size[0] = 0;
				StrCat(args.name, 256, 0, "/");
			}
			else FormatNumber(size, sizeof size, "%d KB", (args.size+1023) >> 10);
			SIT_ListInsertItem(list, -1, (APTR) args.isDir, args.name, args.type, args.date, size);
		}
		while (ScanDirNext(&args));
	}
	SIT_ListReorgColumns(list, "*--*");
}

static int FSSortFiles(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnSort * sort = cd;
	if (sort->item1 != sort->item2)
	{
		return (int) sort->item2 - (int) sort->item1;
	}
	else switch (sort->column) {
	case 0:  return SIT_SortAlphaNum;
	case 3:  return SIT_SortNumeric;
	default: return SIT_SortAlpha;
	}
}

static int FSCellPaint(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnCellPaint * paint = cd;

	memcpy(paint->fgColor, ud && (paint->rowColumn & 0xff) == 0 ? "\xaa\xaa\xff\xff" : "\0\0\0", 4);
	memcpy(paint->bgColor, (paint->rowColumn >> 8) & 1 ? "\0\0\0\x4" : "\0\0\0", 4);

	return 1;
}

/* SITE_OnChange on list box */
static int FSSelItem(SIT_Widget w, APTR cd, APTR ud)
{
	FileSelect file = ud;
	STRPTR     text;
	int        nth;
	SIT_GetValues(w, SIT_SelectedIndex, &nth, NULL);
	text = SIT_ListGetCellText(w, 0, nth);

	if (file->hasTip)
		SIT_SetValues(file->tip, SIT_Visible, False, NULL), file->hasTip = 0;
	if (file->autoSel == 0)
		SIT_SetValues(file->edit, SIT_Title, text, SIT_StartSel, strlen(text), NULL);
	return 1;
}

/* SITE_OnChange on edit box */
static int FSAutoComplete(SIT_Widget w, APTR cd, APTR ud)
{
	FileSelect file = ud;
	int len = strlen(cd);
	/* only auto-complete when character were added */
	if (len > file->len)
	{
		file->autoSel = 1;
		SIT_SetValues(file->list, SIT_AutoComplete, cd, NULL);
		file->autoSel = 0;

		int pos, sel;
		SIT_GetValues(file->list, SIT_SelectedIndex, &sel, NULL);
		SIT_GetValues(w, SIT_StartSel, &pos, NULL);
		SIT_SetValues(w, SIT_Title, SIT_ListGetCellText(file->list, 0, sel), SIT_StartSel, pos, SIT_EndSel, 100000, NULL);
	}
	if (file->hasTip)
		SIT_SetValues(file->tip, SIT_Visible, False, NULL), file->hasTip = 0;
	file->len = len;
	return 1;
}

static void FSNotifySelection(FileSelect file);

static int FSAccept(SIT_Widget w, APTR cd, APTR ud)
{
	FileSelect file = ud;
	SIT_CloseDialog(w);
	file->accept = 1;
	FSNotifySelection(file);
	return 1;
}

static void FSWarnOverwrite(FileSelect file, STRPTR name)
{
	static struct SIT_Accel_t accels[] = {
		{SITK_FlagCapture + SITK_Return, SITE_OnActivate, "ok"},
		{SITK_FlagCapture + SITK_Escape, SITE_OnClose},
		{0}
	};
	SIT_Widget diag = SIT_CreateWidget("warn.warning", SIT_DIALOG, file->diag,
		SIT_DialogStyles, SITV_Plain | SITV_Movable | SITV_Modal,
		SIT_AccelTable,   accels,
		NULL
	);
	TEXT title[128];
	snprintf(title, sizeof title, "Are you sure you want to overwrite the file '%s' ?", name);
	SIT_CreateWidgets(diag,
		"<label name=text title=", title, "top=FORM,,NOPAD left=FORM,,NOPAD right=FORM,,NOPAD>"
		"<button name=ok title=Yes top=WIDGET,text,0.5em buttonType=", SITV_DefaultButton, ">"
		"<button name=ko title=No top=WIDGET,text,0.5em right=FORM buttonType=", SITV_CancelButton, ">"
	);
	SIT_Widget ok = SIT_GetById(diag, "ok");
	SIT_SetAttributes(diag, "<ok right=WIDGET,ko,0.5em>");
	SIT_AddCallback(ok, SITE_OnActivate, FSAccept, file);
	SIT_SetFocus(ok);
	SIT_ManageWidget(diag);
}

static void FSNotifySelection(FileSelect file)
{
	STRPTR path, name, full;

	SIT_GetValues(file->path, SIT_Title, &path, NULL);
	if (file->flags & SITV_FileMultiSelect)
	{
		int count, i, sel, total, len;
		SIT_GetValues(file->list, SIT_ItemCount, &count, NULL);
		/* get total file length */
		for (i = total = 0, len = strlen(path); i < count; i ++)
		{
			SIT_GetValues(file->list, SIT_RowSel(i), &sel, NULL);
			if (sel) total += len + strlen(SIT_ListGetCellText(file->list, 0, i)) + 3;
		}
		full = total > 1024 ? malloc(total) : alloca(total);

		/* build NUL terminated list of list */
		for (i = 0, name = full; i < count; i ++)
		{
			SIT_GetValues(file->list, SIT_RowSel(i), &sel, NULL);
			if (! sel) continue;
			strcpy(name, path);
			AddPart(name, SIT_ListGetCellText(file->list, 0, i), total);
			name = strchr(name, 0) + 1;
		}
		/* double NUL byte at the end */
		*name = 0;

		file->cb(file->diag, full, file->ud);

		if (total > 1024) free(full);
	}
	else
	{
		SIT_GetValues(file->edit, SIT_Title, &name, NULL);

		int max = strlen(path) + strlen(name) + 3;
		full = alloca(max);
		strcpy(full, path);
		AddPart(full, name, max);
		/* double NUL byte at the end */
		path = strchr(full, 0); path[1] = 0;

		if ((file->flags & SITV_FileMustExist) && ! FileExists(full))
		{
			SIT_SetValues(file->tip, SIT_Visible, True, SIT_Title|XfMt, "%s: the file does not exist", name, NULL);
			file->hasTip = 1;
			return;
		}

		if (! file->accept && (file->flags & SITV_FileWarnOverwrite) && FileExists(full))
		{
			FSWarnOverwrite(file, name);
			return;
		}

		file->cb(file->diag, full, file->ud);
	}

	SIT_CloseDialog(file->diag);
}

/* SIT_Accel callback */
static int FSRedirectKeys(SIT_Widget w, APTR cd, APTR ud)
{
	struct SIT_OnKey_t msg = {.keycode = (int) cd};
	FileSelect file;
	SIT_GetValues(w, SIT_UserData, &file, NULL);
	if (msg.keycode == SITK_Return)
	{
		STRPTR path, prev, dir = file->curDir;
		STRPTR save = STRDUPA(dir);
		TEXT   old;
		SIT_GetValues(file->edit, SIT_Title, &path, NULL);
		for (prev = strchr(save, 0) - 1; prev > save && (*prev == '/' || *prev == '\\'); *prev-- = 0);
		AddPart(dir, path, sizeof file->curDir);
		/* split into path and or file */
		for (prev = NULL; ! IsDir(dir); )
		{
			/* try to get what can be reached as a directory, place the rest in file edit */
			STRPTR base = BaseName(dir);
			if (base == dir) break;
			if (prev) *prev = old;
			if (base[-1] == '/' || base[-1] == '\\')
				base --, prev = base, old = base[0], base[0] = 0;
		}
		if (prev && prev[1]) prev ++;

		if (strcmp(dir, save))
		{
			SIT_SetValues(file->path, SIT_Title, dir,  NULL);
			SIT_SetValues(file->edit, SIT_Title, prev, NULL);
			FSFillList(dir, file->list);
		}
		else if (path[0])
		{
			FSNotifySelection(file);
		}
	}
	else SIT_ApplyCallback(file->list, &msg, SITE_OnRawKey);
	return 1;
}

/* SITE_OnActivate on list */
static int FSActiveItem(SIT_Widget w, APTR cd, APTR ud)
{
	FileSelect file = ud;

	if (cd)
	{
		/* directory */
		STRPTR path;
		SIT_GetValues(file->edit, SIT_Title, &path, NULL);
		AddPart(file->curDir, path, sizeof file->curDir);
		SIT_SetValues(file->path, SIT_Title, file->curDir, NULL);
		SIT_SetValues(file->edit, SIT_Title, "", NULL);
		FSFillList(file->curDir, file->list);
	}
	else FSNotifySelection(file);
	return 1;
}

static int FSGotoParent(SIT_Widget w, APTR cd, APTR ud)
{
	FileSelect file = ud;
	if (ParentDir(file->curDir))
	{
		SIT_SetValues(file->path, SIT_Title, file->curDir, NULL);
		SIT_SetValues(file->edit, SIT_Title, "", NULL);
		FSFillList(file->curDir, file->list);
	}
	return 1;
}

static int FSSelect(SIT_Widget w, APTR cd, APTR ud)
{
	FSRedirectKeys(ud, (APTR) SITK_Return, NULL);
	return 1;
}

SIT_Widget FSOpen(SIT_Widget parent, STRPTR curdir, SIT_CallProc cb, APTR ud, int flags)
{
	static struct SIT_Accel_t accels[] = {
		{SITK_FlagCapture + SITK_Up,       -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_Down,     -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_Home,     -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_End,      -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_PrevPage, -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_NextPage, -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_Return,   -1, NULL, FSRedirectKeys},
		{SITK_FlagCapture + SITK_BackSpace + SITK_FlagCtrl, SITE_OnActivate, "parent"},
		{SITK_Escape, SITE_OnClose},
		{0}
	};
	struct FileSelect_t * file;
	SIT_Widget diag = SIT_CreateWidget("fileselect.bg", SIT_DIALOG + SIT_EXTRA(sizeof (struct FileSelect_t)), parent,
		SIT_Title,        "Select file",
		SIT_DialogStyles, SITV_Movable | SITV_Resizable | SITV_Plain,
		SIT_AccelTable,   accels,
		NULL
	);
	int lbFlags = flags & SITV_FileMultiSelect ? SITV_DoSort | SITV_SelectMultiple : SITV_DoSort;

	SIT_GetValues(diag, SIT_UserData, &file, NULL);
	SIT_CreateWidgets(diag,
		"<editbox name=curdir left=FORM readOnly=1>"
		"<button name=parent.save title=Parent right=FORM top=MIDDLE,curdir>"
		"<editbox name=path left=FORM bottom=FORM>"
		"<button name=select.save title=", flags & SITV_FileSave ? "Save" : "Select", "right=FORM top=MIDDLE,path>"
		"<listbox name=files style='font-size: 0.8em' viewMode=", SITV_ListViewReport, "columnNames='Name\tType\tDate\tSize' height=10em width=30em"
		" left=FORM right=FORM top=WIDGET,curdir,0.3em bottom=WIDGET,path,0.5em listBoxFlags=", lbFlags, "columnAlign='L\tL\tL\tR' sortColumn=0"
		" cellPaint=", FSCellPaint, "minWidth=15em>"
	);
	SIT_SetAttributes(diag, "<curdir right=WIDGET,parent,0.3em><path right=WIDGET,select,0.3em>");
	SIT_Widget list = file->list = SIT_GetById(diag, "files");
	SIT_Widget edit = file->edit = SIT_GetById(diag, "path");
	file->tip = SIT_CreateWidget("infotip", SIT_TOOLTIP, file->edit,
		SIT_DelayTime, SITV_TooltipManualTrigger,
		NULL
	);
	SIT_Widget app = parent;
	/* get root widget */
	for (;;)
	{
		SIT_Widget parent;
		SIT_GetValues(app, SIT_Parent, &parent, NULL);
		if (parent == NULL) break;
		app = parent;
	}

	if (curdir == NULL)
	{
		SIT_GetValues(app, SIT_CurrentDir, &curdir, NULL);
	}
	else if (IsRelativePath(curdir))
	{
		/* make it absolute */
		STRPTR path, cur;
		SIT_GetValues(app, SIT_CurrentDir, &path, NULL);
		cur = alloca(strlen(path) + strlen(curdir) + 3);
		strcpy(cur, path);
		AddPart(cur, curdir, 1e6);
		curdir = cur;
	}
	SIT_AddCallback(list, SITE_OnSortItem, FSSortFiles,    NULL);
	SIT_AddCallback(list, SITE_OnChange,   FSSelItem,      file);
	SIT_AddCallback(list, SITE_OnActivate, FSActiveItem,   file);
	SIT_AddCallback(edit, SITE_OnChange,   FSAutoComplete, file);
	SIT_AddCallback(SIT_GetById(diag, "parent"), SITE_OnActivate, FSGotoParent, file);
	SIT_AddCallback(SIT_GetById(diag, "select"), SITE_OnActivate, FSSelect, diag);
	SIT_SetValues(file->path = SIT_GetById(diag, "curdir"), SIT_Title, curdir, NULL);
	SIT_SetFocus(edit);
	CopyString(file->curDir, curdir, sizeof file->curDir);
	file->cb = cb;
	file->ud = ud;
	file->diag = diag;
	file->flags = flags;

	FSFillList(curdir, list);

	return diag;
}
