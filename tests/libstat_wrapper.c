/*
 * Simulate FORCESOCKETSFILE existing for the IPC tests
 */

#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/stat.h>
#include "../include/config.h"
#if defined(QB_LINUX) || defined(QB_CYGWIN)
#include <gnu/lib-names.h>
#endif

// __xstat for ealier libc
int __xstat(int __ver, const char *__filename, struct stat *__stat_buf)
{
#if defined(QB_LINUX) || defined(QB_CYGWIN)
	static int opened = 0;
	static void *dlhandle;
	static int (*real_xstat)(int __ver, const char *__filename, void *__stat_buf);

	if (!opened) {
		dlhandle = dlopen(LIBC_SO, RTLD_NOW);
		real_xstat = dlsym(dlhandle, "__xstat");
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

// Stat for F35 and later
int stat(const char *__filename, struct stat *__stat_buf)
{
#if defined(QB_LINUX) || defined(QB_CYGWIN)
	static int opened = 0;
	static void *dlhandle;
	static int (*real_stat)(const char *__filename, void *__stat_buf);

	if (!opened) {
		dlhandle = dlopen(LIBC_SO, RTLD_NOW);
		real_stat = dlsym(dlhandle, "stat");
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
