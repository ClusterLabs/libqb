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

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <pthread.h>

#include <stdint.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <qb/qbdefs.h>

/**
 * @file qbutil.h
 * These are some convience functions used throughout libqb.
 *
 * @author Angus Salkeld <asalkeld@redhat.com>
 *
 * @par Locking
 * - qb_thread_lock_create()
 * - qb_thread_lock()
 * - qb_thread_trylock()
 * - qb_thread_unlock()
 * - qb_thread_lock_destroy()
 *
 * @par Time functions
 * - qb_timespec_add_ms()
 * - qb_util_nano_current_get()
 * - qb_util_nano_monotonic_hz()
 * - qb_util_nano_from_epoch_get()
 * - qb_util_timespec_from_epoch_get()
 *
 * @par Basic Stopwatch
 * @code
 * uint64_t elapsed1;
 * uint64_t elapsed2;
 * qb_util_stopwatch_t *sw = qb_util_stopwatch_create();
 *
 * qb_util_stopwatch_start(sw);
 *
 * usleep(sometime);
 * qb_util_stopwatch_stop(sw);
 * elapsed1 = qb_util_stopwatch_us_elapsed_get(sw);
 *
 * usleep(somemoretime);
 * qb_util_stopwatch_stop(sw);
 * elapsed2 = qb_util_stopwatch_us_elapsed_get(sw);
 *
 * qb_util_stopwatch_free(sw);
 * @endcode
 *
 * @par Stopwatch with splits
 * Setup a stopwatch with space for 3 splits.
 *
 * @code
 * uint64_t split;
 * qb_util_stopwatch_t *sw = qb_util_stopwatch_create();
 *
 * qb_util_stopwatch_split_ctl(sw, 3, 0);
 * qb_util_stopwatch_start(sw);
 *
 * usleep(sometime);
 * qb_util_stopwatch_split(sw);
 *
 * usleep(somemoretime);
 * qb_util_stopwatch_split(sw);
 *
 * usleep(somemoretime);
 * qb_util_stopwatch_split(sw);
 *
 * idx = qb_util_stopwatch_split_last(sw);
 * do {
 *      split = qb_util_stopwatch_time_split_get(sw, idx, idx);
 *      qb_log(LOG_INFO, "split %d is %"PRIu64"", last, split);
 *      idx--;
 * } while (split > 0);
 *
 * split = qb_util_stopwatch_time_split_get(sw, 2, 1);
 * qb_log(LOG_INFO, "time between second and third split is %"PRIu64"", split);
 *
 * qb_util_stopwatch_free(sw);
 * @endcode
 *
 */

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
void qb_util_set_log_function(qb_util_log_fn_t fn) QB_GNUC_DEPRECATED;

/**
 * Add milliseconds onto the timespec.
 * @param ts the ts to add to
 * @param ms the amount of milliseconds to increment ts
 */
void qb_timespec_add_ms(struct timespec *ts, int32_t ms);

/**
 * Get the current number of nano secounds produced
 * by the systems incrementing clock (CLOCK_MONOTOMIC
 * if available).
 */
uint64_t qb_util_nano_current_get(void);

/**
 * Get the frequence of the clock used in
 * qb_util_nano_current_get().
 */
uint64_t qb_util_nano_monotonic_hz(void);

/**
 * Get the time in nano seconds since epoch.
 */
uint64_t qb_util_nano_from_epoch_get(void);

/**
 * Get the time in timespec since epoch.
 * @param ts (out) the timespec
 * @return status (0 == ok, -errno on error)
 */
void qb_util_timespec_from_epoch_get(struct timespec *ts);

/**
 * strerror_r replacement.
 */
char *qb_strerror_r(int errnum, char *buf, size_t buflen);


typedef struct qb_util_stopwatch qb_util_stopwatch_t;

#define QB_UTIL_SW_OVERWRITE 0x01


/**
 * Create a Stopwatch (to time operations)
 */
qb_util_stopwatch_t * qb_util_stopwatch_create(void);

/**
 * Free the stopwatch
 */
void qb_util_stopwatch_free(qb_util_stopwatch_t *sw);

/**
 * Start the stopwatch
 *
 * This also acts as a reset. Essentially it sets the
 * starting time and clears the splits.
 */
void qb_util_stopwatch_start(qb_util_stopwatch_t *sw);

/**
 * Stop the stopwatch
 *
 * This just allows you to get the elapsed time. So
 * you can call this multiple times. Do not call qb_util_stopwatch_start()
 * unless you want to reset the stopwatch.
 */
void qb_util_stopwatch_stop(qb_util_stopwatch_t *sw);

/**
 * Get the elapsed time in micro seconds.
 *
 * (it must have been started and stopped).
 */
uint64_t qb_util_stopwatch_us_elapsed_get(qb_util_stopwatch_t *sw);

/**
 * Get the elapsed time in seconds.
 *
 * (it must have been started and stopped).
 */
float qb_util_stopwatch_sec_elapsed_get(qb_util_stopwatch_t *sw);

/**
 *
 * @param sw the stopwatch
 * @param max_splits maximum number of time splits
 * @param options (0 or QB_UTIL_SW_OVERWRITE )
 * @retval 0 on success
 * @retval -errno on failure
 */
int32_t qb_util_stopwatch_split_ctl(qb_util_stopwatch_t *sw,
        uint32_t max_splits, uint32_t options);

/**
 * Create a new time split (or lap time)
 *
 * @param sw the stopwatch
 * @retval the relative split time in micro seconds
 * @retval 0 if no more splits available
 */
uint64_t qb_util_stopwatch_split(qb_util_stopwatch_t *sw);

/**
 * Get the last split index to be used by
 * qb_util_stopwatch_time_split_get()
 *
 * @note this is zero based
 *
 * @param sw the stopwatch
 * @return the last entry index
 */
uint32_t
qb_util_stopwatch_split_last(qb_util_stopwatch_t *sw);

/**
 * Read the time split (in us) from "receint" to "older".
 *
 * If older == receint then the cumulated split will be
 * returned (from the stopwatch start).
 *
 * @param sw the stopwatch
 * @param receint split
 * @param older split
 * @retval the split time in micro seconds
 * @retval 0 if not a valid split
 */
uint64_t
qb_util_stopwatch_time_split_get(qb_util_stopwatch_t *sw,
				 uint32_t receint, uint32_t older);

/** Structured library versioning info */
extern const struct qb_version {
	uint8_t major; /**< Major component */
	uint8_t minor; /**< Minor component */
	uint8_t micro; /**< Micro component */
	const char *rest; /**< Rest (pertaining the mid-release-point) */
} qb_ver;

/** Complete library versioning info as a string */
extern const char *const qb_ver_str;

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_UTIL_H_DEFINED */
