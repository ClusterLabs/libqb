/* A really basic expanding/appendable string type */

#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include "cstring.h"

#define INITIAL_SIZE 1024
#define INCREMENT    1024
#define CHECKER_WORD 0xbcd6712a

struct cstring_header
{
	size_t checker;
	size_t allocated;
	size_t used;
	char the_string[];
};

cstring_t cstring_alloc(void)
{
	char *cstring = malloc(INITIAL_SIZE);
	if (cstring) {
		struct cstring_header *h = (struct cstring_header *)cstring;
		h->checker = CHECKER_WORD;
		h->allocated = INITIAL_SIZE;
		h->used = 0;
		h->the_string[0] = '\0';
		return cstring;
	} else {
		return NULL;
	}
}

char *cstring_to_chars(cstring_t cstring)
{
	struct cstring_header *h = (struct cstring_header *)cstring;

	if (!h) {
		return NULL;
	}

	assert(h->checker == CHECKER_WORD);
	return strdup(h->the_string);
}

size_t cstring_len(cstring_t cstring)
{
	struct cstring_header *h = (struct cstring_header *)cstring;

	if (!h) {
		return 0;
	}

	assert(h->checker == CHECKER_WORD);
	return h->used;
}


cstring_t cstring_append_chars(cstring_t cstring, const char *newstring)
{
	struct cstring_header *h = (struct cstring_header *)cstring;
	size_t newlen;

	if (!h) {
		return NULL;
	}

	assert(h->checker == CHECKER_WORD);
	if (!newstring) {
		return NULL;
	}

	newlen = h->used + strlen(newstring)+1 + sizeof(struct cstring_header);
	if (newlen > h->allocated) {
		size_t new_allocsize = (newlen + 2048) & 0xFFFFFC00;
		char *tmp = realloc(cstring, new_allocsize);
		if (!tmp) {
			return cstring;
		}

		cstring = tmp;
		h = (struct cstring_header *)cstring;
		h->allocated = new_allocsize;
	}
	strncat(h->the_string, newstring, h->allocated - h->used -1);
	h->used += strlen(newstring);
	return cstring;
}

cstring_t cstring_append_cstring(cstring_t cstring, cstring_t newstring)
{
	/* Just check the newstring - cstring_append_chars() will check the target */
	struct cstring_header *h = (struct cstring_header *)newstring;

	if (!h) {
		return NULL;
	}

	assert(h->checker == CHECKER_WORD);
	return cstring_append_chars(cstring, h->the_string);
}

cstring_t cstring_from_chars(const char* chars)
{
	cstring_t new_string = cstring_alloc();
	if (!new_string) {
		return NULL;
	}
	return cstring_append_chars(new_string, chars);
}

void cstring_free(cstring_t cstring)
{
	struct cstring_header *h = (struct cstring_header *)cstring;

	if (!h) {
		return;
	}
	assert(h->checker == CHECKER_WORD);
	free(cstring);
}


