/*
 * stb_image_win32.h: somewhat drop in replacement for std_image.h, but using windows API (gdiplus) to
 *                    load bitmap data. It usually saves about 35 Kb of binary size.
 *
 * Written by T.Pierron, aug 2021.
 */

#ifndef STB_IMAGE_WIN32_H
#define STB_IMAGE_WIN32_H

#include <stdint.h>
#include <stdio.h>

#ifndef DLLIMP
#define DLLIMP
#endif

/*
 * req_comp can only have 0, 1, 3 or 4 as value. 2 is not supported.
 */

DLLIMP uint8_t * stbi_load(const char * filename, int * x, int * y, int * comp, int req_comp);
DLLIMP uint8_t * stbi_load_from_memory(const uint8_t * buffer, int len, int * x, int * y, int * comp, int req_comp);
DLLIMP unsigned char * stbi_load_from_file(FILE * f, int *x, int *y, int *comp, int req_comp);

#define stbi_image_free(retval_from_stbi_load) \
        free(retval_from_stbi_load)

/*
 * part of the API, but does nothing
 */
#define stbi_set_unpremultiply_on_load(x)
#define stbi_convert_iphone_png_to_rgb(x)
#define stbi_set_flip_vertically_on_load(x)


#ifdef STB_IMAGE_IMPLEMENTATION

#define UNICODE
#include <windows.h>
#include <malloc.h>

/*
 * gdiplus declarations: original header won't compile using a C compiler.
 * and besides, it removes a dependency on win32 SDK.
 */
typedef enum
{
   GpOk = 0,
   GpGenericError = 1,
   GpInvalidParameter = 2,
   GpOutOfMemory = 3,
   GpObjectBusy = 4,
   GpInsufficientBuffer = 5,
   GpNotImplemented = 6,
   GpWin32Error = 7,
   GpWrongState = 8,
   GpAborted = 9,
   GpFileNotFound = 10,
   GpValueOverflow = 11,
   GpAccessDenied = 12,
   GpUnknownImageFormat = 13,
   GpFontFamilyNotFound = 14,
   GpFontStyleNotFound = 15,
   GpNotTrueTypeFont = 16,
   GpUnsupportedGdiplusVersion = 17,
   GpGdiplusNotInitialized = 18,
   GpPropertyNotFound = 19,
   GpPropertyNotSupported = 20
} GpStatus;

/* from GdiplusInit.h */

typedef enum
{
   GpDebugEventLevelFatal,
   GpDebugEventLevelWarning
} GpDebugEventLevel;

typedef VOID (WINAPI *GpDebugEventProc)(GpDebugEventLevel level, CHAR *message);

typedef GpStatus (WINAPI *GpNotificationHookProc)(ULONG_PTR *token);
typedef VOID (WINAPI *GpNotificationUnhookProc)(ULONG_PTR token);

typedef struct {
   UINT32 GdiplusVersion;
   GpDebugEventProc DebugEventCallback;
   BOOL SuppressBackgroundThread;
   BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
   GpNotificationHookProc NoticationHook;
   GpNotificationUnhookProc NotificatoinUnhook;
} GdiplusStartupOutput;

static HINSTANCE GdiplusDLL;
static GdiplusStartupOutput gpStartupOutput;

/* from GdiplusHeaders.h */
typedef void GpImage; /* opaque type */

typedef struct {
   GpImage* nativeImage;
   GpStatus lastResult;
   GpStatus loadStatus;
} GpBitmap;

/* from GdiplusTypes.h */
typedef struct {
   INT X;
   INT Y;
   INT Width;
   INT Height;
} GpRect;

/* from GdiplusPixelFormat.h */
typedef INT GpPixelFormat;
/*
 * In-memory pixel data formats:
 * bits 0-7 = format index
 * bits 8-15 = pixel size (in bits)
 * bits 16-23 = flags
 * bits 24-31 = reserved
 */
#define GpPixelFormatGDI         0x00020000 /* Is a GDI-supported format */
#define GpPixelFormatAlpha       0x00040000 /* Has an alpha component */
#define GpPixelFormatIndexed     0x00010000
#define GpPixelFormatCanonical   0x00200000
#define GpPixelFormat24bppRGB    (8  | (24 << 8) | GpPixelFormatGDI)
#define GpPixelFormat32bppARGB   (10 | (32 << 8) | GpPixelFormatAlpha | GpPixelFormatGDI | GpPixelFormatCanonical)
#define GpPixelFormat8bppIndexed (3  | ( 8 << 8) | GpPixelFormatIndexed | GpPixelFormatGDI)

/* from GdiplusImaging.h */
typedef struct {
   UINT Width;
   UINT Height;
   INT Stride;
   GpPixelFormat PixelFormat;
   VOID* Scan0;
   UINT_PTR Reserved;
} GpBitmapData;

typedef enum
{
   GpImageLockModeRead        = 0x0001,
   GpImageLockModeWrite       = 0x0002,
   GpImageLockModeUserInputBuf= 0x0004
} GpImageLockMode;


static long GpToken;
static HINSTANCE GdiplusDLL = (HINSTANCE) 1;
static HINSTANCE Ole32;
static GdiplusStartupOutput gpStartupOutput;

typedef GpStatus (WINAPI *GdiplusStartupProc)(ULONG_PTR *token, const GdiplusStartupInput* input, GdiplusStartupOutput* output);
typedef GpStatus (WINAPI *GdipCreateBitmapFromStreamProc)(IStream* stream, GpBitmap **bitmap);
typedef GpStatus (WINAPI *GdipCreateBitmapFromFileProc)(WCHAR* filename, GpBitmap **bitmap);
typedef GpStatus (WINAPI *GdipDisposeImageProc)(GpImage *image);
typedef GpStatus (WINAPI *GdipBitmapLockBitsProc)(GpBitmap* bitmap, const GpRect* rect, GpImageLockMode flags, GpPixelFormat format, GpBitmapData* lockedBitmapData);
typedef GpStatus (WINAPI *GdipBitmapUnlockBitsProc)(GpBitmap* bitmap, GpBitmapData* lockedBitmapData);
typedef GpStatus (WINAPI *GdipGetImagePixelFormatProc)(GpImage *image, GpPixelFormat *format);

typedef HRESULT  (WINAPI *CreateStreamOnHGlobalProc)(HGLOBAL hGlobal, BOOL fDeleteOnRelease, LPSTREAM *ppstm);

static GdiplusStartupProc GdiplusStartup;
static GdipCreateBitmapFromStreamProc GdipCreateBitmapFromStream;
static GdipDisposeImageProc GdipDisposeImage;
static GdipBitmapLockBitsProc GdipBitmapLockBits;
static GdipBitmapUnlockBitsProc GdipBitmapUnlockBits;
static GdipCreateBitmapFromFileProc GdipCreateBitmapFromFile;
static GdipGetImagePixelFormatProc GdipGetImagePixelFormat;
static CreateStreamOnHGlobalProc pCreateStreamOnHGlobal;


FARPROC GpFunc(char * str)
{
	FARPROC p = GetProcAddress(GdiplusDLL, str);
	if (p == NULL)
		/* if something doesn't load, bail */
		GdiplusDLL = 0;
	return p;
}

static void LoadGdiplus(void)
{
	GdiplusDLL = LoadLibrary(L"gdiplus.dll");
	Ole32 = LoadLibrary(L"ole32.dll");
	if (! GdiplusDLL) return;

	GdiplusStartup = (GdiplusStartupProc) GpFunc("GdiplusStartup");
	GdipCreateBitmapFromStream = (GdipCreateBitmapFromStreamProc)GpFunc("GdipCreateBitmapFromStream");
	GdipCreateBitmapFromFile = (GdipCreateBitmapFromFileProc)GpFunc("GdipCreateBitmapFromFile");
	GdipDisposeImage = (GdipDisposeImageProc)GpFunc("GdipDisposeImage");
	GdipBitmapLockBits = (GdipBitmapLockBitsProc)GpFunc("GdipBitmapLockBits");
	GdipBitmapUnlockBits = (GdipBitmapUnlockBitsProc)GpFunc("GdipBitmapUnlockBits");
	GdipGetImagePixelFormat = (GdipGetImagePixelFormatProc)GpFunc("GdipGetImagePixelFormat");

	if (GdiplusDLL)
	{
		GdiplusStartupInput gpStartupInput = {1, NULL, TRUE, FALSE};
		if (GdiplusStartup(&GpToken, &gpStartupInput, &gpStartupOutput) != GpOk)
			GdiplusDLL = 0;
	}

	if (Ole32)
	{
		pCreateStreamOnHGlobal = (CreateStreamOnHGlobalProc)GetProcAddress(Ole32, "CreateStreamOnHGlobal");
	}
}

DLLIMP uint8_t * stbi_load_from_memory(const uint8_t * mem, int len, int *x, int *y, int *n, int n_req)
{
	GpBitmap *    bitmap = NULL;
	GpBitmapData  data;
	GpPixelFormat pixelFormat;
	uint8_t *     ret;

	if (GdiplusDLL == (HINSTANCE) 1)
		LoadGdiplus();

	if (! GdiplusDLL)
		return NULL;

	*x = 0;
	*y = 0;
	data.Scan0 = NULL;

	if (len != 0)
	{
		HGLOBAL hMem;
		IStream * stream = NULL;

		if (len < 0)
		{
			/* mem is a (FILE *) actually */
			FILE * in = (FILE *) mem;
			fseek(in, 0, SEEK_END); len = ftell(in);
			fseek(in, 0, SEEK_SET); hMem = GlobalAlloc(GMEM_MOVEABLE, len);
			if (hMem)
			{
				uint8_t * buf = GlobalLock(hMem);
				if (buf)
				{
					fread(buf, 1, len, in);
					GlobalUnlock(hMem);
					goto image_read;
				}
				GlobalFree(hMem);
			}
		}
		else
		{
			/* read file from memory */
			hMem = GlobalAlloc(GMEM_MOVEABLE, len);
			if (hMem)
			{
				uint8_t * buf = GlobalLock(hMem);
				if (buf)
				{
					memcpy(buf, mem, len);
					GlobalUnlock(hMem);
					goto image_read;
				}
				GlobalFree(hMem);
			}
		}
		return NULL;

		image_read:
		if (pCreateStreamOnHGlobal(hMem, FALSE, &stream) == S_OK)
		{
			GpStatus ret = GdipCreateBitmapFromStream(stream, &bitmap);
			stream->lpVtbl->Release(stream);
			GlobalFree(hMem);
			if (ret == GpOk)
				goto image_parsed;
		}
		GlobalFree(hMem);
	}
	else /*  read from file */
	{
		/* convert from UTF-8 to UTF-16 */
		int len = MultiByteToWideChar(CP_UTF8, 0, mem, -1, NULL, 0);

		LPWSTR utf16 = alloca(len * sizeof *utf16);

		MultiByteToWideChar(CP_UTF8, 0, mem, -1, utf16, len);

		if (GdipCreateBitmapFromFile(utf16, &bitmap) != GpOk)
			return NULL;
	}

	image_parsed:

	if (n_req == 0)
	{
		/* try to infer the format */
		int srcFormat;
		GdipGetImagePixelFormat((GpImage *) bitmap, &srcFormat);
		if (srcFormat & GpPixelFormatAlpha)
			n_req = 4;
		else if (srcFormat & GpPixelFormatIndexed)
			n_req = 1;
		else /* note: must default to this */
			n_req = 3;
	}

	*n = n_req;
	pixelFormat = n_req == 3 ? GpPixelFormat24bppRGB :
	              n_req == 4 ? GpPixelFormat32bppARGB : GpPixelFormat8bppIndexed;

	if (GdipBitmapLockBits(bitmap, NULL, GpImageLockModeRead, pixelFormat, &data) == GpOk)
	{
		*x  = data.Width;
		*y  = data.Height;
		*n  = n_req;
		ret = malloc(data.Width * data.Height * n_req);
		if (ret)
		{
			uint8_t * s, * d;
			int stride = data.Width * n_req, i;
			for (i = data.Height, s = data.Scan0, d = ret; i > 0; i --, s += data.Stride, d += stride)
			{
				/* need to remove padding and byte order */
				uint8_t * s2, * d2;
				int j;
				if (n_req == 3)
				{
					for (j = data.Width, s2 = s, d2 = d; j > 0; j --, s2 += 3, d2 += 3)
						d2[0] = s2[2], d2[1] = s2[1], d2[2] = s2[0];
				}
				else if (n_req == 4)
				{
					for (j = data.Width, s2 = s, d2 = d; j > 0; j --, s2 += 4, d2 += 4)
						d2[0] = s2[2], d2[1] = s2[1], d2[2] = s2[0], d2[3] = s2[3];
				}
				else /* n_req == 1 */
				{
					memcpy(d, s, stride);
				}
			}
		}
		GdipBitmapUnlockBits(bitmap, &data);
	}
	else ret = NULL;
	GdipDisposeImage(bitmap);

	return ret;
}

DLLIMP uint8_t * stbi_load(const char * filename, int * x, int * y, int * comp, int req_comp)
{
	return stbi_load_from_memory((uint8_t *) filename, 0, x, y, comp, req_comp);
}

DLLIMP unsigned char * stbi_load_from_file(FILE * f, int *x, int *y, int *comp, int req_comp)
{
	return stbi_load_from_memory((uint8_t *) f, -1, x, y, comp, req_comp);
}

#endif /* STB_IMAGE_IMPLEMENTATION */
#endif
