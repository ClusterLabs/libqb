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

#ifndef QB_DEFS_H_DEFINED
#define QB_DEFS_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbdefs.h
 * These are some convience macros and defines.
 *
 * @author Angus Salkeld <asalkeld@redhat.com>
 */

/*
 * simple math macros
 */
#define QB_ROUNDUP(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#define QB_ABS(i)		(((i) < 0) ? -(i) : (i))
#define QB_MAX(a, b)		(((a) > (b)) ? (a) : (b))
#define QB_MIN(a, b)		(((a) < (b)) ? (a) : (b))

/*
 * the usual boolean defines
 */
#define	QB_FALSE		0
#define	QB_TRUE			(!QB_FALSE)

/*
 * bit manipulation
 */
#define qb_bit_value(bit) (1U << (bit))
#define qb_bit_set(barray, bit) (barray |= qb_bit_value(bit))
#define qb_bit_clear(barray, bit) (barray &= ~(qb_bit_value(bit)))
#define qb_bit_is_set(barray, bit) (barray & qb_bit_value(bit))
#define qb_bit_is_clear(barray, bit) (!(barray & qb_bit_value(bit)))

/*
 * wrappers over preprocessor operators
 */

#define QB_PP_JOIN_(a, b)	a##b
#define QB_PP_JOIN(a, b)	QB_PP_JOIN_(a, b)
#define QB_PP_STRINGIFY_(arg)	#arg
#define QB_PP_STRINGIFY(arg)	QB_PP_STRINGIFY_(arg)


/*
 * handy time based converters.
 */
#ifndef HZ
#define HZ 100			/* 10ms */
#endif

#define QB_TIME_MS_IN_SEC   1000ULL
#define QB_TIME_US_IN_SEC   1000000ULL
#define QB_TIME_NS_IN_SEC   1000000000ULL
#define QB_TIME_US_IN_MSEC  1000ULL
#define QB_TIME_NS_IN_MSEC  1000000ULL
#define QB_TIME_NS_IN_USEC  1000ULL


#if defined (__GNUC__) && defined (__STRICT_ANSI__)
#undef inline
#define inline __inline__
#undef typeof
#define typeof __typeof__
#endif /* ANSI */

#if  __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
#define QB_GNUC_DEPRECATED                            \
  __attribute__((__deprecated__))
#else
#define QB_GNUC_DEPRECATED
#endif /* __GNUC__ */

#if    __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define QB_GNUC_DEPRECATED_FOR(f)                        \
  __attribute__((deprecated("Use " #f " instead")))
#else
#define QB_GNUC_DEPRECATED_FOR(f)        QB_GNUC_DEPRECATED
#endif /* __GNUC__ */

#if     __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
#define QB_GNUC_MAY_ALIAS __attribute__((may_alias))
#else
#define QB_GNUC_MAY_ALIAS
#endif

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_DEFS_H_DEFINED */
