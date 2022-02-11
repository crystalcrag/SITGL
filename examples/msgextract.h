/*
 * msgextract.h
 */

#ifndef	MSGEXTRACT_H
#define	MSGEXTRACT_H

typedef struct Msg_t *        Msg;
typedef struct LangFile_t *   LangFile;
typedef struct String_t *     String;
typedef struct LangStack_t    LangStack;

struct Msg_t
{
	ListNode node;
	int      flag;
	uint8_t  split[8];
	TEXT     text[1];
};

struct String_t
{
	ListNode node;
	STRPTR   str;
	STRPTR   trans;
	int      offset_str;
	int      offset_trans;
	int      type;
	int      origin;
	int      check;
};

struct LangFile_t
{
	ListNode node;
	STRPTR   path;
	Lang     file;
};

struct LangStack_t
{
	STRPTR buffer;
	STRPTR file;
	STRPTR eol;
};

struct Lang_t    /* typedef struct Lang_t *   defined in UtilityLib.h */
{
	TEXT      name[32];
	ListHead  strings;
	LangStack files[32];
	int       count, stack;
};

enum /* FnMatch flags */
{
	FNM_PATHNAME    = 1,
	FNM_NOESCAPE    = 2,
	FNM_PERIOD      = 4,
	FNM_LEADING_DIR = 8,
	FNM_CASEFOLD    = 16
};

static void ParseLangs(STRPTR dir);
static Lang ParseLang(STRPTR name, STRPTR path);
static void ExtractMessages(STRPTR folder, STRPTR prefix, STRPTR suffix, Bool escape);
static STRPTR LangExtractStr(STRPTR old, STRPTR buffer);
static void LangAddLine(ListHead *, STRPTR msgid, STRPTR msgstr, int * offset, int origin);
static void WriteString(STRPTR prefix1, STRPTR prefix2, Msg msg, FILE * out);
static void LangRewrite(LangFile l);
static void FreeLang(Lang);
static int LangCompare(STRPTR msg1, STRPTR msg2);

Bool FnMatch(STRPTR pattern, STRPTR string, int flags);
void UnescapeAntiSlash(STRPTR src);

#endif

