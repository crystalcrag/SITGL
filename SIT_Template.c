/*
 * SIT_Template.c : alternate interface for creating controls/setting attributes.
 *
 * written by T.Pierron, july 2010.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include "SIT_P.h"

typedef struct
{
	APTR parent, w;
	int  type;
	Bool set;
	int  closing;
}	PLArgs;

extern TagList LabelClass[],    ButtonClass[],   EditBoxClass[], WidgetClass[];
extern TagList ListBoxClass[],  ComboClass[],    SBClass[],      SliderClass[];
extern TagList ProgressClass[], TabClass[],      TooltipClass[];

/*
 * some attributes can only be set at creation time, therefore we need the complete tag
 * list before calling SIT_CreateWidget()
 */
static TagList * classes[] = {
	NULL, NULL,  LabelClass,    ButtonClass,  EditBoxClass,  WidgetClass, ListBoxClass,
	WidgetClass, SBClass,       SliderClass,  ProgressClass, ComboClass,  TabClass,
	TooltipClass
};

/* unescape &#%d; and &#x%x; into UTF-8 sequence: io is an alloca()'ed buffer */
static void UnescapeEntities(STRPTR io)
{
	STRPTR p;

	for (p = io; *p; )
	{
		if (*p == '&' && p[1] == '#')
		{
			STRPTR s = p;
			int cp = (p[2] == 'x' ? strtoul(p + 3, &p, 16) : strtoul(p + 2, &p, 10));

			if (0 <= cp && cp <= 0x10ffff && *p == ';')
			{
				if (cp <= 0x7F) {
				   *s++ = cp;
				} else if (cp <= 0x7FF) {
				   *s++ = 0xC0 | ((cp >> 6) & 0x1F);
				   *s++ = 0x80 | (cp & 0x3F);
				} else if (cp <= 0xFFFF) {
				   *s++ = 0xE0 | ((cp >> 12) & 0x0F);
				   *s++ = 0x80 | ((cp >> 6) & 0x3F);
				   *s++ = 0x80 | (cp & 0x3F);
				} else {
				   *s++ = 0xF0 | ((cp >> 18) & 0x07);
				   *s++ = 0x80 | ((cp >> 12) & 0x3F);
				   *s++ = 0x80 | ((cp >> 6)  & 0x3F);
				   *s++ = 0x80 | (cp & 0x3F);
				}
				p ++;
				if (s < p) p = strcpy(s, p);
			}
		}
		else p ++;
	}
}

/* parse one tag declaration */
static STRPTR SIT_ParseLine(STRPTR line, va_list * list, TagList * classArgs, PLArgs * cd)
{
	KeyVal_t table[8];
	TagList  name  = {.tl_TagID = SIT_TagUser, .tl_Type = SIT_PTR};
	KeyVal   cur   = table;
	KeyVal   start = table;
	STRPTR   buf   = NULL;

	while (*line && *line != '>')
	{
		TagList * stack[5];
		TagList * args;
		int       usage = 1, tag;
		STRPTR    p;

		for (p = line; isalnum(*p); p ++); tag = p - line;
		/* get args */
		while (*p && *p != '=') p ++;
		for (p ++; isspace(*p); p ++);

		stack[0] = WidgetClass; /* implicit: all widgets derived from this */
		if (strncasecmp(line, "name", tag))
		{
			/* search for tag definition in classArgs */
			for (args = classArgs; args->tl_TagID != SIT_TagEnd; )
			{
				if (args->tl_TagID == SIT_SuperClass)
				{
					stack[usage ++] = args + 1;
					args = (TagList *) args->tl_Arg;
					continue;
				}
				if (args->tl_TagName && strncasecmp(line, args->tl_TagName, tag) == 0 &&
				    args->tl_TagName[tag] == 0)
					break;
				args ++;
				if (args->tl_TagID == 0 && usage > 0)
					args = stack[-- usage];
			}
			/* usually mis-spelled attribute */
			if (args->tl_TagID == SIT_TagEnd)
			{
				p = alloca(tag+1);
				CopyString(p, line, tag+1);
				SIT_Log(SIT_CRITICAL, "Unknown attribute '%s'%con widget '%s'.", p, buf ? ' ' : 0, buf);
			}
		}
		else args = &name;

		if (args->tl_Type == SIT_ABBR)
			tag = args->tl_Arg, args -= (tag & 0xff) - ABBRBASE, tag >>= 8;
		else
			tag = 0;

		/* 2 ways to specify args: inline string, or varargs */
		if (*p)
		{
			STRPTR e;
			TEXT delim = *p;
			if (delim != '\'' && delim !='\"') delim = 0;
			else p ++;
			for (;;)
			{
				if (cur - start == DIM(table) - 1)
				{
					cur->tag = SIT_TagList;
					start = cur->key.ptr = alloca(sizeof table);
					cur = start;
				}
				cur->tag = args->tl_TagID;
				/* get to the end of token */
				if (delim == 0)
					for (e = p; *p && !isspace(*p) && *p != ',' && *p != '>'; p ++);
				else
					for (e = p; *p && *p != delim; p ++);

				if (*p == '>' && p[-1] == '/') cd->closing = 1, p --;

				/* hack: we want userdata to be processed as an integer not pointer */
				switch (args->tl_TagID == SIT_UserData ? SIT_LONG : args->tl_Type) {
				case SIT_ABBR: break;
				case SIT_REAL: cur->key.real = e < p ? strtod(e, &e) : 0; break;
				case SIT_INT:
					if (SIT_LeftAttachment   <= args->tl_TagID &&
					    SIT_BottomAttachment >= args->tl_TagID && isalpha(*e))
					{
						cur->key.val = FindInList("NONE,FORM,POSITION,WIDGET,OPPOSITE,MIDDLE,NOOVERLAP",
							e, p - e);
						if (cur->key.val == (ULONG) -1)
							cur->key.val = strtoul(e, NULL, 0);
						break;
					}
					/* no break; */
				case SIT_LONG:
				case SIT_BOOL: cur->key.val = e < p ? strtol(e, &e, 0) : 0; break;
				case SIT_CTRL:
					cur->key.ptr = e < p ? SIT_FindControl(cd->parent, e, p - e + 1, False) : NULL;
					if (cur->key.ptr == NULL && e < p)
						fprintf(stderr, "can't find control %.*s\n", p - e + 1, e);
					break;
				case SIT_UNIT:
					if (strncasecmp(p-2, "em", 2) == 0)
						cur->key.val = SITV_Em(strtod(e, &e));
					else if (strncasecmp(e, "NOPAD", 5) == 0)
						cur->key.val = SITV_NoPad;
					else cur->key.val = e < p ? strtol(e, &e, 0) & ~(1<<31) : 0;
					break;
				case SIT_PTR: /* :-/ -- SIT_Font is defined like this */
				case SIT_STR:
					if (e < p && p - e <= 1024)
					{
						cur->key.ptr = alloca(p - e + 1);
						CopyString(cur->key.ptr, e, p - e + 1);
						UnescapeEntities(cur->key.ptr);
					}
					else cur->key.ptr = NULL;
				}
				if (cur->tag > 0)
				{
					if (cur->tag < SIT_TagUser) cur ++;
					else buf = cur->key.ptr;
				}
				if (*p == ',' || *p == delim) p ++;
				if (tag == 0) break;
				args -= (tag & 0xff) - ABBRBASE; tag >>= 8;
			}
			/* prevent unquoted comma from wreaking havoc */
			if (p[0] == '/' && p[1] == '>') cd->closing = 1;
			while (*p && *p != '>' && !isspace(*p)) p++;
		}
		else /* varargs */
		{
			for (;;)
			{
				if (cur - start == DIM(table) - 1)
				{
					cur->tag = SIT_TagList;
					start = cur->key.ptr = alloca(sizeof table);
					cur = start;
				}
				cur->tag = args->tl_TagID;

				switch (args->tl_Type) {
				case SIT_ABBR: break;
				case SIT_REAL: cur->key.real = va_arg(*list, double); break;
				case SIT_CTRL: case SIT_STR:
				case SIT_PTR:  cur->key.ptr = va_arg(*list, APTR); break;
				case SIT_UNIT:
				case SIT_INT:  cur->key.val = va_arg(*list, int);  break;
				case SIT_LONG: cur->key.val = va_arg(*list, long); break;
				case SIT_BOOL: cur->key.val = va_arg(*list, Bool); break;
				}
				if (cur->tag > 0)
				{
					if (cur->tag < SIT_TagUser) cur ++;
					else buf = cur->key.ptr; /* name attr */
				}
				if (tag == 0) break;
				args -= (tag & 0xff) - ABBRBASE; tag >>= 8;
			}
			p = va_arg(*list, STRPTR);
		}
		for (line = p; isspace(*line); line ++);
		if (*line == '/' && line[1] == '>') { line ++; cd->closing = 1; }
	}
	cur->tag = SIT_TagEnd;

	if (! cd->set)
	{
		cd->w = SIT_CreateWidget(buf, cd->type, cd->parent, SIT_TagList, table, NULL);
		if (! cd->w) SIT_Log(SIT_CRITICAL, "Fail to create widget '%s'", buf);
	}
	else SIT_SetValues(cd->w, SIT_TagList, table, NULL);

	if (*line == '>')
		for (line ++; isspace(*line); line ++);

	return line;
}

/* Create a list of controls in one API call */
DLLIMP Bool SIT_CreateWidgets(SIT_Widget parent, STRPTR fmt, ...)
{
	static char auto_nested[] = {SIT_FRAME, SIT_CANVAS, SIT_TAB, 0};
	va_list args;
	PLArgs  buf = {};
	STRPTR  p;

	if (parent == NULL)
		return False;
	va_start(args, fmt);
	if (parent->type == SIT_DIALOG && ((SIT_Dialog)parent)->clientArea)
		parent = ((SIT_Dialog)parent)->clientArea;
	buf.parent = parent;
	while (fmt)
	{
		/* Get control type to be created */
		buf.closing = 0;
		while (*fmt && *fmt != '<') fmt ++;
		if (*fmt == 0) break;
		if (fmt[1] == '/') buf.closing = 2, fmt ++;
		for (p = ++ fmt; isalpha(*p); p ++);
		buf.type = 2+FindInList(
			"label,button,editbox,frame,listbox,canvas,scrollbar,"
			"slider,progress,combobox,tab,tooltip", fmt, p - fmt
		);

		if (buf.type < 2) {
			STRPTR dup = alloca(p-fmt+1);
			CopyString(dup, fmt, p-fmt+1);
			SIT_Log(SIT_CRITICAL, "Unknown widget type '%s'", dup);
		}
		while (*p && isspace(*p)) p ++;
		if (buf.closing == 2)
		{
			if (buf.type == ((SIT_Widget)buf.parent)->type)
				buf.parent = ((SIT_Widget)buf.parent)->parent;
		}
		else fmt = SIT_ParseLine(p, &args, classes[buf.type], &buf);

		if (! buf.closing && buf.w && strchr(auto_nested, buf.type))
			buf.parent = buf.w;
	}
	va_end(args);
	return True;
}

/* Set properties of several controls at once */
DLLIMP void SIT_SetAttributes(SIT_Widget parent, STRPTR fmt, ...)
{
	va_list args;
	PLArgs  buf = {.set = True};
	STRPTR  p;

	if (parent == NULL) return;
	va_start(args, fmt);
	while (fmt)
	{
		buf.closing = 0;
		while (*fmt && *fmt != '<') fmt ++;
		if (*fmt == 0) break;
		if (fmt[1] == '/') buf.closing = 2, fmt ++;
		for (p = ++ fmt; isalnum(*p); p ++);

		buf.w = SIT_FindControl(parent, fmt, p - fmt + 1, True);
		if (buf.w)
		{
			buf.parent = ((SIT_Widget)buf.w)->parent;
			while (*p && isspace(*p)) p ++;
			if (buf.closing < 2) fmt = SIT_ParseLine(p, &args, ((SIT_Widget)buf.w)->attrs, &buf);
		}
		else fprintf(stderr, "%.*s: control not found\n", p - fmt, fmt);
	}
	va_end(args);
}

