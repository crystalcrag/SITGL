/*
 * exampleSDL.c : small example showing how to integrate SITGL with SDL
 *
 * Written by T.Pierron, oct 2020
 */

#include <stdio.h>
#include <SDL/SDL.h>
#include <GL/GL.h>
#include "nanovg.h"
#include <malloc.h>
#include <math.h>
#include <time.h>
#include "SIT.h"

static int SDLKtoSIT[] = {
	SDLK_HOME,      SITK_Home,
	SDLK_END,       SITK_End,
	SDLK_PAGEUP,    SITK_PrevPage,
	SDLK_PAGEDOWN,  SITK_NextPage,
	SDLK_UP,        SITK_Up,
	SDLK_DOWN,      SITK_Down,
	SDLK_LEFT,      SITK_Left,
	SDLK_RIGHT,     SITK_Right,
	SDLK_LSHIFT,    SITK_LShift,
	SDLK_RSHIFT,    SITK_RShift,
	SDLK_LAST,      SITK_LAlt,
	SDLK_RALT,      SITK_RAlt,
	SDLK_LCTRL,     SITK_LCtrl,
	SDLK_RCTRL,     SITK_RCtrl,
	SDLK_LSUPER,    SITK_LCommand,
	SDLK_RSUPER,    SITK_RCommand,
	SDLK_MENU,      SITK_AppCommand,
	SDLK_RETURN,    SITK_Return,
	SDLK_CAPSLOCK,  SITK_Caps,
	SDLK_INSERT,    SITK_Insert,
	SDLK_DELETE,    SITK_Delete,
	SDLK_NUMLOCK,   SITK_NumLock,
	SDLK_PRINT,     SITK_Impr,
	SDLK_F1,        SITK_F1,
	SDLK_F2,        SITK_F2,
	SDLK_F3,        SITK_F3,
	SDLK_F4,        SITK_F4,
	SDLK_F5,        SITK_F5,
	SDLK_F6,        SITK_F6,
	SDLK_F7,        SITK_F7,
	SDLK_F8,        SITK_F8,
	SDLK_F9,        SITK_F9,
	SDLK_F10,       SITK_F10,
	SDLK_F11,       SITK_F11,
	SDLK_F12,       SITK_F12,
	SDLK_F13,       SITK_F13,
	SDLK_F14,       SITK_F14,
	SDLK_F15,       SITK_F15,
	SDLK_BACKSPACE, SITK_BackSpace,
	SDLK_ESCAPE,    SITK_Escape,
	SDLK_SPACE,     SITK_Space,
	SDLK_HELP,      SITK_Help,
};

static int SDLMtoSIT(int mod)
{
	int ret = 0;
	if (mod & KMOD_CTRL)  ret |= SITK_FlagCtrl;
	if (mod & KMOD_SHIFT) ret |= SITK_FlagShift;
	if (mod & KMOD_ALT)   ret |= SITK_FlagAlt;
	return ret;
}

/* change shape of cursor based on corner hovered on a resizable dialog */
static int setCursor(SIT_Widget w, APTR cd, APTR ud)
{
	/* honestly, it is not worth it */
	static uint8_t arrowData[360] = {
		0x22,0x88, /* SW-NE */
		0x00, 0xff, 0x00, 0x41, 0x00, 0x21, 0x00, 0x41, 0x00, 0x81, 0x01, 0x05, 0x02, 0x0b, 0x04, 0x11,
		0x88, 0x20, 0xd0, 0x40, 0xa0, 0x80, 0x81, 0x00, 0x82, 0x00, 0x84, 0x00, 0x82, 0x00, 0xff, 0x00,
		0x00, 0xff, 0x00, 0x7f, 0x00, 0x3f, 0x00, 0x7d, 0x00, 0xff, 0x01, 0xff, 0x03, 0xfb, 0x07, 0xf1,
		0x8f, 0xe0, 0xdf, 0xc0, 0xff, 0x80, 0xff, 0x00, 0xfe, 0x00, 0xfc, 0x00, 0xfe, 0x00, 0xff, 0x00,

		0x22,0x88, /* NE-SW */
		0xff, 0x00, 0x82, 0x00, 0x84, 0x00, 0x82, 0x00, 0x81, 0x00, 0xa0, 0x80, 0xd0, 0x40, 0x88, 0x20,
		0x04, 0x11, 0x02, 0x0b, 0x01, 0x05, 0x00, 0x81, 0x00, 0x41, 0x00, 0x21, 0x00, 0x41, 0x00, 0xff,
		0xff, 0x00, 0xfe, 0x00, 0xfc, 0x00, 0xfe, 0x00, 0xff, 0x00, 0xff, 0x80, 0xdf, 0xc0, 0x8f, 0xe0,
		0x07, 0xf1, 0x03, 0xfb, 0x01, 0xff, 0x00, 0xff, 0x00, 0x7f, 0x00, 0x3f, 0x00, 0x7f, 0x00, 0xff,

		0x32,0x87, /* N-S */
		0x01, 0x80, 0x02, 0x40, 0x04, 0x20, 0x08, 0x10, 0x10, 0x08, 0x20, 0x04, 0x7c, 0x3e, 0x04, 0x20,
		0x04, 0x20, 0x04, 0x20, 0x04, 0x20, 0x04, 0x20, 0x04, 0x20, 0x04, 0x20, 0x7c, 0x3e, 0x20, 0x04,
		0x10, 0x08, 0x08, 0x10, 0x04, 0x20, 0x02, 0x40, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x80, 0x03, 0xc0, 0x07, 0xe0, 0x0f, 0xf0, 0x1f, 0xf8, 0x3f, 0xfc, 0x7f, 0xfe, 0x07, 0xe0,
		0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x7f, 0xfe, 0x3f, 0xfc,
		0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		0x24,0x58, /* E-W */
		0x02, 0x01, 0x00, 0x00, 0x06, 0x01, 0x80, 0x00, 0x0a, 0x01, 0x40, 0x00, 0x12, 0x01, 0x20, 0x00,
		0x23, 0xff, 0x10, 0x00, 0x40, 0x00, 0x08, 0x00, 0x80, 0x00, 0x04, 0x00, 0x80, 0x00, 0x04, 0x00,
		0x40, 0x00, 0x08, 0x00, 0x23, 0xff, 0x10, 0x00, 0x12, 0x01, 0x20, 0x00, 0x0a, 0x01, 0x40, 0x00,
		0x06, 0x01, 0x80, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x01, 0x00, 0x00, 0x06, 0x01, 0x80, 0x00, 0x0e, 0x01, 0xc0, 0x00, 0x1e, 0x01, 0xe0, 0x00,
		0x3f, 0xff, 0xf0, 0x00, 0x7f, 0xff, 0xf8, 0x00, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xfc, 0x00,
		0x7f, 0xff, 0xf8, 0x00, 0x3f, 0xff, 0xf0, 0x00, 0x1e, 0x01, 0xe0, 0x00, 0x0e, 0x01, 0xc0, 0x00,
		0x06, 0x01, 0x80, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	SIT_OnChange * msg = cd;

	if (msg->type == SIT_CHANGE_CURSOR)
	{
		static uint8_t cursorType[] = {0, 4, 3, 2, 4, 0, 1, 0, 3, 1, 0, 0, 2}; /* indexed by corner */
		static uint8_t cursorData[] = {0, 66, 132, 230};
		static SDL_Cursor * cursors[5];
		int type, i;

		if (cursors[0] == NULL)
			cursors[0] = SDL_GetCursor();

		type = cursorType[msg->arg];
		if (cursors[type] == NULL)
		{
			int hot, width, height;
			i      = cursorData[type-1];
			width  = arrowData[i];
			height = (width >> 1) & ~7;
			width  = (width & 7) << 3;
			hot    = arrowData[i+1];
			cursors[type] = SDL_CreateCursor(arrowData + i + 2, arrowData + i + 2 + (width >> 3) * height, width, height, hot & 15, hot >> 4);
		}
		SDL_SetCursor(cursors[type]);
	}

	return 1;
}

static void createUI(SIT_Widget app)
{
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
	);
	SIT_SetAttributes(dialog, "<edit bottom=WIDGET,ok,0.5em>");

	SIT_ManageWidget(dialog);
}

int main(int nb, char * argv[])
{
	SDL_Surface * screen;
	SDL_Event     event;
	SIT_Widget    app;
	int           width, height, exitProg;


	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
		return 1;

	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

	width  = 1000;
	height = 600;
    screen = SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER | SDL_OPENGL | SDL_RESIZABLE);
    if (screen == NULL) {
		fprintf(stderr, "failed to set video mode, aborting.\n");
		return 1;
	}
	SDL_WM_SetCaption("SITGL Test", "SITGL Test");

	app = SIT_Init(SIT_NVG_FLAGS, width, height, "css/windows7.css", 1);

	if (app == NULL)
	{
		SIT_Log(SIT_ERROR, "could not init SITGL: %s.\n", SIT_GetError());
		return 1;
	}

	static SIT_Accel accels[] = {
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnClose, ""},
		{SITK_FlagCapture + SITK_Escape,            SITE_OnClose, ""},
		{0}
	};

	exitProg = 0;
	SIT_SetValues(app,
		SIT_DefSBSize,   SITV_Em(0.9),
		SIT_DefSBArrows, SITV_NoArrows,
		SIT_RefreshMode, SITV_RefreshAsNeeded,
		SIT_AddFont,     "sans-serif",      "System",
		SIT_AddFont,     "sans-serif-bold", "System/Bold",
		SIT_AccelTable,  accels,
		SIT_ExitCode,    &exitProg,
		NULL
	);
	SIT_AddCallback(app, SITE_OnChange, setCursor, NULL);

	createUI(app);

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_EnableUNICODE(1);

	glViewport(0, 0, width, height);

	FrameSetFPS(50);
	while (! exitProg)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_F4:
					SIT_Nuke(SITV_NukeCtrl);
					glClear(GL_COLOR_BUFFER_BIT);
				default: break;
				}
			case SDL_KEYUP:
				{
					int * sdlk;
					for (sdlk = SDLKtoSIT; sdlk < EOT(SDLKtoSIT); sdlk += 2)
					{
						if (sdlk[0] == event.key.keysym.sym) {
							SIT_ProcessKey(sdlk[1], SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN);
							goto break_loop;
						}
					}
				}
				if (event.key.keysym.unicode > 0)
					SIT_ProcessChar(event.key.keysym.unicode, SDLMtoSIT(event.key.keysym.mod));
			break_loop:
				break;
			case SDL_MOUSEBUTTONDOWN:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 1);
				break;
			case SDL_MOUSEBUTTONUP:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 0);
				break;
			case SDL_MOUSEMOTION:
				SIT_ProcessMouseMove(event.motion.x, event.motion.y);
				break;
			case SDL_VIDEORESIZE:
				width  = event.resize.w;
				height = event.resize.h;
				SIT_ProcessResize(width, height);
				glViewport(0, 0, width, height);
				break;
			case SDL_QUIT:
				goto exit;
			default:
				continue;
			}
		}

		/* update and render */
		if (SIT_RenderNodes(FrameGetTime()))
			SDL_GL_SwapBuffers();
		FrameWaitNext();
	}

	exit:
	SIT_Nuke(SITV_NukeAll);
	SDL_FreeSurface(screen);
	SDL_Quit();

	return 0;
}

#ifdef	WIN32
#include <windows.h>
int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
	return main(0, NULL);
}
#endif
