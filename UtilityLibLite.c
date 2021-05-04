/*
 * UtilityLibLite.c: stripped version of UtilityLib that only contains the
 * mostly necessary to compile SITGL.
 *
 * written by T.Pierron, oct 2013.
 */

#define UNICODE
#include <malloc.h>
#include <stdio.h>
#include <windows.h>
#include <sys/stat.h>
#include <unistd.h>  /* ftruncate */
#include <wchar.h>
#include <ctype.h>
#include <process.h>
#include "UtilityLibLite.h"

#ifdef DEBUG_MALLOC
#include "debugMem.h"
#endif

#define	utf8len(x)     MultiByteToWideChar(CP_UTF8, 0, x, -1, NULL, 0)
#define	utf8toutf16(utf8, utf16) \
{ \
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0); \
\
	utf16 = alloca(len * sizeof *utf16); \
\
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (LPWSTR) utf16, len); \
}

#define	RGB_GETB(rgb)   (((rgb) >> 16) & 0xff)
#define	RGB_GETG(rgb)   (((rgb) >> 8) & 0xff)
#define	RGB_GETR(rgb)   ((rgb) & 0xff)

DLLIMP void GetSelectionColor(DATA8 fg, DATA8 bg)
{
	if (fg)
	{
		DWORD rgb = GetSysColor(COLOR_HIGHLIGHTTEXT);

		fg[0] = RGB_GETR(rgb);
		fg[1] = RGB_GETG(rgb);
		fg[2] = RGB_GETB(rgb);
		fg[3] = 255;
	}
	if  (bg)
	{
		DWORD rgb = GetSysColor(COLOR_HIGHLIGHT);

		bg[0] = RGB_GETR(rgb);
		bg[1] = RGB_GETG(rgb);
		bg[2] = RGB_GETB(rgb);
		bg[3] = 255;
	}
}

DLLIMP ULONG TimeMS(void)
{
	return GetTickCount();
}

static void ScanDirFill(ScanDirData * ret)
{
	LPWIN32_FIND_DATAW data = ret->handle;

	// name, type, size, isDir
	ret->size = ((uint64_t)data->nFileSizeHigh<<32) | data->nFileSizeLow;
	ret->isDir = data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 1 : 0;

	WideCharToMultiByte(CP_UTF8, 0, data->cFileName, -1, ret->name, MAX_PATH, NULL, NULL);

	// Get file type using shell API
	SHFILEINFOW info = {};
	SHGetFileInfoW(data->cFileName, data->dwFileAttributes, &info, sizeof info, SHGFI_USEFILEATTRIBUTES|SHGFI_TYPENAME);

	WideCharToMultiByte(CP_UTF8, 0, info.szTypeName, -1, ret->type, 80, NULL, NULL);

	SYSTEMTIME sys, local;

	FileTimeToSystemTime(&data->ftLastWriteTime, &sys);
	SystemTimeToTzSpecificLocalTime(NULL, &sys, &local);

	sprintf(ret->date, "%d-%02d-%02d %02d:%02d:%02d", local.wYear, local.wMonth, local.wDay,
		local.wHour, local.wMinute, local.wSecond);
}

DLLIMP int ScanDirInit(ScanDirData * ret, STRPTR path)
{
	int sz = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0)+2;

	LPWIN32_FIND_DATAW data = malloc(sz*2 + sizeof (HANDLE) + sizeof *data + 80 + MAX_PATH);

	ret->handle = data;
	ret->path   = data + 1; ret->path += sizeof (HANDLE);
	ret->name   = ret->path + sz*2;
	ret->type   = ret->name + MAX_PATH;

	MultiByteToWideChar(CP_UTF8, 0, path, -1, ret->path, sz);
	if (GetFileAttributesW(ret->path) & FILE_ATTRIBUTE_DIRECTORY)
		wcscat(ret->path, L"\\*");

	HANDLE hnd = FindFirstFileW(ret->path, ret->handle);

	if (hnd != INVALID_HANDLE_VALUE)
	{
		memcpy(data + 1, &hnd, sizeof hnd);
		/* ignore "." and ".." */
		for (;;)
		{
			if (data->cFileName[0] == '.')
			{
				int ch = data->cFileName[1];
				if (ch == 0 || (ch == '.' && data->cFileName[2] == 0))
					FindNextFile(hnd, data);
				else
					break;
			}
			else break;
		}
		ScanDirFill(ret);
		return 1;
	}
	ret->error = GetLastError();
	if (ret->error == ERROR_FILE_NOT_FOUND || ret->error == ERROR_PATH_NOT_FOUND)
		ret->error = 0;
	free(ret->handle);
	ret->handle = NULL;
	return 0;
}

DLLIMP int ScanDirNext(ScanDirData * ret)
{
	LPWIN32_FIND_DATAW data = ret->handle;
	HANDLE hnd;
	memcpy(&hnd, data+1, sizeof hnd);
	while (FindNextFileW(hnd, data))
	{
		ScanDirFill(ret);
		return 1;
	}
	FindClose(hnd);
	free(ret->handle);
	return 0;
}

DLLIMP void ScanDirCancel(ScanDirData * ret)
{
	LPWIN32_FIND_DATAW data = ret->handle;
	HANDLE hnd;
	memcpy(&hnd, data+1, sizeof hnd);
	FindClose(hnd);
	free(ret->handle);
}

/* DOS/AddPart */
static Bool IsParent(STRPTR path)
{
	return path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\\');
}

DLLIMP Bool IsRelativePath(STRPTR file)
{
	return ! ((isalpha(file[0]) && file[1] == ':') || (file[0] == '/' && file[1] == '/') || (file[0] == '\\' && file[1] == '\\'));
}

DLLIMP Bool AddPart(STRPTR path, STRPTR src, int max)
{
	STRPTR p    = strchr(path, 0);
	STRPTR file = src;
	int    n    = 0;
	TEXT   sep;

	/* Check for separator used in source path */
	while (p > path && *p != '/' && *p != '\\') p --;
	if (p == path)
	{
		for (p = src; *p && *p != '/' && *p != '\\'; p ++);
		sep = (*p ? *p : '\\');
	}
	else sep = *p;
	p = strchr(path, 0);

	/* Is file a fully qualified path ? */
	if (isalpha(file[0]) && file[1] == ':')
	{
		if (strlen(file) >= max) return False;

		strcpy(path, file);
	}
	else if (file[0] == '/' || file[0] == '\\')
	{
		/* Absolute path name */
		p = path;
		if (isalpha(p[0]) && p[1] == ':') p += 2, max -= 2;
		if (strlen(file) >= max) return False;
		strcpy(p, file);
	}
	else for (n = p - path; *file && n < max; file ++) /* Append file to path */
	{
		if (IsParent(file) && p > path && ! IsParent(p - 2))
		{
			/* Get directly parent directory of path */
			ParentDir(path);
			file += 2;
			n = strlen(path);
			p = path + n;
			if (*file == 0) file --;
		}
		else if (file[0] == '.' && strchr("/\\", file[1]))
		{
			file ++;
			if (file[0]) file ++;
		}
		else /* Append part */
		{
			if (p > path && p[-1] != sep && n < max)
				*p ++ = sep, n ++;

			for (; n < max && *file && *file != '/' && *file != '\\'; file ++, n ++)
				*p ++ = *file;

			if (n < max) p[ 0] = 0;
			else         p[-1] = 0;
			if (*file == 0) file --;
		}
	}
	return True;
}

/* DOS/ParentDir */
DLLIMP Bool ParentDir(STRPTR dir)
{
	if (! IsDef(dir)) return False;

	STRPTR p = strchr(dir, 0) - 1;

	/* Ignore final slash */
	if (p > dir && (*p == '/' || *p == '\\')) p --;

	while (p > dir && *p != '/' && *p != '\\') p --;

	if (p >= dir && isalpha(p[0]) && p[1] == ':') return False;

	*p = 0;

	return True;
}

/* DOS/CreatePath */
DLLIMP STRPTR CreatePath(STRPTR path, Bool not_last)
{
	LPWSTR dup;
	LPWSTR p, next;
	STRPTR ret = NULL;
	DWORD  attr;
	int    unc = -1;

	utf8toutf16(path, dup);

	for (p = dup; p && *p; p = next)
	{
		for (next = p; *next && *next != '/' && *next != '\\'; next ++);

		if (p == next || (unc > 0 && unc < 3)) { unc ++; next ++; continue; } /* Absolute or UNC path */
		if (next[0] == 0 && not_last) break;
		if (next[0] == 0) next = NULL;
		else              next[0] = 0;

		/* Check if dir exists */
		/* ERROR_ALREADY_EXISTS can still be triggered when 2 or more threads create the same path at almost the same time */
		attr = GetFileAttributesW(dup);
		if ((attr == 0xFFFFFFFF && CreateDirectoryW(dup, NULL) == 0 &&
		     GetLastError() != ERROR_ALREADY_EXISTS) || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			for (p = wcschr(dup, 0); p > dup && *p != '/' && *p != '\\'; p --);
			ret = NthChar(path, p - dup + 1);
			break;
		}
		if (next) *next++ = '/';
	}
	return ret;
}

/* DOS/BaseName */
DLLIMP STRPTR BaseName(STRPTR path)
{
	if (! IsDef(path)) return NULL;

	STRPTR end = strchr(path, 0);

	while (end > path && *end != '/' && *end != '\\') end --;

	return end > path ? end + 1 : path;
}


/* DOS/IsDir */
DLLIMP Bool IsDir(STRPTR file)
{
	LPWSTR path;
	DWORD  ret;

	utf8toutf16(file, path);
	ret = GetFileAttributesW(path);

	return ret != (DWORD) -1 && (ret & FILE_ATTRIBUTE_DIRECTORY);
}

/* DOS/TimeStamp */
DLLIMP ULONG TimeStamp(STRPTR path, int type)
{
	struct _stat stbuf;
	LPWSTR dir;

	if (path == NULL) return 0;

	utf8toutf16(path, dir);

	if (_wstat(dir, &stbuf) == 0)
	{
		switch (type) {
		case 0: return stbuf.st_ctime;
		case 1: return stbuf.st_atime;
		case 2: return stbuf.st_mtime;
		}
	}
	return 0;
}


/* DOS/FileSize  */
ULLONG FileSize(STRPTR file)
{
	LPWSTR path;
	HANDLE hFile;

	utf8toutf16(file, path);

	hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL, OPEN_EXISTING, 0, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		DWORD hi;
		DWORD sz = GetFileSize(hFile, &hi);

		CloseHandle(hFile);
		if (sz != 0xFFFFFFFF) return sz|((ULLONG)hi<<32);
	}
	return 0;
}


/* DOS/Execute */
DLLIMP int Execute(STRPTR cmd, int timeout)
{
	/* Quoted from MinGW runtime library */
	STARTUPINFO sa;
	PROCESS_INFORMATION pi;
	DWORD ec = 1;
	LPWSTR wcmd;

	memset(&sa, 0, sizeof(sa));
	memset(&pi, 0, sizeof(pi));
	utf8toutf16(cmd, wcmd);
	if (!CreateProcess(0, wcmd, 0, 0, 1, 0, 0, 0, &sa, &pi))
	{
		return -1;
	}

	if (timeout >= -1)
	{
		if (WaitForSingleObject(pi.hProcess, timeout == -1 ? INFINITE : timeout) == WAIT_TIMEOUT)
		{
			TerminateProcess(pi.hProcess, 0xbad);
		}
		GetExitCodeProcess(pi.hProcess, &ec);
	}
	else ec = 0;

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return ec;
}

/* open document with currently assigned tool */
DLLIMP int OpenDocument(STRPTR filename)
{
	LPWSTR doc;
	utf8toutf16(filename, doc);
	return (int) ShellExecute(NULL, L"open", doc, NULL, NULL, SW_SHOWNORMAL) > 32;
}

/* DOS/GetSysError */
DLLIMP void GetErrorMsg(int code, STRPTR buffer, int max)
{
	LPWSTR ptr;

	FormatMessageW(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
	    NULL,
	    code < 0 ? GetLastError() : code,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
	    (APTR) &ptr,
	    0,
	    NULL
	);

	int    len = WideCharToMultiByte(CP_UTF8, 0, ptr, -1, NULL, 0, NULL, NULL);
	STRPTR msg = alloca(len), eol;

	if (max < 0)
		eol = strchr(buffer, 0), max = -max;
	else
		eol = buffer;

	WideCharToMultiByte(CP_UTF8, 0, ptr, -1, msg, len, NULL, NULL);
	LocalFree(ptr);
	max -= eol - buffer;
	if (max < len)
	{
		DATA8 p;
		/* Truncate message: only full UTF-8 characters */
		for (p = (DATA8) msg + max; p > (DATA8) msg && (*p & 0xc0) == 0x80; p --);
		CopyString(eol, msg, p - (DATA8) msg);
	}
	else strcpy(eol, msg);
}

DLLIMP STRPTR GetError(void)
{
	static TEXT buffer[100];
	GetErrorMsg(-1, buffer, sizeof buffer);
	return buffer;
}

/* DOS/DeleteDOS */
DLLIMP Bool DeleteDOS(STRPTR file)
{
	LPWSTR path;
	DWORD  attr;

	utf8toutf16(file, path);
	attr = GetFileAttributesW(path);

	if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY))
		return RemoveDirectoryW(path);
	else
		return DeleteFileW(path);
}

/* DOS/FileCopy */
DLLIMP Bool FileCopy(STRPTR from, STRPTR to, Bool overwrite)
{
	LPWSTR fromw, tow;

	utf8toutf16(from, fromw);
	utf8toutf16(to,   tow);

	return CopyFileW(fromw, tow, ! overwrite);
}

/* DOS/FileRename */
DLLIMP Bool FileRename(STRPTR from, STRPTR to, Bool overwrite)
{
	DWORD  flags   = overwrite ? MOVEFILE_REPLACE_EXISTING : 0;
	LPWSTR fromw, tow;

	utf8toutf16(from, fromw);
	utf8toutf16(to,   tow);

	return MoveFileExW(fromw, tow, MOVEFILE_COPY_ALLOWED | flags);
}

DLLIMP int GetErrorCode(void)
{
	return GetLastError();
}

/* DOS/fopen_enc */
DLLIMP void * fopen_enc(const char * file, const char * mode)
{
	LPWSTR path;
	LPWSTR modew;

	utf8toutf16(file, path);
	utf8toutf16(mode, modew);

	return _wfopen(path, modew);
}

DLLIMP Bool FileExists(STRPTR file)
{
	LPWSTR path;
	utf8toutf16(file, path);
	return GetFileAttributesW(path) != 0xFFFFFFFF;
}

/* DOS/IsSameFile */
DLLIMP Bool IsSameFile(STRPTR file1, STRPTR file2)
{
	Bool   ret  = False;
	LPWSTR path1, path2;
	int    flag1, flag2;
	HANDLE fh1,   fh2;

	utf8toutf16(file1, path1);
	utf8toutf16(file2, path2);
	flag1 = GetFileAttributesW(path1); if (flag1 == (DWORD) -1) return False;
	flag2 = GetFileAttributesW(path2); if (flag2 == (DWORD) -1) return False;

	if (flag1 & FILE_ATTRIBUTE_DIRECTORY)
		fh1 = CreateFileW(path1, GENERIC_READ | FILE_READ_EA |
			FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ |
			FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
			FILE_FLAG_BACKUP_SEMANTICS, NULL
		);
	else
		fh1 = CreateFileW(path1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	if (flag2 & FILE_ATTRIBUTE_DIRECTORY)
		fh2 = CreateFileW(path2, GENERIC_READ | FILE_READ_EA |
			FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ |
			FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
			FILE_FLAG_BACKUP_SEMANTICS, NULL
		);
	else
		fh2 = CreateFileW(path2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	if (fh1 != INVALID_HANDLE_VALUE && fh2 != INVALID_HANDLE_VALUE)
	{
		BY_HANDLE_FILE_INFORMATION info1, info2;

		if (GetFileInformationByHandle(fh1, &info1) &&
		    GetFileInformationByHandle(fh2, &info2) )
		{
			ret = info1.dwVolumeSerialNumber == info2.dwVolumeSerialNumber &&
			      info1.nFileIndexHigh       == info2.nFileIndexHigh       &&
			      info1.nFileIndexLow        == info2.nFileIndexLow;
		}
		CloseHandle(fh2);
	}
	if (fh1 != INVALID_HANDLE_VALUE) CloseHandle(fh1);
	return ret;
}

/* DOS/TruncateFile */
DLLIMP Bool TruncateFile(STRPTR file, uint64_t offset)
{
	BOOL   ret = FALSE;
	LPWSTR filew;

	utf8toutf16(file, filew);

	HANDLE fhd = CreateFileW(filew, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL, OPEN_EXISTING, 0, NULL);

	if (fhd != INVALID_HANDLE_VALUE)
	{
		DWORD curhi, curlo;
		LONG  low  = offset;
		LONG  high = offset >> 32;
		curlo = GetFileSize(fhd, &curhi);

		/* We do NOT want to enlarge the file */
		if ((((uint64_t) curhi << 32) | curlo) > offset)
		{
			SetFilePointer(fhd, low, &high, FILE_BEGIN);
			ret = SetEndOfFile(fhd);
		}
		CloseHandle(fhd);
	}
	return ret;
}

/* DOS/ExpandEnvVar */
DLLIMP STRPTR ExpandEnvVar(STRPTR str)
{
	LPWSTR wstr, dest;
	int    len;
	utf8toutf16(str, wstr);
	len = ExpandEnvironmentStrings(wstr, NULL, 0);
	if (len == 0) return NULL;
	dest = alloca(len * 2);
	ExpandEnvironmentStrings(wstr, dest, len);
	len = WideCharToMultiByte(CP_UTF8, 0, dest, -1, NULL, 0, NULL, NULL);
	str = malloc(len);
	WideCharToMultiByte(CP_UTF8, 0, dest, -1, str, len, NULL, NULL);
	return str;
}

DLLIMP void ExpandEnvVarBuf(STRPTR str, STRPTR utf8, int max)
{
	LPWSTR wstr, dest;
	int    len;
	utf8toutf16(str, wstr);
	len = ExpandEnvironmentStrings(wstr, NULL, 0);
	if (len > 0)
	{
		dest = alloca(len * 2);
		ExpandEnvironmentStrings(wstr, dest, len);
		WideCharToMultiByte(CP_UTF8, 0, dest, -1, utf8, max, NULL, NULL);
	}
	else utf8[0] = 0;
}

/*
 * List.c
 */

DLLIMP void ListAddHead(ListHead * head, ListNode * item)
{
	ListNode * first = head->lh_Head;

	if (first) first->ln_Prev = item;
	else       head->lh_Tail  = item;
	head->lh_Head = item;
	item->ln_Next = first;
	item->ln_Prev = NULL;
}

DLLIMP void ListAddTail(ListHead * head, ListNode * item)
{
	ListNode * last = head->lh_Tail;

	if (last) last->ln_Next = item;
	else      head->lh_Head = item;
	head->lh_Tail = item;
	item->ln_Prev = last;
	item->ln_Next = NULL;
}


/* List/ListInsert */
DLLIMP void ListInsert(ListHead * head, ListNode * item, ListNode * insert_after)
{
	ListNode * next;
	if (insert_after == NULL)
	{
		/* Add Head */
		next = head->lh_Head;
		head->lh_Head = item;
	}
	else /* Add Tail ~ Middle */
	{
		next = insert_after->ln_Next;
		insert_after->ln_Next = item;
	}
	if (next) next->ln_Prev = item;
	else      head->lh_Tail = item;
	item->ln_Next = next;
	item->ln_Prev = insert_after;
}


/* List/ListInsertSort, List/ListSort */
DLLIMP void ListInsertSort(ListHead * head, ListNode * item, ListSortFunc cb)
{
	ListNode * ins;
	ListNode * prev;

	for (prev = NULL, ins = head->lh_Head; ins && cb(ins, item) < 0;
	     prev = ins,  ins = ins->ln_Next);

	ListInsert(head, item, prev);
}

/* insert sort, should be enough for small list */
DLLIMP void ListSort(ListHead * head, ListSortFunc cb)
{
	ListNode * node;
	ListNode * next;
	ListHead   hdr;

	memset(&hdr, 0, sizeof hdr);

	for (node = head->lh_Head; node; node = next)
	{
		next = node->ln_Next;
		ListInsertSort(&hdr, node, cb);
	}
	memcpy(head, &hdr, sizeof hdr);
}

/* List/ListRemove */
DLLIMP void ListRemove(ListHead * head, ListNode * item)
{
	ListNode * node;

	node = item->ln_Prev;
	if (node) node->ln_Next = item->ln_Next;
	else      head->lh_Head = item->ln_Next;

	node = item->ln_Next;
	if (node) node->ln_Prev = item->ln_Prev;
	else      head->lh_Tail = item->ln_Prev;
}

/* List/ListRemHead, List/ListRemTail */
DLLIMP ListNode * ListRemHead(ListHead * head)
{
	ListNode * node = head->lh_Head;

	if (node)
	{
		ListNode * next = node->ln_Next;
		head->lh_Head = next;
		if (next) next->ln_Prev = NULL;
		else      head->lh_Tail = NULL;
	}
	return node;
}

DLLIMP ListNode * ListRemTail(ListHead * head)
{
	ListNode * node = head->lh_Tail;

	if (node)
	{
		ListNode * prev = node->ln_Prev;
		head->lh_Tail = prev;
		if (prev) prev->ln_Next = NULL;
		else      head->lh_Head = NULL;
	}
	return node;
}

/*
 * String.c
 */

/* String/CopyString */
DLLIMP void CopyString(STRPTR destination, STRPTR source, int max)
{
	STRPTR d = destination;
	STRPTR s = source;
	int    i = max;

	if (s == NULL) s = "";

	while (i > 0 && (*d = *s++)) i --, d ++;

	if (i   > 0) d[ 0] = 0; else
	if (max > 0) d[-1] = 0;
}

DLLIMP int StrCat(STRPTR dest, int max, int pos, STRPTR cat)
{
	int i;
	/* go to end of string */
	for (i = pos; dest[i]; i ++);

	/* copy as many char we can from <cat> */
	for ( ; *cat && i < max; dest[i] = *cat ++, i ++);

	if (i == max) i --;
	dest[i] = 0;
	return i;
}

void SkipPrintfArgs(STRPTR fmt, va_list * args)
{
	STRPTR p, type;

	for (p = fmt; *p; p ++)
	{
		int size;
		if (p[0] != '%') continue;
		if (p[1] == '%') { p ++; continue; }

		for (size = 0; *p && ! isalpha(*p); p ++);
		if (*p == 'h') p ++;
		if (*p == 'h') p ++;
		if (*p == 'l') p ++, size ++;
		if (*p == 'l') p ++, size ++;
		if (*p == 'L') p ++, size += 2;
		if (size > 2)  size = 2;

		type = strchr("1diuoxXcC2feEgG3sSpn", *p);

		if (type == NULL) return; /* Unrecognized format - not good */
		if (*p == 'c' || *p == 'C') size = 0;

		while (! isdigit(*type)) type --;
		switch (*type) {
		case '1':
			switch (size) {
			case 0: va_arg(*args, int); break;
			case 1: va_arg(*args, long); break;
			case 2: va_arg(*args, long long);
			}
			break;
		case '2':
			switch (size) {
			case 0:
			case 1: va_arg(*args, double); break;
			case 2: va_arg(*args, long double);
			}
			break;
		case '3':
			va_arg(*args, void *);
		}
	}
}

/* String/RawDoFmt */
STRPTR RawDoFmt(STRPTR fmt, va_list * args)
{
	int     max = 256, length = 0;
	STRPTR  ret = malloc(max);

	while (ret)
	{
		length = vsnprintf(ret, max, fmt, *args);

		/* Okay, return value of snprintf is somewhat system dependant though
		 * standard says that it should return the number of bytes that would
		 * have been written whatever size of output buffer is.
		 */
		if (length < 0 || length >= max - 1)
		{
			/* Seems not enough large */
			STRPTR newstr = realloc(ret, max += 256);

			if (newstr == NULL) { free(ret); return NULL; }

			ret = newstr;
		}
		else break;
	}
	if (++ length < max)
	{
		STRPTR newstr = realloc(ret, max);

		if (newstr) ret = newstr;
	}
	SkipPrintfArgs(fmt, args);
	return ret;
}


/* String/FindInList */
DLLIMP int FindInList(STRPTR list, STRPTR word, int len)
{
	int pos = 0;

	if (list == NULL || word == NULL) return -1;

	if (len <= 0)
		len = strlen(word);

	while (list)
	{
		if (strncasecmp(list, word, len) == 0 && (list[len] == ',' || list[len] == 0))
			return pos;

		pos ++;
		list = strchr(list, ',');
		if (list) list ++;
	}
	return -1;
}

/* String/NextLine */
DLLIMP STRPTR NextLine(STRPTR start, Bool cut)
{
	STRPTR p;
	TEXT eol;

	if (start == NULL) return NULL;
	for (p = start; *p && *p != '\n' && *p != '\r'; p ++);
	if (*p == 0) return NULL;   eol = *p;
	if (cut) *p = 0;  p ++;
	if (eol == '\r' && *p == '\n') p ++;
	return p;
}

/* String/StripCRLF */
DLLIMP STRPTR StripCRLF(STRPTR in)
{
	STRPTR eol = strchr(in, 0);

	while (eol > in && (eol[-1] == '\r' || eol[-1] == '\n')) *--eol = 0;

	return in;
}

/* String/Split */
DLLIMP int Split(STRPTR * table, STRPTR str, int max, int chr)
{
	STRPTR p, next;
	Bool   cutlast = max > 0;
	int    nb;

	if (max == 0) return 0;
	if (str == NULL)
	{
		memset(table, 0, max * sizeof *table);
		return 0;
	}
	if (!cutlast) max = - max;

	for (p = str, nb = 1, table[0] = p; *p && nb < max; p = next, table[nb] = p, nb ++)
	{
		for (next = p; *next && *next != chr; next ++);
		if (*next) *next++ = 0;
		else break;
	}
	if (cutlast && nb == max && (p = strchr(p, chr)))
		*p = 0;

	/* NULL remaining fields */
	for (chr = nb; chr < max; table[chr] = NULL, chr ++);

	return nb;
}

/* String/StrCount */
DLLIMP int StrCount(STRPTR list, int chr)
{
	STRPTR p;
	int    n;

	if (list == NULL) return 0;
	for (n = 0, p = list; *p; p ++)
		if (*p == (TEXT) chr) n ++;

	return n;
}

/*
 * Thread.c
 */


/* Thread/MutexCreate */
DLLIMP Mutex MutexCreate(void)
{
	LPCRITICAL_SECTION cs = malloc(sizeof *cs);

	if (cs) InitializeCriticalSection(cs);

	return cs;
}

/* Thread/MutexEnter, Thread/MutexLeave, Thread/MutexTryEnter */
DLLIMP void MutexEnter(Mutex lock)
{
	if (lock) EnterCriticalSection(lock);
}

DLLIMP void MutexLeave(Mutex lock)
{
	if (lock) LeaveCriticalSection(lock);
}

DLLIMP Bool MutexTryEnter(Mutex lock)
{
	if (lock)
	{
		return TryEnterCriticalSection(lock);
	}
	return False;
}

/* Thread/MutexDestroy  */
DLLIMP void MutexDestroy(Mutex lock)
{
	if (lock)
	{
		DeleteCriticalSection(lock);
		free(lock);
	}
}

/* Thread/ThreadCreate */
DLLIMP Thread ThreadCreate(ThreadCb func, APTR arg)
{
	uintptr_t tid = _beginthread(func, 0, arg);

	return tid;
}

/* Thread/SemInit */
DLLIMP Semaphore SemInit(int count)
{
	return (Semaphore) CreateSemaphore(NULL, count, 32767, NULL);
}

/* Thread/SemWait, Thread/SemWaitTimeout */
DLLIMP Bool SemWait(Semaphore s)
{
	return WaitForSingleObject((HANDLE) s, INFINITE) == WAIT_OBJECT_0;
}

DLLIMP Bool SemWaitTimeout(Semaphore s, ULONG ms)
{
	return WaitForSingleObject((HANDLE) s, ms) == WAIT_OBJECT_0;
}

/* Thread/SemAdd */
DLLIMP void SemAdd(Semaphore s, int count)
{
	ReleaseSemaphore((HANDLE) s, count, NULL);
}

/* Thread/SemClose */
DLLIMP void SemClose(Semaphore s)
{
	CloseHandle(s);
}

/* Thread/ThreadPause */
DLLIMP void ThreadPause(int delay)
{
	Sleep(delay);
}


DLLIMP float RandRange(float min, float max)
{
	return (max - min) * rand() * (1.0 / RAND_MAX) + min;
}

/*
 * Encodings.c
 */

/* ENC/NthChar */
DLLIMP STRPTR NthChar(STRPTR str, int nth)
{
	while (nth > 0)
	{
		unsigned char cp = *str;
		int n = 1;
		if ((cp & 0xe0) == 0xc0) n = 2; else
		if ((cp & 0xf0) == 0xe0) n = 3; else
		if ((cp & 0xf8) == 0xf0) n = 4;
		while (*str && n) str ++, n --;
		if (*str == 0) return str;
		nth --;
	}
	return str;
}

DLLIMP int UTF8ToUTF16(STRPTR out, int max, STRPTR in, int len)
{
	return MultiByteToWideChar(CP_UTF8, 0, in, len, (LPWSTR) out, max);
}

DLLIMP int UTF16ToUTF8(STRPTR out, int max, STRPTR in, int len)
{
	len = WideCharToMultiByte(CP_UTF8, 0, (LPWSTR) in, len, out, max, NULL, NULL);

	if (len < max) out[len] = 0;

	return len;
}

/* str must be encoded in UTF-16 */
DLLIMP int StrToLower16(STRPTR str, int len)
{
	return CharLowerBuffW((LPWSTR)str, len);
}

DLLIMP int StrToUpper16(STRPTR str, int len)
{
	return CharUpperBuffW((LPWSTR)str, len);
}

/* only take care of ASCII range */
DLLIMP int StrToLower(STRPTR str, int len)
{
	STRPTR p;
	for (p = str; len != 0 && *p; p ++, len --)
		if ('A' <= *p && *p <= 'Z') *p += 32;
	return p - str;
}

/*
 * vector.c : simple, yet efficient vector class
 */

DLLIMP APTR vector_nth(vector v, int i)
{
	int size;

	if (i >= v->max)
	{
		/* Table not large enough: round i to nearest power of 2 */
		size = i;
		size |= size >> 1;
		size |= size >> 2;
		size |= size >> 4;
		size |= size >> 8;
		size ++;
		if (size < 2) size = 2;

		STRPTR buffer = realloc(v->buffer, v->itemsize * size);
		if (! buffer) return NULL;
		v->buffer = buffer;
		/* Clear memory */
		if (v->clearwith)
		{
			int nb = size - v->max;
			buffer += v->max * v->itemsize;
			if (v->clearwith == VECTOR_ZERO_FILL)
			{
				memset(buffer, 0, v->itemsize * nb);
			}
			else while (nb > 0)
			{
				memcpy(buffer, v->clearwith, v->itemsize), nb --, buffer += v->itemsize;
			}
		}
		v->max = size;
	}
	if (i >= v->count)
		v->count = i + 1;
	return v->buffer + i * v->itemsize;
}

/*
 * INIFile: not required by SIT, but useful in a lot of applications
 */

/* Type is private */
struct INIFile_t
{
	STRPTR   buffer;
	STRPTR * curkey;
	int      size;
	STRPTR   keys[1];
};

DLLIMP INIFile ParseINI(STRPTR file)
{
	if (file == NULL) return NULL;

	FILE *  in;
	int     sz = FileSize(file);
	STRPTR  buf;

	if (sz > 0)
	{
		buf = malloc(sz + 1);

		if (buf)
		{
			in = fopen(file, "rb");

			if (in)
			{
				if (fread(buf, 1, sz, in) == sz)
				{
					fclose(in);
					buf[sz] = 0;
					INIFile ini = ParseINIStr(buf);
					if (ini) ini->buffer = buf;
					return ini;
				}
				else fclose(in);
			}
		}
	}
	return NULL;
}

static STRPTR ParseINILine(STRPTR line)
{
	STRPTR value = strchr(line, '=');
	STRPTR prev  = value - 1;

	StripCRLF(line);

	if (value == NULL || strchr("#;", *line)) return NULL;

	while (prev > line && isspace(*prev)) prev --; prev[1] = 0;
	for (value ++; *value && isspace(*value); value ++);

	if (*value == '\"')
	{
		value ++;
		for (prev = value; prev[0] != '\"' || prev[-1] == '\\'; prev ++)
		{
			if (prev[0] == '\\')
				strcpy(prev, prev + 1);
		}
		if (prev) *prev = 0;
	}
	else /* Check for trailing spaces or comment */
	{
		prev = strchr(value, ';');
		if (prev == NULL) prev = strchr(value, 0);
		for (prev --; prev > value && isspace(*prev); prev --);
		prev[1] = 0;
	}
	return value;
}

DLLIMP INIFile ParseINIStr(STRPTR buf)
{
	STRPTR * keyVal;
	STRPTR   buffer[17];
	STRPTR   file, last;
	int      i, total;

	for (keyVal = buffer, total = i = 0, last = file = buf; file; )
	{
		STRPTR next  = NextLine(file, True);
		STRPTR value = ParseINILine(file);

		if (value)
		{
			/* key/value pair */
			keyVal[i] = file;  i ++;
			keyVal[i] = value; i ++;
			total += 2;
		}
		else if (file[0] == '[' && (value = strrchr(file, ']')))
		{
			/* section header */
			keyVal[i] = file+1; i ++;
			keyVal[i] = NULL;   i ++;
			total += 2;
			*value = 0;
		}
		if (i == 16)
		{
			keyVal[16] = alloca(sizeof buffer);
			keyVal     = (STRPTR *) keyVal[16];
			i          = 0;
		}
		last = file;
		file = next;
	}
	if (total > 0)
	{
		STRPTR * dest;
		INIFile  ini = calloc(sizeof *ini + total * sizeof (STRPTR), 1);

		if (ini)
		{
			ini->size = strchr(last, 0) + 1 - buf;

			for (keyVal = buffer, dest = ini->keys, i = total; i > 0; )
			{
				int count = i > 16 ? 16 : i;
				memcpy(dest, keyVal, count * sizeof (STRPTR));
				dest += count;
				i    -= count;
				keyVal = (STRPTR *) keyVal[16];
			}
			*dest = NULL;
			return ini;
		}
	}
	return NULL;
}

/* INI/FreeINI  */
DLLIMP void FreeINI(INIFile ini)
{
	if (ini)
	{
		if (ini->buffer) free(ini->buffer);
		free(ini);
	}
}

DLLIMP Bool IterINI(INIFile ini, STRPTR * key, STRPTR * value)
{
	if (ini == NULL) return False;
	if (ini->keys[0])
	{
		if (ini->curkey == NULL)
		{
			*key   = ini->keys[0];
			*value = ini->keys[1];
			ini->curkey = ini->keys + 2;
		}
		else if (ini->curkey[0])
		{
			*key   = ini->curkey[0];
			*value = ini->curkey[1];
			ini->curkey += 2;
		}
		else /* End of file */
		{
			ini->curkey = NULL;
			return False;
		}
		return True;
	}
	else ini->curkey = NULL;
	return False;
}

DLLIMP void IterINIReset(INIFile ini)
{
	if (ini) ini->curkey = NULL;
}

DLLIMP STRPTR GetINIValue(INIFile ini, STRPTR section)
{
	STRPTR sub, key, value;
	int    length;
	Bool   sec;

	if (ini == NULL) return NULL;

	sub = strchr(section, '/');
	sec = (sub == NULL);
	if (sub) length = sub - section, sub ++;
	else     length = 0;

	while (IterINI(ini, &key, &value))
	{
		if (sub && value == NULL)
		{
			sec = strncasecmp(section, key, length) == 0 && key[length] == 0;
		}
		else if (value && sec && strcasecmp(key, sub ? sub : section) == 0)
		{
			ini->curkey = NULL;
			return value;
		}
	}
	return NULL;
}

DLLIMP int GetINIValueInt(INIFile ini, STRPTR key, int def)
{
	STRPTR value = GetINIValue(ini, key);

	if (value)
	{
		long val = strtol(value, &key, 10);

		if (key > value) return val;
	}
	return def;
}

DLLIMP STRPTR GetINIValueStr(INIFile ini, STRPTR key, STRPTR def)
{
	STRPTR value = GetINIValue(ini, key);

	return value ? value : def;
}

static Bool HasQuotes(STRPTR value)
{
	if (isspace(value[0]) || strchr(value, ';')) return True;

	STRPTR end = strchr(value, 0) - 1;

	return end >= value && isspace(*end);
}

DLLIMP Bool SetINIValue(STRPTR path, STRPTR key, STRPTR val)
{
	INIFile ini = ParseINI(path);
	STRPTR  sub = strchr(key, '/');
	Bool    esc = HasQuotes(val);
	FILE *  fh;
	int     lg;

	if (ini)
	{
		STRPTR value = GetINIValue(ini, key);
		int    flags = 0;

		if (value)
		{
			/* Key exists in file, overwrite value */
			if (strcmp(val, value) == 0)
			{
				/* Same value than currently stored : don't change anything */
				FreeINI(ini);
				return True;
			}
			flags = 2;
		}
		else if (sub) /* File exists but key do not : check if section exists */
		{
			STRPTR section;
			lg = sub - key;
			while (IterINI(ini, &section, &value))
			{
				if (value == NULL && strncasecmp(section, key, lg) == 0 && section[lg] == 0)
					break;
			}

			if (ini->curkey) /* Section exists, only add key/value after it */
			{
				value = ini->curkey[0];
				flags = 3;
			}
			else /* Add section, key and value at end of file */
				flags = 7, value = ini->buffer + ini->size;
		}
		else /* No section, only key name : add ket/value at beginning of file */
			flags = 3, value = ini->buffer;

		if (value == NULL)
		{
			/* No insertion point ? */
			FreeINI(ini);
			return False;
		}
		else if (flags > 0)
		{
			fh = fopen(path, "rb+");
			lg = strlen(value);

			if (fh == NULL) { FreeINI(ini); return False; }

			/* Optimize if only key has changed and same length */
			if (flags == 2 && strlen(val) == lg && (esc == False || value[-1] == '\"'))
			{
				fseek(fh, value - ini->buffer, SEEK_SET);
				fwrite(val, 1, strlen(val), fh);
			}
			else /* Need to shift file */
			{
				int diff = value - ini->buffer + (flags == 2 ? lg : 0);

				fseek(fh, diff, SEEK_SET);                 /* Set pointer at the end of old key */
				lg = fread(ini->buffer, 1, ini->size, fh); /* Read all up to the end */
				fseek(fh, value - ini->buffer, SEEK_SET);  /* Start of insertion point */
				if (flags & 4)
				{
					/* Add section name */
					fseek(fh, -1, SEEK_CUR);
					diff = fgetc(fh);
					fseek(fh, value - ini->buffer, SEEK_SET);
					if (strchr("\r\n", diff) == NULL) fputs("\r\n", fh);
					fputc('[', fh);
					fwrite(key, 1, sub - key, fh);
					fputs("]\r\n", fh);
				}
				if (flags & 1) fprintf(fh, "%s=", sub ? sub + 1 : key);
				sub = (esc ? "\"" : "");
				fprintf(fh, "%s%s%s%s", sub, val, sub, flags == 2 ? "" : "\r\n");
				fwrite(ini->buffer, 1, lg, fh);  /* Overwrite remaining */
				ftruncate(fileno(fh), ftell(fh));
			}
			fclose(fh);
		}
		FreeINI(ini);
		return True;
	}
	else /* File does not exist yet */
	{
		fh = fopen(path, "a");

		if (sub)
		{
			/* Add section/key/value */
			fputc('[', fh);
			fwrite(key, 1, sub - key, fh);
			fputs("]\r\n", fh);
			key = sub + 1;
		}
		sub = (esc ? "\"" : "");
		fprintf(fh, "%s=%s%s%s\r\n", key, sub, val, sub);
		fclose(fh);
	}
	return True;
}

DLLIMP Bool SetINIValueInt(STRPTR path, STRPTR key, int val)
{
	TEXT number[32];

	snprintf(number, sizeof number, "%d", val);

	return SetINIValue(path, key, number);
}

/*
 * UtilityLib/Lang
 */

typedef struct LangStack_t    LangStack;
typedef struct LangString_t   LangString;

struct LangStack_t
{
	STRPTR buffer;
	STRPTR file;
	STRPTR eol;
};

struct LangString_t
{
	STRPTR   message;
	uint32_t crc;
};

struct Lang_t
{
	int        stringsMax;
	LangStack  files[4];
	int        count, stack;
	LangString strings[0];
};

uint32_t crc32(uint32_t, DATA8, int);
static STRPTR LangExtractStr(STRPTR old, STRPTR buffer);

static STRPTR LangStackFile(Lang lang, STRPTR path, STRPTR eol)
{
	FILE * in;
	STRPTR rel      = lang->stack > 0 ? lang->files[lang->stack-1].file : NULL;
	ULONG  length   = strlen(path) + (rel ? strlen(rel) : 0) + 2;
	STRPTR fullpath = alloca(length);
	STRPTR buffer   = NULL;

	CopyString(fullpath, rel, length);
	ParentDir(fullpath);
	AddPart(fullpath, path, length);

	in = fopen_enc(fullpath, "rb");

	if (in)
	{
		LangStack * ls = lang->files + lang->stack;

		length = FileSize(fullpath);
		buffer = malloc(length + strlen(fullpath) + 2);
		buffer[length] = 0;
		fread(buffer, 1, length, in);
		fclose(in);

		ls->file = strcpy(buffer + length + 1, fullpath);
		ls->buffer = buffer;
		ls->eol = eol;
		lang->stack ++;
	}
	return buffer;
}

static STRPTR LangPopFile(Lang lang, STRPTR * start)
{
	if (lang->stack > 0)
	{
		LangStack * ls = lang->files + (-- lang->stack);
		STRPTR eol = ls->eol;

		/* move it to the end of table */
		lang->files[-- lang->count] = *ls;
		if (lang->stack > 0)
		{
			ls --;
			*start = ls->buffer;
			return eol;
		}
	}
	*start = NULL;
	return NULL;
}

#define MAXSTRS    128

DLLIMP Lang LangParse(STRPTR path)
{
	STRPTR * strings = NULL;
	STRPTR * last    = NULL;
	STRPTR   msgid   = NULL;
	STRPTR   msgstr  = NULL;
	STRPTR   line, eol, buffer;
	int      count, total;

	struct Lang_t lang = {.count = DIM(lang.files)};

	buffer = LangStackFile(&lang, path, NULL);
	/* Usually missing file */
	if (buffer == NULL) return NULL;

	eol = NULL;
	total = count = 0;

	while (buffer)
	{
		for (line = eol ? eol : buffer; line; line = eol)
		{
			eol = NextLine(line, True);

			if (*line == '#')
			{
				if (strncasecmp(line + 1, "include ", 8) == 0)
				{
					buffer = LangStackFile(&lang, line + 9, eol);

					if (buffer) eol = buffer;
				}
				continue;
			}

			if (strncasecmp(line, "msgid ", 6) == 0)
			{
				if (IsDef(msgstr) && IsDef(msgid))
				{
					/* reuse some bytes from file buffer */
					int diff;

					flushstr:
					diff = msgstr - msgid;
					/* offset to get translated message from original message (only need to save 1 ptr) */
					msgid[-1] = diff & 0xff;
					msgid[-2] = diff >> 8;
					/* offset to next slot for chaining */
					msgstr[-1] = msgstr[-2] = 0;
					if (count == 0 || count == MAXSTRS-1)
					{
						STRPTR * table = alloca(sizeof *table * MAXSTRS);
						if (last) last[MAXSTRS-1] = (STRPTR) table;
						last = table;
						count = 0;
						if (strings == NULL) strings = last;
					}
					last[count++] = msgid;
					total ++;
				}
				msgstr = msgid = NULL;
				msgid  = LangExtractStr(msgid, line);
			}
			else if (strncasecmp(line, "msgstr ", 7) == 0)
			{
				msgstr = LangExtractStr(msgstr, line);
			}
			else if (msgstr)
			{
				msgstr = LangExtractStr(msgstr, line);
			}
			else if (msgid)
			{
				/* continuation of msg ID */
				msgid = LangExtractStr(msgid, line);
			}
		}
		if (IsDef(msgstr) && IsDef(msgid))
			goto flushstr;

		eol = LangPopFile(&lang, &buffer);
	}

	static uint16_t primes[] = {
		23, 53, 97, 149, 193, 251, 307, 353, 401, 457, 769, 1543, 3079, 6151, 12289, 24593
	};

	/* get nearest prime number */
	for (count = 0; count < DIM(primes); count ++)
	{
		if (primes[count] > total) {
			lang.stringsMax = primes[count];
			break;
		}
	}

	/* now we can alloc a proper hash table */
	Lang ret = calloc(sizeof *ret + lang.stringsMax * sizeof *ret->strings, 1);

	*ret = lang;

	for (lang.count = count = 0; count < total; count ++)
	{
		STRPTR   str = *strings;
		uint32_t crc = crc32(0, (DATA8) str, strlen(str));
		int      slot = crc % lang.stringsMax;

		LangString * langStr = ret->strings + slot;

		if (langStr->message)
		{
			/* crude hash table: grab next available slot and chain text */
			LangString * prev = langStr;
			do
			{
				langStr ++;
				slot ++;
				if (slot == lang.stringsMax)
					slot = 0, langStr = ret->strings;
			}
			while (langStr->message);

			/* chain message */
			DATA8 link = (DATA8) prev->message;
			DATA8 tr   = (DATA8) str + str[-1] + (str[-2] << 8);
			link += link[-1] + (link[-2] << 8);
			tr[-1] = link[-1];
			tr[-2] = link[-2];
			link[-1] = slot & 0xff;
			link[-2] = slot >> 8;
		}
		langStr->message = str;
		langStr->crc = crc;

		strings ++; lang.count ++;
		if (lang.count == MAXSTRS-1)
			/* yep, that's a "char ***" type */
			strings = *(STRPTR **)strings, lang.count = 0;
	}

	return ret;
}

static void UnescapeAntiSlash(STRPTR src)
{
	STRPTR token;

	for (token = src; *token; token ++)
	{
		if (*token != '\\') continue;
		STRPTR overwrite = token + 2;
		switch (token[1]) {
		case 'a':  *token = '\a'; break;
		case 'b':  *token = '\b'; break;
		case 't':  *token = '\t'; break;
		case 'n':  *token = '\n'; break;
		case 'v':  *token = '\v'; break;
		case 'f':  *token = '\f'; break;
		case 'r':  *token = '\r'; break;
		case 'x':  *token = strtoul(token + 2, &overwrite, 16); break;
		default:   *token = token[1];
		}
		strcpy(token + 1, overwrite);
	}
}

static STRPTR LangExtractStr(STRPTR old, STRPTR buffer)
{
	if (buffer == NULL) return NULL;
	STRPTR quote = strchr(buffer, '\"');
	STRPTR end;

	if (quote == NULL) return old;

	for (end = ++ quote; *end && *end != '\"'; end ++)
		if (*end == '\\' && end[1]) end ++;
	*end = 0;
	UnescapeAntiSlash(quote);
	if (old)
	{
		memmove(strchr(old, 0), quote, strlen(quote) + 1);
		return old;
	}
	return quote;
}

static STRPTR LangSearch(Lang lang, STRPTR msg)
{
	uint32_t crc = crc32(0, (DATA8) msg, strlen(msg));
	int      max = lang->stringsMax;
	int      pos = crc % max;

	LangString * langStr;

	for (langStr = lang->strings + pos; langStr->crc != crc; )
	{
		DATA8 tr = (DATA8) langStr->message;
		tr += tr[-1] | (tr[-2] << 8);
		pos = tr[-1] | (tr[-2] << 8);
		if (pos == 0) return msg;
		langStr = lang->strings + pos;
	}

	DATA8 str = (DATA8) langStr->message;
	return (STRPTR) str + (str[-1] | (str[-2] << 8));
}

DLLIMP STRPTR LangStr(Lang lang, STRPTR msg, int max)
{
	STRPTR trans = NULL;

	if (lang)
	{
		trans = LangSearch(lang, msg);

		/* Hmmm, hack -- but whatever ... */
		if (trans == NULL && max > 0)
		{
			if (isupper(msg[0]))
			{
				msg[0] = tolower(msg[0]);
				trans  = LangSearch(lang, msg);

				if (trans) CopyString(msg, trans, max);

				msg[0] = toupper(msg[0]);
			}
			else if (islower(msg[0]))
			{
				msg[0] = toupper(msg[0]);
				trans  = LangSearch(lang, msg);

				if (trans) CopyString(msg, trans, max);

				msg[0] = tolower(msg[0]);
			}
			return msg;
		}
	}
	return trans ? trans : msg;
}

// XXX Hmm could be improved
DLLIMP STRPTR LangStrPlural(Lang lang, int nb, STRPTR sing, STRPTR plur)
{
	if (nb == 1) return LangStr(lang, sing, 0);
	else         return LangStr(lang, plur, 0);
}

DLLIMP void LangFree(Lang lang)
{
	if (lang)
	{
		LangStack * ls;
		int i;

		for (ls = lang->files + DIM(lang->files) - 1, i = lang->count; i < DIM(lang->files); free(ls->buffer), i ++, ls --);
		free(lang);
	}
}
