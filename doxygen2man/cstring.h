#ifndef __CSTRING_H__
#define __CSTRING_H__

typedef void* cstring_t;

cstring_t cstring_alloc(void);
cstring_t cstring_from_chars(const char* chars);
cstring_t cstring_dup(cstring_t string);
char *cstring_to_chars(cstring_t cstring);
cstring_t cstring_append_chars(cstring_t cstring, const char *newstring);
cstring_t cstring_append_cstring(cstring_t cstring, cstring_t newstring);
void cstring_free(cstring_t cstring);
size_t cstring_len(cstring_t cstring);

#endif
