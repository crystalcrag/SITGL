/*
 * msgextract.c : extract strings from files and merge them with existing language file
 *
 * note: this file depends on SITGL because it needs a lot of the UtilityLibLite functions.
 *
 * Written by T.Pierron, june 2020.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include "../UtilityLibLite.h"
#include "msgextract.h"


ListHead strings = {0}; /* Msg */
ListHead langs   = {0}; /* Lang */
ListHead skip    = {0}; /* String */
int      total   = 0;

int main(int nb, char * argv[])
{
	INIFile conf = ParseINI(nb > 1 ? argv[1] : "msgextract.ini");

	if (conf)
	{
		STRPTR folder, prefix, suffix;
		STRPTR k, v;
		LangFile l;
		Bool unescape;

		folder = prefix = suffix = NULL;
		unescape = True;

		while (IterINI(conf, &k, &v))
		{
			if (v == NULL)
			{
				if (folder && prefix && suffix)
					ExtractMessages(folder, prefix, suffix, unescape);

				folder = prefix = suffix = NULL;
				unescape = True;
			}
			else switch (FindInList("Folder,Prefix,Suffix,Lang,Skip,Unescape", k, 0)) {
			case 0: folder = v; break;
			case 1: prefix = v; break;
			case 2: suffix = v; break;
			case 3: ParseLangs(v); break;
			case 4: LangAddLine(&skip, v, NULL, NULL, 0); break;
			case 5: unescape = strtol(v, NULL, 10);
			}
		}
		if (folder && prefix && suffix)
			ExtractMessages(folder, prefix, suffix, unescape);
		FreeINI(conf);

		fprintf(stderr, "%d files processed for message translation.\n", total);

		for (l = HEAD(langs); l; NEXT(l))
		{
			String s;
			Msg    m;

			for (m = HEAD(strings); m; m->flag = 0, NEXT(m));

			for (s = HEAD(l->file->strings); s; NEXT(s))
			{
				if (s->type) continue;

				for (m = HEAD(strings); m && LangCompare(m->text, s->str); NEXT(m));

				if (m == NULL)
				{
					/* String does not exist anymore : comment it when rewriting file */
					s->type = 2;
				}
				else m->flag = 1; /* Mark message as OLD */
			}

			/* Now we have : old messages in "l->file->strings" marked (type == 2).
			 *               new messages in "strings".
			 *               current messages in l->file->strings (type == 0).
			 */
			LangRewrite(l);
		}
	}
	else fprintf(stderr, "Cannot open '%s': %s\n", nb > 1 ? argv[1] : "msgextract.ini", GetError());

	return 0;
}

/* First character case insensitive, remaining case sensitive */
static int LangCompare(STRPTR msg1, STRPTR msg2)
{
	return toupper(msg1[0]) == toupper(msg2[0]) ? strcmp(msg1+1, msg2+1) : strcmp(msg1, msg2);
}

static int LangComment(STRPTR * lang, int sz, int offset)
{
	STRPTR p = *lang + offset;
	Bool   f = True;
	int    m = (sz + 255) & ~255;
	STRPTR end, t;

	for (; p; f = False, p = end)
	{
		end = NextLine(p, False);
		if (f == False)
		{
			/* Check if this line is a continuation of previous */
			for (t = p; *t && *t != '\n' && *t != '\r' && isspace(*t); t ++);
			if (t == p || *t != '\"') return sz;
		}
		if (sz + 2 > m)
		{
			m = (sz + 257) & ~255;
			t = realloc(*lang, m);
			if (t == NULL) return sz;
			p = t + (p - *lang);
			end = t + (end - *lang);
			*lang = t;
		}
		memmove(p + 2, p, sz - (p - *lang));
		memcpy(p, "# ", 2);
		sz += 2; end += 2;
	}
	return sz;
}

/* Check if string is not explicitely marked as "Do not comment" */
static Bool CanComment(STRPTR buf, String s)
{
	STRPTR p, eol;
	/* Get to start of previous line */
	for (p = eol = buf + s->offset_str - 1; p > buf && isspace(*p); p --);
	while (p > buf && *p != '\r' && *p != '\n') p --;
	if (p > buf) p ++;
	if (*p == '#') /* Has to be a comment */
	{
		for (p ++; isspace(*p) && p < eol; p ++);
		return strncasecmp(p, "Do not comment", 14) != 0;
	}
	return True;
}

/* Get line number given string is on, by counting preceding line feed */
static int LineNumber(STRPTR begin, int pos)
{
	STRPTR p;
	int    ln;
	for (p = begin + pos, ln = 1; p > begin; ln += *p == '\n', p--);
	return ln;
}

static void LangBackup(STRPTR path)
{
	STRPTR backup = alloca(strlen(path) + 16);

	CopyString(backup, path, 1e6);
	AddPart(backup, "../old", 1e6);
	AddPart(backup, BaseName(path), 1e6);
	CreatePath(backup, True);
	FileCopy(path, backup, True);
}

static void LangRewrite(LangFile l)
{
	STRPTR old;
	FILE * out;
	String s;
	Msg    m;
	int    sz;
	int    nbadd = 0;
	int    nbdel = 0;
	int    nbrem = 0;

	/* Save the old one, just in case */
	LangBackup(l->path);

	/* Read the whole file at once, we'll comment obsolete message in memory and overwrite everything at once */
	out = fopen(l->path, "rb");
	sz  = FileSize(l->path) + 1;
	old = malloc((sz + 255) & ~255);

	fread(old, 1, sz, out); old[sz - 1] = 0;
	fclose(out);

	for (s = TAIL(l->file->strings); s; PREV(s))
	{
		if (s->origin > 0) continue; /* Quoted from an included file */
		if (s->type != 2) { nbrem ++; continue; }

		if (CanComment(old, s))
		{
			sz = LangComment(&old, sz, s->offset_trans);
			sz = LangComment(&old, sz, s->offset_str);
			nbdel ++;
			fprintf(stderr, "commenting:%d: %s\n", LineNumber(old, s->offset_str), s->str);
		}
	}

	out = fopen(l->path, "wb");
	if (out)
	{
		Bool warnednew = False;

		fwrite(old, 1, sz - 1, out);

		/* Put new messages after old ones */
		for (m = HEAD(strings); m; NEXT(m))
		{
			if (m->flag == 1) continue;
			if (! warnednew)
				fprintf(out, "\n# NEW MESSAGES\n"), warnednew = True;

			WriteString("msgid ", "      ", m->text, out);

			/* Empty translation */
			fprintf(out, "msgstr \"\"\n\n");
			nbadd ++;
		}
		fprintf(stderr, "%s updated: %d added, %d commented, %d untouched.\n", BaseName(l->path),
			nbadd, nbdel, nbrem);

		fclose(out);
	}
	else fprintf(stderr, "Cannot write '%s': %s\n", l->path, GetError());
}

static void WriteString(STRPTR prefix1, STRPTR prefix2, STRPTR str, FILE * out)
{
	fprintf(out, "%s\"", prefix1);

	while (*str)
	{
		switch (*str) {
		case '\t': fprintf(out, "\\t"); break;
		case '\n': fprintf(out, "\\n"); break;
		case '\r': fprintf(out, "\\r"); break;
		case '\v': fprintf(out, "\\v"); break;
		case '\\': fprintf(out, "\\\\"); break;
		case '\"': fprintf(out, "\\\""); break;
		default:
			if (* (unsigned char *) str < 32) fprintf(out, "\\x%02x", *str);
			else fputc(*str, out);
		}
		str ++;
	}
	fprintf(out, "\"\n");
}

/* Scan file for string to be translated */
static void ExtractMessages(STRPTR folder, STRPTR prefix, STRPTR suffix, Bool unescape)
{
	STRPTR pat = BaseName(folder);

	if (pat > folder) pat[-1] = 0;
	else folder = ".";

	ScanDirData args;

	if (! ScanDirInit(&args, folder))
		return;

	do {
		if (args.isDir || ! FnMatch(pat, args.name, FNM_CASEFOLD))
			continue;

		TEXT buffer[256];

		CopyString(buffer, folder, sizeof buffer);
		AddPart(buffer, args.name, sizeof buffer);

		FILE * in = fopen(buffer, "rb");

		if (! in)
			continue;

		fprintf(stderr, "parsing %s...\n", buffer);

		STRPTR token, end;
		Msg    msg, m;
		String s;

		while (fgets(buffer, sizeof buffer, in))
		{
			end = buffer;
			while ((token = strstr(end, prefix)))
			{
				token += strlen(prefix);
				end = strstr(token, suffix);
				if (end == NULL) break;
				msg = malloc(sizeof *msg + end - token + 1);
				CopyString(msg->text, token, end - token + 1);
				if (unescape) UnescapeAntiSlash(msg->text);
				end += strlen(suffix);

				/* check if string has already been added */
				for (m = HEAD(strings); m && LangCompare(m->text, msg->text); NEXT(m));

				/* check if string match reject list */
				for (s = HEAD(skip); s && ! FnMatch(s->str, msg->text, 0); NEXT(s));

				if (s == NULL && m == NULL) ListAddTail(&strings, &msg->node);
				else free(msg);
			}
		}
		fclose(in);
		total ++;
	}
	while (ScanDirNext(&args));
}

/* Get messages already translated */
static void ParseLangs(STRPTR dir)
{
	ScanDirData args;

	if (ScanDirInit(&args, dir))
	{
		do {

			if (args.isDir)
				continue;

			LangFile lf = malloc(sizeof *lf + strlen(dir) + strlen(args.name) + 2);
			lf->path = strcpy((STRPTR) (lf + 1), dir);
			AddPart(lf->path, args.name, 1e6);

			Lang lang = ParseLang(args.name, lf->path);

			if (lang)
			{
				lf->file = lang;
				ListAddTail(&langs, &lf->node);
			}
			else free(lf);
		}
		while (ScanDirNext(&args));
	}
}

static void FreeLang(Lang lang)
{
	LangStack * ls;
	ListNode *  node;
	int i;

	for (ls = lang->files + DIM(lang->files) - 1, i = lang->count; i < DIM(lang->files); free(ls->buffer), i ++, ls --);
	while ((node = ListRemHead(&lang->strings))) free(node);
	free(lang);
}

static STRPTR LangStackFile(Lang lang, STRPTR path, STRPTR eol)
{
	LangStack * ls;
	FILE * in;
	STRPTR rel      = lang->stack > 0 ? lang->files[lang->stack-1].file : NULL;
	ULONG  length   = strlen(path) + (rel ? strlen(rel) : 0) + 2;
	STRPTR fullpath = alloca(length);
	STRPTR buffer   = NULL;

	CopyString(fullpath, rel, length);
	ParentDir(fullpath);
	AddPart(fullpath, path, length);

	/* Check for circular ref */
	int i;
	for (ls = lang->files, i = 0; i < lang->stack && strcasecmp(fullpath, ls->file); ls ++, i ++);
	if (i < lang->stack) return NULL;

	in = fopen(fullpath, "rb");

	if (in)
	{
		ls = lang->files + lang->stack;

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
	else fprintf(stderr, "Fail to open '%s': %s\n", fullpath, GetError());
	return buffer;
}

static STRPTR LangPopFile(Lang lang, STRPTR * start)
{
	if (lang->stack > 0)
	{
		LangStack * ls = lang->files + (-- lang->stack);
		STRPTR eol = ls->eol;

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


static Lang ParseLang(STRPTR name, STRPTR path)
{
	STRPTR msgid  = NULL;
	STRPTR msgstr = NULL;
	STRPTR line, eol, buffer;
	Lang   lang;
	int    offset[2];

	lang = calloc(sizeof *lang, 1);
	lang->count = DIM(lang->files);
	buffer = LangStackFile(lang, path, NULL);
	eol = NULL;

	CopyString(lang->name, name, sizeof lang->name);

	while (buffer)
	{
		for (line = eol ? eol : buffer; line; line = eol)
		{
			eol = NextLine(line, True);

			if (*line == '#')
			{
				if (strncasecmp(line + 1, "include ", 8) == 0)
				{
					buffer = LangStackFile(lang, line + 9, eol);

					if (buffer) eol = buffer;
				}
				else if (strncasecmp(line + 1, "name ", 5) == 0 && lang->stack == 1)
				{
					CopyString(lang->name, line + 6, sizeof lang->name);
				}
				else if (strncasecmp(line + 1, "msgextract skip", 15) == 0)
				{
					FreeLang(lang);
					return NULL;
				}
				continue;
			}

			if (strncasecmp(line, "msgid ", 6) == 0)
			{
				if (msgstr && IsDef(msgid))
				{
					/* print "adding \"$msgid\" => \"$msgstr\"\n"; */
					LangAddLine(&lang->strings, msgid, msgstr, offset, lang->stack > 1);
				}
				msgstr = msgid = NULL;
				msgid  = LangExtractStr(msgid, line);
				offset[0] = line - buffer;
			}
			else if (strncasecmp(line, "msgstr ", 7) == 0)
			{
				msgstr = LangExtractStr(msgstr, line);
				offset[1] = line - buffer;
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
		if (msgstr && IsDef(msgid))
		{
			LangAddLine(&lang->strings, msgid, msgstr, offset, lang->stack > 1);
		}
		msgstr = msgid = NULL;
		eol = LangPopFile(lang, &buffer);
	}

	return lang;
}

static void LangAddLine(ListHead * head, STRPTR msgid, STRPTR msgstr, int * offset, int origin)
{
	String s = malloc(sizeof *s);

	ListAddTail(head, &s->node);
	s->str = msgid;
	s->trans = msgstr;
	s->type = (msgstr == NULL);
	s->origin = origin;
	if (offset)
	{
		s->offset_str = offset[0];
		s->offset_trans = offset[1];
	}
	else s->offset_str = s->offset_trans = 0;
}

static STRPTR LangExtractStr(STRPTR old, STRPTR buffer)
{
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

void UnescapeAntiSlash(STRPTR src)
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

/* don't know where this code comes from ... */
Bool FnMatch(STRPTR pattern, STRPTR string, int flags)
{
	STRPTR p = pattern, n = string;
	TEXT   c;

	/* Note that this evaluates C many times.  */
# define FOLD(c) ((flags & FNM_CASEFOLD) && isupper (c) ? tolower (c) : (c))

	while ((c = *p++))
	{
		c = FOLD(c);

		switch (c) {
		case '?':
			if (*n == '\0')
				return False;
			else if ((flags & FNM_PATHNAME) && *n == '/')
				return False;
			else if ((flags & FNM_PERIOD) && *n == '.' &&
			         (n == string || ((flags & FNM_PATHNAME) && n[-1] == '/')))
				return False;
			break;

		case '\\':
			if (!(flags & FNM_NOESCAPE))
			{
				c = *p++;
				if (c == '\0')
					/* Trailing \ loses.  */
					return False;
				c = FOLD (c);
			}
			if (FOLD (*n) != c)
				return False;
			break;

		case '*':
			if ((flags & FNM_PERIOD) && *n == '.' &&
			        (n == string || ((flags & FNM_PATHNAME) && n[-1] == '/')))
				return False;

			for (c = *p++; c == '?' || c == '*'; c = *p++)
			{
				if ((flags & FNM_PATHNAME) && *n == '/')
					/* A slash does not match a wildcard under FNM_FILE_NAME */
					return False;
				else if (c == '?')
				{
					/* A ? needs to match one character */
					if (*n == '\0')
						/* There isn't another character; no match */
						return False;
					else
						++n;
				}
			}

			if (c == '\0')
				return True;

			{
				TEXT c1 = (!(flags & FNM_NOESCAPE) && c == '\\') ? *p : c;
				c1 = FOLD (c1);
				for (--p; *n != '\0'; ++n)
					if ((c == '[' || FOLD (*n) == c1) && FnMatch (p, n, flags & ~FNM_PERIOD))
						return True;
				return False;
			}

		case '[':
		{
			/* Nonzero if the sense of the character class is inverted */
			register int neg;

			if (*n == '\0') return False;

			if ((flags & FNM_PERIOD) && *n == '.' &&
			        (n == string || ((flags & FNM_PATHNAME) && n[-1] == '/')))
				return False;

			neg = (*p == '!' || *p == '^');
			if (neg) ++p;

			c = *p++;
			for (;;)
			{
				register char cstart = c, cend = c;

				if (!(flags & FNM_NOESCAPE) && c == '\\')
				{
					if (*p == '\0')
						return False;
					cstart = cend = *p++;
				}

				cstart = cend = FOLD (cstart);

				/* [ (unterminated) loses */
				if (c == '\0') return False;

				c = *p++;
				c = FOLD(c);

				if ((flags & FNM_PATHNAME) && c == '/')
					/* [/] can never match.  */
					return False;

				if (c == '-' && *p != ']')
				{
					cend = *p++;
					if (!(flags & FNM_NOESCAPE) && cend == '\\')
						cend = *p++;
					if (cend == '\0')
						return False;
					cend = FOLD(cend);

					c = *p++;
				}

				if (FOLD(*n) >= cstart && FOLD (*n) <= cend)
					goto matched;

				if (c == ']') break;
			}
			if (! neg)
				return False;
			break;

			matched:

			/* Skip the rest of the [...] that already matched.  */
			while (c != ']')
			{
				/* [... (unterminated) loses.  */
				if (c == '\0') return False;

				c = *p++;
				if (!(flags & FNM_NOESCAPE) && c == '\\')
				{
					if (*p == '\0') return False;
					/* XXX 1003.2d11 is unclear if this is right.  */
					++p;
				}
			}
			if (neg) return False;
		}
		break;

		default:
			if (c != FOLD (*n))
				return False;
		}
		++n;
	}

	if (*n == '\0') return True;

	if ((flags & FNM_LEADING_DIR) && *n == '/')
		/* The FNM_LEADING_DIR flag says that "foo*" matches "foobar/frobozz".  */
		return True;

	return False;

# undef FOLD
}
