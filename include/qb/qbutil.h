/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QB_UTIL_H_DEFINED
#define QB_UTIL_H_DEFINED

/**
 * @file qbutil.h
 * @author Angus Salkeld <asalkeld@redhat.com>
 *
 * These are some convience functions used throughout libqb.
 */

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @typedef qb_thread_lock_type_t
 * QB_THREAD_LOCK_SHORT is a short term lock (spinlock if available on your system)
 * QB_THREAD_LOCK_LONG is a mutex
 */
typedef enum {
	QB_THREAD_LOCK_SHORT,
	QB_THREAD_LOCK_LONG,
} qb_thread_lock_type_t;

struct qb_thread_lock_s;
typedef struct qb_thread_lock_s qb_thread_lock_t;

/**
 * Create a new lock of the given type.
 * @param type QB_THREAD_LOCK_SHORT == spinlock (where available, else mutex)
 *        QB_THREAD_LOCK_LONG == mutex 
 * @return pointer to qb_thread_lock_type_t or NULL on error.
 */
qb_thread_lock_t *qb_thread_lock_create(qb_thread_lock_type_t type);

/**
 * Calls either pthread_mutex_lock() or pthread_spin_lock().
 */
int32_t qb_thread_lock(qb_thread_lock_t * tl);

/**
 * Calls either pthread_mutex_trylock() or pthread_spin_trylock().
 */
int32_t qb_thread_trylock(qb_thread_lock_t * tl);

/**
 * Calls either pthread_mutex_unlock() or pthread_spin_unlock.
 */
int32_t qb_thread_unlock(qb_thread_lock_t * tl);

/**
 * Calls either pthread_mutex_destro() or pthread_spin_destroy().
 */
int32_t qb_thread_lock_destroy(qb_thread_lock_t * tl);

typedef void (*qb_util_log_fn_t) (const char *file_name,
				  int32_t file_line,
				  int32_t severity, const char *msg);

/**
 * Use this function to output libqb internal log message as you wish.
 */
void qb_util_set_log_function(qb_util_log_fn_t fn);

/**
 * Create a file to be used to back shared memory.
 *
 * @param path (out) the final absolute path of the file.
 * @param file (in) the name of the file to be used.
 * @param bytes the size to truncate the file to.
 * @param file_flags same as passed into open()
 * @return 0 (success) or -1 (error)
 */
int32_t qb_util_mmap_file_open(char *path, const char *file, size_t bytes,
			       uint32_t file_flags);

/**
 * Create a shared mamory circular buffer.
 *
 * @param fd an open file to use to back the shared memory.
 * @param buf (out) the pointer to the start of the memory.
 * @param bytes the size of the shared memory.
 * @return 0 (success) or -1 (error)
 */
int32_t qb_util_circular_mmap(int32_t fd, void **buf, size_t bytes);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_UTIL_H_DEFINED */
