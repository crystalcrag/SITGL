/*
 * ColorChooser.c : simple RGB color chooser using HSV color space.
 *
 * written by T.Pierron, may 2021.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "SIT.h"


typedef struct ColorChooser_t *      ColorChooser;
typedef uint16_t *                   DATA16;

struct ColorChooser_t
{
	SIT_Widget SV;
	SIT_Widget hue;
	SIT_Widget val;
	SIT_Widget SVindic;
	SIT_Widget Hindic;
	uint8_t    rgb[4];
	uint16_t   hsv[3];
};


/* Normalized in range 255-255-255 from range 360-100-100 */
static void HSV_to_RGB(DATA16 hsv, DATA8 rgb)
{
	int i, f, m, n, v;

	i = hsv[0] / 60;
	v = hsv[2];
	f = ((hsv[0] - 60*i) << 8) / 60;
	if ((i&1) == 0) f = 256 - f; // if i is even
	m = v * (100 - hsv[1]) * 255 / 10000;
	n = v * (100 - (hsv[1] * f >> 8)) * 255 / 10000;
	v = v * 255 / 100;
	switch (i) {
	case 6:
	case 0: rgb[0] = v; rgb[1] = n; rgb[2] = m; break;
	case 1: rgb[0] = n; rgb[1] = v; rgb[2] = m; break;
	case 2: rgb[0] = m; rgb[1] = v; rgb[2] = n; break;
	case 3: rgb[0] = m; rgb[1] = n; rgb[2] = v; break;
	case 4: rgb[0] = n; rgb[1] = m; rgb[2] = v; break;
	case 5: rgb[0] = v; rgb[1] = m; rgb[2] = n;
	}
}

#define MAX3(x,y,z)  ((y) >= (z) ? ((x) >= (y) ? (x) : (y)) : \
                                   ((x) >= (z) ? (x) : (z)))
#define MIN3(x,y,z)  ((y) <= (z) ? ((x) <= (y) ? (x) : (y)) : \
                                   ((x) <= (z) ? (x) : (z)))

/* Normalized in range 360-100-100 from range 255-255-255 */
static void RGB_to_HSV(DATA16 rgb, DATA16 hsv)
{
	int max = MAX3(rgb[0], rgb[1], rgb[2]);
	int min = MIN3(rgb[0], rgb[1], rgb[2]);
	hsv[0] = hsv[1] = 0;
	hsv[2] = 100 * max / 255;
	if (max > 0)
	{
		int diff = max - min;
		hsv[1] = 100 * diff / max;
		if (hsv[1] > 0)
		{
			if (max == rgb[0]) {
				hsv[0] = 60 * (rgb[1] - rgb[2]) / diff;
				if (hsv[0] < 0) hsv[0] += 360;
			} else if (max == rgb[1]) {
				hsv[0] = 120 + 60 * (rgb[2] - rgb[0]) / diff;
			} else {
				hsv[0] = 240 + 60 * (rgb[0] - rgb[1]) / diff;
			}
		}
	}
}

static void CCSetColor(ColorChooser cc, DATA8 rgb)
{
	TEXT     bg[128];
	TEXT     rgbtxt[8];
	uint16_t rgb16[3];
	uint8_t  rgb8[3];

	memcpy(cc->rgb, rgb, 4);
	rgb16[0] = rgb[0];
	rgb16[1] = rgb[1];
	rgb16[2] = rgb[2];
	RGB_to_HSV(rgb16, cc->hsv);

	SIT_SetValues(cc->Hindic,  SIT_TopObject, SITV_AttachPos(100 - cc->hsv[0] / 3.60), NULL);
	SIT_SetValues(cc->SVindic, SIT_TopObject, SITV_AttachPos(100 - cc->hsv[1]), SIT_LeftObject, SITV_AttachPos(cc->hsv[2]), NULL);

	uint16_t hsv[] = {cc->hsv[0], 100, 100};
	HSV_to_RGB(hsv, rgb8);

	sprintf(rgbtxt, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
	sprintf(bg,
		"background: linear-gradient(to right, black, rgba(0,0,0,0)), "
		"linear-gradient(to bottom, %s, white)", rgbtxt
	);
	SIT_SetValues(cc->SV, SIT_Style, bg, NULL);
	SIT_SetValues(cc->val, SIT_Title, rgbtxt, NULL);
}

static void CCSetSVCursor(ColorChooser cc, int x, int y)
{
	float width, height;
	SIT_GetValues(cc->SV, SIT_Width, &width, SIT_Height, &height, NULL);

	if (x < 0) x = 0; if (x >= width)  x = width-1;
	if (y < 0) y = 0; if (y >= height) y = height-1;

	cc->hsv[1] = 100 - (y / height) * 100;
	cc->hsv[2] = (x / width) * 100;

	TEXT    rgbtxt[8];
	uint8_t rgb[4];
	HSV_to_RGB(cc->hsv, rgb);
	sprintf(rgbtxt, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
	SIT_SetValues(cc->val, SIT_Title, rgbtxt, NULL);
	SIT_SetValues(cc->SVindic, SIT_TopObject, SITV_AttachPos(y * 100 / height), SIT_LeftObject, SITV_AttachPos(x * 100 / width), NULL);
}

static int CCMoveHS(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;

	switch (msg->state) {
	case SITOM_ButtonPressed:
		if (msg->button == SITOM_ButtonLeft)
		{
			CCSetSVCursor(ud, msg->x, msg->y);
			return 2;
		}
		break;
	case SITOM_CaptureMove:
		CCSetSVCursor(ud, msg->x, msg->y);
	default:
		break;
	}
	return 1;
}

SIT_Widget CCOpen(SIT_Widget app, DATA8 rgb)
{
	static TEXT bgSV[] =
		"background: linear-gradient(to bottom, #f00, #f0f 17%, #00f 33%, #0ff 50%, #0f0 67%, #ff0 83%, #f00)";
	static TEXT bgCursorSV[] =
		"background: url(data:image/gif;base64,R0lGODlhDwAPAKEBAAAAAP///////////yH5BAEKAAIALAAAAAAPAA8AAAIklB8Qx53b4otSUWcvyiz4/4AeQJbmKY4p1HHapBlwPL/uVRsFADs=);"
		"top: -7px; left: -7px";
	static TEXT bgCursorHue[] =
		"background: url(data:image/gif;base64,R0lGODlhBwALAKECAAAAAP///6g8eKg8eCH5BAEKAAIALAAAAAAHAAsAAAITTIQYcLnsgGxvijrxqdQq6DRJAQA7);"
		"top: -5px; left: -7px";

	SIT_Widget diag = SIT_CreateWidget("colorchooser", SIT_DIALOG + SIT_EXTRA(sizeof (struct ColorChooser_t)), app,
		SIT_Title,        "Select color",
		SIT_DialogStyles, SITV_Movable | SITV_Resizable,
		NULL
	);
	ColorChooser cc;

	SIT_GetValues(diag, SIT_UserData, &cc, NULL);
	SIT_CreateWidgets(diag,
		"<canvas name=SV height=10em>"
		" <canvas name=SVindic width=15 height=15 style=", bgCursorSV, "top=POSITION left=POSITION/>"
		"</canvas>"
		"<canvas name=hue style=", bgSV, "width=1em height=10em right=FORM top=FORM bottom=OPPOSITE,SV>"
		" <canvas name=Hindic width=7 height=11 top=POSITION style=", bgCursorHue, "/>"
		"</canvas>"
		"<editbox name=color width=5em bottom=FORM>"
		"<button name=ko title=Cancel top=MIDDLE,color left=WIDGET,color,0.3em>"
		"<button name=ok title=Select top=MIDDLE,color left=WIDGET,ko,0.3em>"
	);
	SIT_SetAttributes(diag, "<SV left=FORM top=FORM right=WIDGET,hue,0.8em bottom=WIDGET,color,0.3em>");

	cc->SV  = SIT_GetById(diag, "SV");
	cc->hue = SIT_GetById(diag, "hue");
	cc->val = SIT_GetById(diag, "color");
	cc->SVindic = SIT_GetById(diag, "SVindic");
	cc->Hindic  = SIT_GetById(diag, "Hindic");

	SIT_AddCallback(cc->SV, SITE_OnClickMove, CCMoveHS, cc);

	CCSetColor(cc, rgb);

	return diag;
}
