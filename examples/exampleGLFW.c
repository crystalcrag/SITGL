/*
 * simple example on how to integrate SITGL with GLFWv3+
 *
 * written by T.Pierron, Oct 2021.
 */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/GL.h>
#include <stdlib.h>
#include <stdio.h>
#include "SIT.h"

static int lastMods, lastMouseX, lastMouseY;
static int curWidth, curHeight;

static void errorFromGLFW(int error, const char* description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

static int GLFWMtoSIT(int mods)
{
	int flags = 0;
	if (mods & GLFW_MOD_SHIFT)   flags |= SITK_FlagShift;
	if (mods & GLFW_MOD_CONTROL) flags |= SITK_FlagCtrl;
	if (mods & GLFW_MOD_ALT)     flags |= SITK_FlagAlt;
	if (mods & GLFW_MOD_SUPER)   flags |= SITK_FlagCmd;
	return flags;
}

/* GLFW uses callbacks for event reporting instead of state machine like Win32 or SDL */
static void rawkeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	/* SITE_OnRawKey */
	static int GLFWKtoSIT[] = {
		GLFW_KEY_HOME,      SITK_Home,      GLFW_KEY_END,       SITK_End,     GLFW_KEY_PAGE_UP,      SITK_PrevPage,
		GLFW_KEY_PAGE_DOWN, SITK_NextPage,  GLFW_KEY_UP,        SITK_Up,      GLFW_KEY_DOWN,         SITK_Down,
		GLFW_KEY_LEFT,      SITK_Left,      GLFW_KEY_RIGHT,     SITK_Right,   GLFW_KEY_MENU,         SITK_AppCommand,
		GLFW_KEY_ENTER,     SITK_Return,    GLFW_KEY_CAPS_LOCK, SITK_Caps,    GLFW_KEY_INSERT,       SITK_Insert,
		GLFW_KEY_DELETE,    SITK_Delete,    GLFW_KEY_NUM_LOCK,  SITK_NumLock, GLFW_KEY_PRINT_SCREEN, SITK_Impr,
		GLFW_KEY_F1,        SITK_F1,        GLFW_KEY_F2,        SITK_F2,      GLFW_KEY_F3,           SITK_F3,
		GLFW_KEY_F4,        SITK_F4,        GLFW_KEY_F5,        SITK_F5,      GLFW_KEY_F6,           SITK_F6,
		GLFW_KEY_F7,        SITK_F7,        GLFW_KEY_F8,        SITK_F8,      GLFW_KEY_F9,           SITK_F9,
		GLFW_KEY_F10,       SITK_F10,       GLFW_KEY_F11,       SITK_F11,     GLFW_KEY_F12,          SITK_F12,
		GLFW_KEY_F13,       SITK_F13,       GLFW_KEY_F14,       SITK_F14,     GLFW_KEY_F15,          SITK_F15,
		GLFW_KEY_BACKSPACE, SITK_BackSpace, GLFW_KEY_ESCAPE,    SITK_Escape,  GLFW_KEY_SPACE,        SITK_Space,
	};

	int * glfw;
	int   pressed = action == GLFW_REPEAT || action == GLFW_PRESS;
	lastMods = GLFWMtoSIT(mods);
	if (lastMods == SITK_FlagCtrl && GLFW_KEY_A <= key && key <= GLFW_KEY_Z)
	{
		/* Ctrl+A~Z : process these as characters 1~26 instead */
		SIT_ProcessChar(key - GLFW_KEY_A + 1, lastMods);
	}
	/* performance is not critical here, a simple loop will be largely enough */
	else for (glfw = GLFWKtoSIT; glfw < EOT(GLFWKtoSIT); glfw += 2)
	{
		if (glfw[0] == key) {
			SIT_ProcessKey(glfw[1], lastMods, pressed);
			break;
		}
	}
}

/* SITE_OnVanillaKey */
static void vanillaKeyCallback(GLFWwindow* window, unsigned int codepoint)
{
	if (codepoint != 32 && codepoint != 10)
		SIT_ProcessChar(codepoint, lastMods);
}

/* SITE_OnMouseMove */
static void mouseMove(GLFWwindow* window, double xpos, double ypos)
{
	lastMouseX = xpos;
	lastMouseY = ypos;
	SIT_ProcessMouseMove(lastMouseX, lastMouseY);
}

/* SITE_OnMouseClick */
static void mouseClick(GLFWwindow* window, int button, int action, int mods)
{
	SIT_ProcessClick(lastMouseX, lastMouseY, button, action == GLFW_PRESS);
}

void mouseWheel(GLFWwindow* window, double xoffset, double yoffset)
{
	SIT_ProcessClick(lastMouseX, lastMouseY, yoffset > 0 ? SITOM_ButtonWheelUp : SITOM_ButtonWheelDown, True);
}

#include "extra.h"
#include "Lexer.c"

struct TextEditor_t
{
	SIT_Widget edit, stat, save, load;
	TEXT       path[256];
	STRPTR     name;
	STRPTR     ext;
	CFA        lastCFA;
};

typedef struct TextEditor_t *      TextEditor;

/* SITE_OnChange on edit box */
static int editorShowStat(SIT_Widget w, APTR cd, APTR ud)
{
	TextEditor editor = ud;
	int bytes, lines;
	SIT_GetValues(w, SIT_EditLength, &bytes, SIT_MaxLines, &lines, NULL);
	SIT_SetValues(editor->stat, SIT_Title|XfMt, "%s, %d bytes, %d lines", editor->name, bytes, lines, NULL);
	return 1;
}

static int editorLoadFile(SIT_Widget w, APTR cd, APTR ud)
{
	static SIT_Widget file;
	TextEditor editor = ud;

	if (file == NULL)
		file = SIT_CreateWidget("fileselect", SIT_FILESELECT, w,
			SIT_Filters,   "Any\t*",
			SIT_SelFilter, 0,
			SIT_InitPath,  "./",
			SIT_DlgFlags,  SITV_FileMustExist,
			NULL
		);

	if (SIT_ManageWidget(file))
	{
		STRPTR path;
		SIT_GetValues(file, SIT_SelPath, &path, NULL);
		int size = FileSize(path);
		FILE * in = fopen(path, "rb");
		if (in)
		{
			STRPTR buffer = malloc(size + 1);
			fread(buffer, size, 1, in);
			fclose(in);
			buffer[size] = 0;
			CopyString(editor->path, path, sizeof editor->path);
			editor->name = BaseName(editor->path);
			SIT_SetValues(editor->edit, SIT_Title, buffer, NULL);
			free(buffer);

			/* lexer: quick'n dirty: grab by extension */
			STRPTR ext = strrchr(editor->name, '.');
			STRPTR old = editor->ext;
			if (ext == NULL) ext = ""; else ext ++;
			if (old == NULL) old = "";
			editor->ext = ext;
			if (strcmp(old, ext))
			{
				/* new syntax file */
				if (editor->lastCFA)
					SYN_Free(editor->lastCFA);
				CFA cfa = NULL;
				TEXT syntax[128];
				sprintf(syntax, "examples/syntax/%s.syntax", ext);
				SYN_Parse(syntax, NULL, &cfa);
				editor->lastCFA = cfa;
				SIT_SetValues(editor->edit, SIT_LexerData, cfa, NULL);
			}
			editorShowStat(editor->edit, NULL, editor);
		}
	}
	return 1;
}

static int editorSaveFile(SIT_Widget w, APTR cd, APTR ud)
{
	return 1;
}

/* create a dummy user interface */
static void createUI(SIT_Widget parent)
{
	static uint8_t colorMap[] = { /* default color map - suited for bright bg */
		10,
		0x00, 0x00, 0x00, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* normal text */
		0x14, 0x6a, 0x31, 0xff,      0x00, 0x00, 0x00, 0x00,    0x01,    /* type */
		0x54, 0xaa, 0xaa, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* identifier */
		0xa6, 0x28, 0x28, 0xff,      0x00, 0x00, 0x00, 0x00,    0x01,    /* keywords */
		0x22, 0x22, 0xaa, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* comments */
		0xa6, 0x00, 0xf7, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* directive */
		0xff, 0x00, 0xff, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* constants */
		0x77, 0x00, 0xff, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* special */
		0xff, 0xff, 0xff, 0xff,      0xff, 0x33, 0x33, 0xff,    0x00,    /* errors */
		0x00, 0x00, 0x00, 0xff,      0xff, 0xea, 0x4d, 0xff,    0x00,    /* notes */
	};

	/* create a Q'N'D text editor */
	SIT_Widget dialog = SIT_CreateWidget("mainwnd", SIT_DIALOG + SIT_EXTRA(sizeof (struct TextEditor_t)), parent,
		SIT_Width,        SITV_Em(40),
		SIT_Title,        _("This is a movable window"),
		SIT_DialogStyles, SITV_Movable | SITV_Resizable,
		SIT_Width,        curWidth  * 8 / 10,
		SIT_Height,       curHeight * 8 / 10,
		NULL
	);

	TextEditor editor;
	SIT_GetValues(dialog, SIT_UserData, &editor, NULL);
	strcpy(editor->path, "Untitled");
	editor->name = editor->path;

	SIT_CreateWidgets(dialog,
		"<button name=load title='Load file'>"
		"<button name=save title='Save' enabled=0 left=WIDGET,load,0.5em>"
		"<label name=stat left=WIDGET,save,1em right=FORM top=MIDDLE,save>"
		"<editbox name=edit left=FORM right=FORM right=FORM bottom=FORM top=WIDGET,load,0.5em editType=", SITV_Multiline,
		" lexer=", SYN_HighlightText, "colorMap=", colorMap, "extra=", LEXER_EXTRA, ">"
	);
	editor->edit = SIT_GetById(dialog, "edit");
	editor->stat = SIT_GetById(dialog, "stat");
	editor->save = SIT_GetById(dialog, "save");
	editor->load = SIT_GetById(dialog, "load");
	SIT_AddCallback(editor->edit, SITE_OnChange,   editorShowStat, editor);
	SIT_AddCallback(editor->load, SITE_OnActivate, editorLoadFile, editor);
	SIT_AddCallback(editor->save, SITE_OnActivate, editorSaveFile, editor);
	editorShowStat(editor->edit, NULL, editor);

	SIT_ManageWidget(dialog);
}

int main(void)
{
	GLFWwindow * window;
	SIT_Widget   app;
	int          exitProg;

	glfwSetErrorCallback(errorFromGLFW);

	if (! glfwInit())
		return 1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	curWidth  = 1024;
	curHeight = 768;
	window    = glfwCreateWindow(curWidth, curHeight, "Simple GLFW example", NULL, NULL);
	if (! window)
	{
		glfwTerminate();
		return 1;
	}

	glfwSetKeyCallback(window, rawkeyCallback);
	glfwSetCharCallback(window, vanillaKeyCallback);
	glfwSetCursorPosCallback(window, mouseMove);
	glfwSetMouseButtonCallback(window, mouseClick);
	glfwSetScrollCallback(window, mouseWheel);

	glfwMakeContextCurrent(window);

	app = SIT_Init(SIT_NVG_FLAGS, curWidth, curHeight, "examples/css/windows7.css", 1);

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

	createUI(app);

	FrameSetFPS(50);
	while (! glfwWindowShouldClose(window) && ! exitProg)
	{
		int width, height;

		glfwGetFramebufferSize(window, &width, &height);
		if (width != curWidth || height != curHeight)
		{
			SIT_ProcessResize(width, height);
			curWidth = width;
			curHeight = height;
		}

		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT);

		if (SIT_RenderNodes(FrameGetTime()))
			glfwSwapBuffers(window);

		FrameWaitNext();
		glfwPollEvents();
	}

	glfwDestroyWindow(window);

	glfwTerminate();
	return 0;
}
