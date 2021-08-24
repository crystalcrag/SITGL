/*
 * exampleWIN32.c : this example shows how to integrate SITGL using nothing more than WIN32 API.
 *                  obviously, this will only works on Windows (2000 or later).
 *
 * I wouldn't recommend using this as a starting base, but if you absolutely want to reduce the amount
 * of dependencies, to ship as little binaries as possible, and only care about Windows, then why not ?
 *
 * But if you are as hell bent as this to reduce binary overhead, you might as well go deeper into
 * that hell, and use the Win32 API directly.
 *
 * Note: using mingw, this is the command line used to compile this source (you'll need to copy SITGL.dll
 *       into the "examples" folder):
 * $ gcc exampleWIN32.c -Os -s -lopengl32 -lgdi32 SITGL.dll -o exampleWIN32.exe
 *
 * This application also use "windows7.css" as its stylesheet for controls, it should already be in the
 * "examples" folder.
 *
 * Written by T.Pierron, june 2020
 */

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/gl.h>
#include <stdio.h>
#include "SIT.h"

LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);

/* setting full screen and/or saving window size/position will be left as a exercise */
static SIT_Widget app;
static int        winWidth  = 1200;
static int        winHeight = 800;
static LPWSTR     winCursor;
static int        appExit;
static HDC        glHDC;

static int exitApp(SIT_Widget w, APTR cd, APTR ud)
{
	appExit = 1;
	return 1;
}

/* simpler than the SDL version, since we can use pre-defined win32 cursor */
static int setCursor(SIT_Widget w, APTR cd, APTR ud)
{
	/* bonus point: they are HiDPI aware */
	static LPWSTR cursors[] = {
		IDC_ARROW, IDC_SIZEWE, IDC_SIZENS, IDC_SIZENWSE, IDC_SIZEWE, 0, IDC_SIZENESW,
		0, IDC_SIZENS, IDC_SIZENESW, 0, 0, IDC_SIZENWSE
	};
	SIT_OnChange * msg = cd;
	if (msg->type == SIT_CHANGE_CURSOR)
		winCursor = cursors[msg->arg];
	return 1;
}

int WINAPI WinMain (HINSTANCE hThisInstance,
                    HINSTANCE hPrevInstance,
                    LPSTR lpszArgument,
                    int nCmdShow)
{
	HWND hwnd;
	MSG messages;
	WNDCLASSEX wincl;

	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = L"MyAppClass";
	wincl.lpfnWndProc = WindowProcedure;
	wincl.style = 0;
	wincl.cbSize = sizeof (WNDCLASSEX);

	/* Use default icon and mouse-pointer */
	wincl.hIcon = LoadIcon (NULL, IDI_APPLICATION);
	wincl.hIconSm = LoadIcon (NULL, IDI_APPLICATION);
	wincl.hCursor = LoadCursor(NULL, IDC_ARROW);
	wincl.lpszMenuName = NULL;
	wincl.cbClsExtra = 0;
	wincl.cbWndExtra = 0;
	wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;
	winCursor = IDC_ARROW;

	if (!RegisterClassEx (&wincl))
		return 0;

	/* of course, the width/height param refer to outer border */
	RECT rect = {.right = winWidth, .bottom = winHeight};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

	hwnd = CreateWindowEx(0, wincl.lpszClassName, L"Your Application Title", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, HWND_DESKTOP, NULL, hThisInstance, NULL);

	/* we have our window, create an OpenGL context */
	PIXELFORMATDESCRIPTOR pfd;
	int iFormat;

	glHDC = GetDC(hwnd);

	/* set the pixel format for the DC */
	memset(&pfd, 0, sizeof pfd);

	pfd.nSize = sizeof pfd;
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	iFormat = ChoosePixelFormat(glHDC, &pfd);

	SetPixelFormat(glHDC, iFormat, &pfd);

	/* create and enable the render context (RC) */
	HGLRC GLctx = wglCreateContext(glHDC);

	wglMakeCurrent(glHDC, GLctx);

	/* now we can init SITGL */
	app = SIT_Init(SIT_NVG_FLAGS, winWidth, winHeight, "examples/css/windows7.css", 1);

	if (app == NULL)
	{
		SIT_Log(SIT_ERROR, "could not initialize SITGL: %s.\n", SIT_GetError());
		return 1;
	}

	SIT_SetValues(app,
		SIT_RefreshMode, SITV_RefreshAsNeeded,
		SIT_AddFont,     "sans-serif",      "System",
		SIT_AddFont,     "sans-serif-bold", "System/Bold",
		SIT_ExitCode,    &appExit,
		NULL
	);
	SIT_AddCallback(app, SITE_OnChange, setCursor, NULL);

	/* create a bunch of widgets, to see if events are working */
	static char text[] =
		"This is some text that can be edited.\n\n"
		"Feel free to edit as you wish.\n\n"
		"More text to check for auto-scroll and scroll wheel.\n\n"
		"ABC DEF GHI JKL MNO PQR STU VWX YZ.\n\n"
		"012 345 678 90.\n\n"
		"The quick brown fox jumps over the lazy dog.\n\n"
		"\tTab indent text\n"
		"\t\tThere are 2 tabs before that text.\n\n"
		"\x01 \x02 \x03 \x04 \x05 \x06 \x07\n\n"
		"\x1b \x1c \x1d \x1e \x1f";

	SIT_Widget dialog = SIT_CreateWidget("mainwnd", SIT_DIALOG, app,
		SIT_Width,        SITV_Em(40),
		SIT_Title,        _("This is a movable window"),
		SIT_DialogStyles, SITV_Movable | SITV_Resizable,
		NULL
	);

	SIT_CreateWidgets(dialog,
		"<label name=hello title=", _("Hello, world!"), ">"
		"<editbox name=edit left=FORM right=FORM height=10em top=WIDGET,hello,0.5em title=", text, "editType=", SITV_Multiline, ">"
		"<button name=ok title=", _("This is a <b>Button</b>"), "bottom=FORM>"
		"<button name=ko title=", _("Exit"), "top=OPPOSITE,ok left=WIDGET,ok,1em>"
		"<progress name=prog progressPos=50 title='%d%%' width=10em left=WIDGET,ko,1em bottom=OPPOSITE,ok>"
		"<button name=check title='A checkbox' checkState=1 buttonType=", SITV_CheckBox, "left=WIDGET,prog,1em top=MIDDLE,ok>"
	);
	SIT_SetAttributes(dialog, "<edit bottom=WIDGET,ok,0.5em>");

	SIT_AddCallback(dialog, SITE_OnClose, exitApp, NULL);
	SIT_AddCallback(SIT_GetById(dialog, "ko"), SITE_OnActivate, exitApp, NULL);
	SIT_ManageWidget(dialog);

	ShowWindow(hwnd, nCmdShow);

	FrameSetFPS(40);
	while (! appExit)
	{
		/*
		 * SITGL requires an active event loop even if refresh mode is set to AsNeeded
		 * but that event loop should be very close to 0% CPU usage. If it is not, there is
		 * a problem somewhere.
		 */
		while (PeekMessage(&messages, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&messages);
			DispatchMessage(&messages);
		}

		if (SIT_RenderNodes(FrameGetTime()))
			SwapBuffers(glHDC);

		FrameWaitNext();
	}

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(GLctx);

	return 0;
}

static void HandlePaint(HWND hwnd)
{
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	if (SIT_RenderNodes(FrameGetTime()))
		SwapBuffers(glHDC);
	EndPaint(hwnd, &ps);
}

static void HandleKeyboard(int key, int flags)
{
	static uint8_t sit2vk[] = { /* must follow order of SITK_* enum */
		VK_HOME,   VK_END,     VK_PRIOR,    VK_NEXT,   VK_UP,      VK_DOWN,     VK_LEFT,
		VK_RIGHT,  VK_LSHIFT,  VK_RSHIFT,   VK_LMENU,  VK_RMENU,   VK_LCONTROL, VK_RCONTROL,
		VK_LWIN,   VK_RWIN,    VK_APPS,     VK_RETURN, VK_CAPITAL, VK_INSERT,   VK_DELETE,
		VK_SCROLL, VK_NUMLOCK, VK_SNAPSHOT, VK_F1,     VK_F2,      VK_F3,       VK_F4,
		VK_F5,     VK_F6,      VK_F7,       VK_F8,     VK_F9,      VK_F10,      VK_F11,
		VK_F12,    VK_F13,     VK_F14,      VK_F15,    VK_F16,     VK_F17,      VK_F18,
		VK_F19,    VK_F20,     VK_TAB,      VK_BACK,   VK_ESCAPE,  VK_SPACE,    VK_HELP,
		VK_SHIFT
	};

	int mod = 0;
	if (GetKeyState(VK_SHIFT) < 0)   mod |= SITK_FlagShift;
	if (GetKeyState(VK_CONTROL) < 0) mod |= SITK_FlagCtrl;
	if (GetKeyState(VK_MENU) < 0)    mod |= SITK_FlagAlt;

	if (flags)
	{
		/* rawkey */
		DATA8 ptr = memchr(sit2vk, key, sizeof sit2vk);

		if (ptr == NULL)
		{
			/* 96 through 111 are keypad message, we'll receive WM_CHAR instead */
			if (96 <= key && key <= 111)
				return;
		}
		else key = RAWKEY(ptr - sit2vk + 1);
	}
	else if (key == 8 || key == 13)
	{
		return;
	}

	if (key < 0x10FFFF && flags == 0)
		SIT_ProcessChar(key, mod);
	else
		SIT_ProcessKey(key, mod, (flags & SITK_FlagUp) == 0);
}

/* event processing function */
LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	int  param = 0;
	switch (message) {
	case WM_DESTROY:
		appExit = 1;
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		HandlePaint(hwnd);
		break;
	case WM_SIZE:
		GetClientRect(hwnd, &rect);
		winWidth = rect.right;
		winHeight = rect.bottom;
		glViewport(0, 0, winWidth, winHeight);
		SIT_ProcessResize(winWidth, winHeight);
		break;
	case WM_MOUSEMOVE:
		SIT_ProcessMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT)
			SetCursor(LoadCursor(NULL, winCursor));
		else
			DefWindowProc(hwnd, message, wParam, lParam);
		break;
	case WM_LBUTTONDOWN: param ++; // no break;
	case WM_LBUTTONUP:   param ++; // no break;
	case WM_RBUTTONDOWN: param ++; // no break;
	case WM_RBUTTONUP:   param ++; // no break;
	case WM_MBUTTONDOWN: param ++; // no break;
	case WM_MBUTTONUP:   param ++; // no break;
	{	static uint8_t buttons[] = {0, SITOM_ButtonMiddle, SITOM_ButtonMiddle, SITOM_ButtonRight, SITOM_ButtonRight, SITOM_ButtonLeft, SITOM_ButtonLeft};
		static uint8_t pressed[] = {0, 0, 1, 0, 1, 0, 1};
		static uint8_t capture;
		SIT_ProcessClick(LOWORD(lParam), HIWORD(lParam), buttons[param], pressed[param]);
		if (pressed[param])
			SetCapture(hwnd), capture = 1;
		else if (capture)
			ReleaseCapture(), capture = 0;
	}	break;
	case WM_MOUSEWHEEL:
		SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &param, 0);
		if (param >= 0)
		{
			static int scrollWheel;

			param *= (int16_t) HIWORD(wParam);

			/* If scroll change direction, reset accumulator */
			if (param * scrollWheel >= 0)
				param += scrollWheel;

			scrollWheel = param % WHEEL_DELTA;

			/* Not enough accumulated to scroll */
			if (param / WHEEL_DELTA != 0)
				SIT_ProcessClick(LOWORD(lParam), HIWORD(lParam), param < 0 ? SITOM_ButtonWheelDown : SITOM_ButtonWheelUp, 1);
		}
		break;
	case WM_CHAR:    param = 0;                goto case_WM_KEY;
	case WM_KEYDOWN: param = SITK_FlagCapture; goto case_WM_KEY; /* not really a capture, but want a harmless value that is not 0 */
	case WM_KEYUP:   param = SITK_FlagUp;
	case_WM_KEY:
		HandleKeyboard(wParam, param);
		break;
	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
    return 0;
}
