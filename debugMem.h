/*
 * debugMem.h : only want to track what's allocated by SIT, using heavy weight tools is too cumbersome for this
 *
 * written by T.Pierron, june 2020.
 */

#ifndef DEBUG_IMPL
#define malloc(x)     SIT_malloc(x, __func__, __LINE__)
#define calloc(x,y)   SIT_calloc(x, y, __func__, __LINE__)
#define realloc(x,y)  SIT_realloc(x, y, __func__, __LINE__)
#define strdup(x)     SIT_strdup(x, __func__, __LINE__)
#define free          SIT_free
void * SIT_malloc(int size, const char * func, int line);
void * SIT_calloc(int nb, int size, const char * func, int line);
void * SIT_realloc(void * old, int size, const char * func, int line);
STRPTR SIT_strdup(STRPTR str, const char * func, int line);
void   SIT_free(void *);
void   SIT_DebugMalloc(int full);
#else
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef free
static int allocCall, allocMem;
static ListHead alloc;

struct MemInfo_t
{
	ListNode node;
	const char * func;
	int      line;
	int      size;
};

typedef struct MemInfo_t *   MemInfo;

#define MEMBLOCK     (sizeof (struct MemInfo_t))

void * SIT_malloc(int size, const char * func, int line)
{
	DATA8 mem = malloc(size + MEMBLOCK);

	if (mem)
	{
		//fprintf(stderr, "malloc %d\n", size);
		MemInfo info = (MemInfo) mem;
		info->func = func;
		info->line = line;
		info->size = size;
		ListAddTail(&alloc, &info->node);
		allocCall ++;
		allocMem += size;
		return mem + MEMBLOCK;
	}
	return NULL;
}

void * SIT_calloc(int nb, int size, const char * func, int line)
{
	size *= nb;
	DATA8 mem = calloc(size + MEMBLOCK, 1);
	if (mem)
	{
		//fprintf(stderr, "calloc %d\n", size);
		MemInfo info = (MemInfo) mem;
		info->func = func;
		info->line = line;
		info->size = size;
		ListAddTail(&alloc, &info->node);
		allocCall ++;
		allocMem += size;
		return mem + MEMBLOCK;
	}
	return NULL;
}

void * SIT_realloc(void * old, int size, const char * func, int line)
{
	if (old)
	{
		old -= MEMBLOCK;
		MemInfo info = (MemInfo) old;
		if (info->size == size)
			return old + MEMBLOCK;
		allocMem -= info->size;
		ListRemove(&alloc, &info->node);
	}
	DATA8 mem = realloc(old, size + MEMBLOCK);
	if (mem)
	{
		MemInfo info = (MemInfo) mem;
		info->size = size;
		if (old == NULL)
		{
			info->func = func;
			info->line = line;
		}
		ListAddTail(&alloc, &info->node);
		//fprintf(stderr, "realloc %d\n", size);
		allocCall ++;
		allocMem += size;
		return mem + MEMBLOCK;
	}
	return NULL;
}

STRPTR SIT_strdup(STRPTR str, const char * func, int line)
{
	int    size = strlen(str) + 1;
	STRPTR mem  = malloc(size + MEMBLOCK);

	if (mem)
	{
		MemInfo info = (MemInfo) mem;
		info->func = func;
		info->line = line;
		info->size = size;
		ListAddTail(&alloc, &info->node);
		allocCall ++;
		allocMem += size;
		strcpy(mem + MEMBLOCK, str);
		return mem + MEMBLOCK;
	}
	return NULL;
}

void SIT_free(void * mem)
{
	if (mem)
	{
		MemInfo info = mem - MEMBLOCK;
		allocMem -= info->size;
		ListRemove(&alloc, &info->node);
		free(info);
	}
}

void SIT_DebugMalloc(int full)
{
	MemInfo info;
	int     count;
	for (count = 0, info = HEAD(alloc); info; NEXT(info), count ++);
	fprintf(stderr, "alloc call = %d, alloc to free = %d, alloc mem = %d\n", allocCall, count, allocMem);
	if (full && alloc.lh_Head)
	{
		for (info = HEAD(alloc); info; NEXT(info))
		{
			fprintf(stderr, "- %d bytes in %s:%d\n", info->size, info->func, info->line);
		}
	}

	if (sit.images.lh_Head)
	{
		CSSImage img;
		fprintf(stderr, "image loaded\n");
		for (img = HEAD(sit.images); img; NEXT(img))
		{
			fprintf(stderr, "%s: %dx%d, usage: %d\n", img->bitmap, img->width, img->height, img->usage);
		}
	}
}
#endif
