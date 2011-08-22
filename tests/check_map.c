/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of libqb.
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
#include "os_base.h"
#include <check.h>
#include "qbdefs.h"
#include "qblog.h"
#include "qbmap.h"

char chars[] =
    "0123456789" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz";

char chars2[] = "0123456789" "abcdefghijklmnopqrstuvwxyz";

static void *destroyed_key = NULL;
static void *destroyed_value = NULL;
static int global_data = 123;

static int32_t
my_char_compare(const void *a, const void *b, void* data)
{
	const char *cha = a;
	const char *chb = b;

	return *cha - *chb;
}

static int32_t
my_str_compare(const void *a, const void *b, void* data)
{
	const char *cha = a;
	const char *chb = b;

	return strcmp(cha, chb);
}

static void
test_map_simple(qb_map_t *m)
{
	qb_map_put(m, "k1", "one");
	qb_map_put(m, "k2", "two");
	qb_map_put(m, "k3", "three");
	ck_assert_int_eq(qb_map_count_get(m), 3);
	qb_map_put(m, "k4", "four");

	ck_assert_int_eq(qb_map_count_get(m), 4);

	ck_assert_str_eq(qb_map_get(m, "k3"), "three");
	ck_assert_str_eq(qb_map_get(m, "k1"), "one");
	ck_assert_str_eq(qb_map_get(m, "k4"), "four");

	qb_map_rm(m, "k2");
	ck_assert_int_eq(qb_map_count_get(m), 3);
	qb_map_put(m, "9k", "nine");

	qb_map_put(m, "k3", "not_three");
	ck_assert_str_eq(qb_map_get(m, "k3"), "not_three");
	ck_assert_int_eq(qb_map_count_get(m), 4);

	qb_map_destroy(m);
}

static int32_t
my_compare_with_data(const void *a, const void *b, void *user_data)
{
	const char *cha = a;
	const char *chb = b;
	int * dp = (int*)user_data;

	/* just check that we got the right data */
	ck_assert_int_eq(*dp, 123);

	return *cha - *chb;
}

static int32_t
my_traverse(void *key, void *value, void *data)
{
	char *ch = key;
	ck_assert((*ch) > 0);
	return QB_FALSE;
}

static int32_t
check_order(void *key, void *value, void *data)
{
	char **p = data;
	char *ch = key;

	ck_assert(**p == *ch);

	(*p)++;

	return QB_FALSE;
}

static void
test_map_search(qb_map_t* m)
{
	int32_t i;
	int32_t removed;
	char c;
	char *p;

	for (i = 0; chars[i]; i++) {
		qb_map_put(m, &chars[i], &chars[i]);
	}
	qb_map_foreach(m, my_traverse, NULL);

	ck_assert_int_eq(qb_map_count_get(m), strlen(chars));

	p = chars;
	qb_map_foreach(m, check_order, &p);

	for (i = 0; i < 26; i++) {
		removed = qb_map_rm(m, &chars[i + 10]);
		ck_assert(removed);
	}

	c = '\0';
	removed = qb_map_rm(m, &c);
	ck_assert(!removed);

	qb_map_foreach(m, my_traverse, NULL);

	ck_assert_int_eq(qb_map_count_get(m), strlen(chars2));

	p = chars2;
	qb_map_foreach(m, check_order, &p);

	for (i = 25; i >= 0; i--) {
		qb_map_put(m, &chars[i + 10], &chars[i + 10]);
	}
	p = chars;
	qb_map_foreach(m, check_order, &p);

	c = '0';
	p = qb_map_get(m, &c);
	ck_assert(p && *p == c);

	c = 'A';
	p = qb_map_get(m, &c);
	ck_assert(p && *p == c);

	c = 'a';
	p = qb_map_get(m, &c);
	ck_assert(p && *p == c);

	c = 'z';
	p = qb_map_get(m, &c);
	ck_assert(p && *p == c);

	c = '!';
	p = qb_map_get(m, &c);
	ck_assert(p == NULL);

	c = '=';
	p = qb_map_get(m, &c);
	ck_assert(p == NULL);

	c = '|';
	p = qb_map_get(m, &c);
	ck_assert(p == NULL);

	qb_map_destroy(m);
}

static void
my_key_destroy(void *key)
{
	destroyed_key = key;
}

static void
my_value_destroy(void *value)
{
	destroyed_value = value;
}

static void
test_map_remove(qb_map_t *tree)
{
	char a, b, c, d;
	int32_t i;
	int32_t removed;
	const char *remove_ch;

	for (i = 0; chars[i]; i++) {
		qb_map_put(tree, &chars[i], &chars[i]);
	}

	a = '0';
	qb_map_put(tree, &a, &a);
	ck_assert(destroyed_key == &chars[0]);
	ck_assert(destroyed_value == &chars[0]);
	destroyed_key = NULL;
	destroyed_value = NULL;

	b = '5';
	removed = qb_map_rm(tree, &b);
	ck_assert(removed);
	ck_assert(destroyed_key == &chars[5]);
	ck_assert(destroyed_value == &chars[5]);
	destroyed_key = NULL;
	destroyed_value = NULL;

	d = '1';
	qb_map_put(tree, &d, &d);
	ck_assert(destroyed_key == &chars[1]);
	ck_assert(destroyed_value == &chars[1]);
	destroyed_key = NULL;
	destroyed_value = NULL;

	c = '2';
	removed = qb_map_rm(tree, &c);
	ck_assert(removed);
	ck_assert(destroyed_key == &chars[2]);
	ck_assert(destroyed_value == &chars[2]);
	destroyed_key = NULL;
	destroyed_value = NULL;

	remove_ch = "omkjigfedba";
	for (i = 0; remove_ch[i]; i++) {
		removed = qb_map_rm(tree, &remove_ch[i]);
		ck_assert(removed);
	}

	qb_map_destroy(tree);
}

static int32_t
traverse_func(void *key, void *value, void *data)
{
	char *c = value;
	char **p = data;

	**p = *c;
	(*p)++;

	return QB_FALSE;
}

static void
test_map_traverse_ordered(qb_map_t *tree)
{
	int32_t i;
	char *p, *result;

	for (i = 0; chars[i]; i++) {
		qb_map_put(tree, &chars[i], &chars[i]);
	}
	result = calloc(sizeof(char), strlen(chars) + 1);

	p = result;
	qb_map_foreach(tree, traverse_func, &p);
	ck_assert_str_eq(result,
			 "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	qb_map_destroy(tree);
}

static int32_t
traverse_and_remove_func(void *key, void *value, void *data)
{
	qb_map_t *m = (qb_map_t *)data;
	qb_map_rm(m, key);
	qb_map_put(m, (char*)key + 20, key);
	return QB_FALSE;
}

static void
test_map_traverse_unordered(qb_map_t *tree)
{
	int32_t i;
	for (i = 0; i < 20; i++) {
		qb_map_put(tree, &chars[i], &chars[i]);
	}
	qb_map_foreach(tree, traverse_and_remove_func, tree);
}


static int32_t
my_counter_traverse(void *key, void *value, void *data)
{
	int32_t *c = (int32_t*)data;
	(*c)++;
	return QB_FALSE;
}

static void
test_map_load(qb_map_t *m, const char* test_name)
{
	char word[1000];
	char *w;
	FILE *fp;
	int32_t res = 0;
	int32_t count;
	int32_t count2;
	float ops;
	void *value;
	qb_util_stopwatch_t *sw;

	ck_assert(m != NULL);
	sw =  qb_util_stopwatch_create();

	/*
	 * Load with dictionary
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	count = 0;
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		w = strdup(word);
		qb_map_put(m, w, w);
		count++;
	}
	qb_util_stopwatch_stop(sw);
	ck_assert_int_eq(qb_map_count_get(m), count);
	fclose(fp);

	ops = count / qb_util_stopwatch_sec_elapsed_get(sw);
	qb_log(LOG_INFO, "%s %9.3f puts/sec\n", test_name, ops);

	/*
	 * Verify dictionary produces correct values
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		value = qb_map_get(m, word);
		ck_assert_str_eq(word, value);
	}
	qb_util_stopwatch_stop(sw);
	fclose(fp);

	ops = count / qb_util_stopwatch_sec_elapsed_get(sw);
	qb_log(LOG_INFO, "%s %9.3f gets/sec\n", test_name, ops);

	/*
	 * time the iteration
	 */
	count2 = 0;
	qb_util_stopwatch_start(sw);
	qb_map_foreach(m, my_counter_traverse, &count2);
	qb_util_stopwatch_stop(sw);
	ops = count2 / qb_util_stopwatch_sec_elapsed_get(sw);
	qb_log(LOG_INFO, "%s %9.3f iterations/sec\n", test_name, ops);

	/*
	 * Delete all dictionary entries
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		res = qb_map_rm(m, word);
		ck_assert_int_eq(res, QB_TRUE);
	}
	ck_assert_int_eq(qb_map_count_get(m), 0);
	qb_util_stopwatch_stop(sw);
	fclose(fp);

	ops = count / qb_util_stopwatch_sec_elapsed_get(sw);
	qb_log(LOG_INFO, "%s %9.3f deletes/sec\n", test_name, ops);
}

START_TEST(test_skiplist_simple)
{
	qb_map_t *m = qb_skiplist_create(my_str_compare, NULL, NULL, NULL);
	test_map_simple(m);
}
END_TEST

START_TEST(test_hashtable_simple)
{
	qb_map_t *m = qb_hashtable_create(my_str_compare, NULL, NULL, NULL,
					  32, qb_hash_string);
	test_map_simple(m);
}
END_TEST

START_TEST(test_skiplist_search)
{
	qb_map_t *m = qb_skiplist_create(my_compare_with_data,
					 &global_data, NULL, NULL);
	test_map_search(m);
}
END_TEST

START_TEST(test_skiplist_remove)
{
	qb_map_t *tree = qb_skiplist_create(my_char_compare, NULL,
					    my_key_destroy,
					    my_value_destroy);
	test_map_remove(tree);
}
END_TEST

START_TEST(test_hashtable_remove)
{
	qb_map_t *tree = qb_hashtable_create(my_char_compare, NULL,
					     my_key_destroy,
					     my_value_destroy,
					     256, qb_hash_char);
	test_map_remove(tree);
}
END_TEST

START_TEST(test_skiplist_traverse)
{
	qb_map_t *m;
	m = qb_skiplist_create(my_char_compare, NULL, NULL, NULL);
	test_map_traverse_ordered(m);

	m = qb_skiplist_create(my_char_compare, NULL, NULL, NULL);
	test_map_traverse_unordered(m);
}
END_TEST

START_TEST(test_hashtable_traverse)
{
	qb_map_t *m = qb_hashtable_create(my_char_compare, NULL, NULL, NULL,
					  256, qb_hash_char);
	test_map_traverse_unordered(m);
}
END_TEST

START_TEST(test_skiplist_load)
{
	qb_map_t *m;
	if (access("/usr/share/dict/words", R_OK) != 0) {
		printf("no dict/words - not testing\n");
		return;
	}
	m = qb_skiplist_create(my_str_compare, NULL, NULL, NULL);
	test_map_load(m, __func__);
}
END_TEST

START_TEST(test_hashtable_load)
{
	qb_map_t *m;
	if (access("/usr/share/dict/words", R_OK) != 0) {
		printf("no dict/words - not testing\n");
		return;
	}
	m = qb_hashtable_create(my_str_compare, NULL, NULL, NULL,
				100000, qb_hash_string);
	test_map_load(m, __func__);
}
END_TEST

static Suite *
map_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("qb_map");

	tc = tcase_create("skiplist_simple");
	tcase_add_test(tc, test_skiplist_simple);
	suite_add_tcase(s, tc);

	tc = tcase_create("hashtable_simple");
	tcase_add_test(tc, test_hashtable_simple);
	suite_add_tcase(s, tc);

	tc = tcase_create("skiplist_remove");
	tcase_add_test(tc, test_skiplist_remove);
	suite_add_tcase(s, tc);

	tc = tcase_create("hashtable_remove");
	tcase_add_test(tc, test_hashtable_remove);
	suite_add_tcase(s, tc);

	tc = tcase_create("skiplist_search");
	tcase_add_test(tc, test_skiplist_search);
	suite_add_tcase(s, tc);

/*
 * 	No hashtable_search as it assumes an ordered
 *	collection
 */

	tc = tcase_create("skiplist_traverse");
	tcase_add_test(tc, test_skiplist_traverse);
	suite_add_tcase(s, tc);

	tc = tcase_create("hashtable_traverse");
	tcase_add_test(tc, test_hashtable_traverse);
	suite_add_tcase(s, tc);

	tc = tcase_create("skiplist_load");
	tcase_add_test(tc, test_skiplist_load);
	tcase_set_timeout(tc, 10);
	suite_add_tcase(s, tc);

	tc = tcase_create("hashtable_load");
	tcase_add_test(tc, test_hashtable_load);
	tcase_set_timeout(tc, 20);
	suite_add_tcase(s, tc);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;

	Suite *s = map_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_format_set(QB_LOG_STDERR, "%f:%l %p %b");
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
