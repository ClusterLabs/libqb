/*
 * Simulate FORCESOCKETSFILE existing for the IPC tests
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/stat.h>
#include "../include/config.h"

// __xstat for earlier libc
int __xstat(int __ver, const char *__filename, struct stat *__stat_buf)
{
#if defined(QB_LINUX) || defined(QB_CYGWIN)
	static int opened = 0;
	static int (*real_xstat)(int __ver, const char *__filename, void *__stat_buf);

	if (!opened) {
		real_xstat = dlsym(RTLD_NEXT, "__xstat");
		opened = 1;
	}

	if (strcmp(__filename, FORCESOCKETSFILE) == 0) {
		fprintf(stderr, "__xstat called for %s\n", __filename);
		return 0; /* it exists! */
	}

	return real_xstat(__ver, __filename, __stat_buf);
#else
	return -1; /* Error in the unlikely event we get called on *BSD* */
#endif
}

// stat for F35 and later
int stat(const char *__filename, struct stat *__stat_buf)
{
#if defined(QB_LINUX) || defined(QB_CYGWIN)
	static int opened = 0;
	static int (*real_stat)(const char *__filename, void *__stat_buf);

	if (!opened) {
		real_stat = dlsym(RTLD_NEXT, "stat");
		opened = 1;
	}

	if (strcmp(__filename, FORCESOCKETSFILE) == 0) {
		fprintf(stderr, "stat called for %s\n", __filename);
		return 0; /* it exists! */
	}

	return real_stat(__filename, __stat_buf);
#else
	return -1; /* Error in the unlikely event we get called on *BSD* */
#endif
}
