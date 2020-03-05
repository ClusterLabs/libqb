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

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>

const char *chars[] = {
	"0","1","2","3","4","5","6","7","8","9",
	"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
	"a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z",
	NULL,
};

const char *chars2[] = {
	"0","1","2","3","4","5","6","7","8","9",
	"a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z",
	NULL,
};

const char *composers[] = {
	"Béla Bartók",
	"Zoltán Kodály",
	"Ludwig van Beethoven",
	"Wolfgang Amadeus Mozart",
	"Leoš Janáček",
	"Benjamin Britten",
	"Josef Haydn",
	"Claude Debussy",
	"Charles Ives",
	/* Maybe in an alien language ... but they can cause trie crashes & conflicts */
	"\x7e\x7f\x80\x81", "\x7e", "\x7e\x7f", "\x7e\x7f\x80",
};


static char *notified_key = NULL;
static void *notified_value = NULL;
static void *notified_new_value = NULL;
static void *notified_user_data = NULL;
static int32_t notified_event = 0;
static int32_t notified_event_prev = 0;
static int32_t notified_events = 0;

static void
my_map_notification_iter(uint32_t event,
			 char* key, void* old_value,
			 void* value, void* user_data)
{
	const char *p;
	void *data;
	qb_map_t *m = (qb_map_t *)user_data;
	qb_map_iter_t *it = qb_map_iter_create(m);

	notified_events++;

	for (p = qb_map_iter_next(it, &data); p; p = qb_map_iter_next(it, &data)) {
		printf("%s > %s\n", p, (char*) data);
	}
	qb_map_iter_free(it);
}

/*
 * create some entries
 * add a notifier
 * delete an entry
 * in the notifier iterate over the map.
 */
static void
test_map_notifications_iter(qb_map_t *m)
{
	int i;

	qb_map_put(m, "k1", "one");
	qb_map_put(m, "k12", "two");
	qb_map_put(m, "k34", "three");
	ck_assert_int_eq(qb_map_count_get(m), 3);

	notified_events = 0;
	i = qb_map_notify_add(m, NULL, my_map_notification_iter,
			      (QB_MAP_NOTIFY_DELETED |
			       QB_MAP_NOTIFY_RECURSIVE), m);
	ck_assert_int_eq(i, 0);
	qb_map_rm(m, "k12");
	ck_assert_int_eq(notified_events, 1);
	ck_assert_int_eq(qb_map_count_get(m), 2);
}

static void
test_map_simple(qb_map_t *m, const char *name)
{
	int i;
	const char *p;
	void *data;
	qb_map_iter_t *it;

	qb_map_put(m, "k1", "one");
	qb_map_put(m, "k12", "two");
	qb_map_put(m, "k34", "three");
	ck_assert_int_eq(qb_map_count_get(m), 3);
	qb_map_put(m, "k3", "four");
	ck_assert_int_eq(qb_map_count_get(m), 4);

	it = qb_map_iter_create(m);
	i = 0;
	for (p = qb_map_iter_next(it, &data); p; p = qb_map_iter_next(it, &data)) {
		printf("%25s(%d) %s > %s\n", name, i, p, (char*) data);
		i++;
	}
	qb_map_iter_free(it);
	ck_assert_int_eq(i, 4);

	ck_assert_str_eq(qb_map_get(m, "k34"), "three");
	ck_assert_str_eq(qb_map_get(m, "k1"), "one");
	ck_assert_str_eq(qb_map_get(m, "k12"), "two");
	ck_assert_str_eq(qb_map_get(m, "k3"), "four");

	qb_map_rm(m, "k12");
	ck_assert_int_eq(qb_map_count_get(m), 3);
	qb_map_put(m, "9k", "nine");

	qb_map_put(m, "k34", "not_three");
	ck_assert_str_eq(qb_map_get(m, "k34"), "not_three");
	ck_assert_int_eq(qb_map_count_get(m), 4);

	qb_map_destroy(m);
}

static int32_t
my_traverse(const char *key, void *value, void *data)
{
	ck_assert((*key) > 0);
	return QB_FALSE;
}

static int32_t
check_order(const char *key, void *value, void *data)
{
	int *o = (int*)data;
	ck_assert_str_eq(chars[*o], key);
	ck_assert_str_eq(chars[*o], value);
	(*o)++;
	return QB_FALSE;
}

static int32_t
check_order2(const char *key, void *value, void *data)
{
	int *o = (int*)data;
	ck_assert_str_eq(chars2[*o], key);
	ck_assert_str_eq(chars2[*o], value);
	(*o)++;
	return QB_FALSE;
}

static void
test_map_search(qb_map_t* m)
{
	int32_t i;
	int32_t removed;
	int order;
	char c[2];
	const char *p;

	for (i = 0; chars[i]; i++) {
		qb_map_put(m, chars[i], chars[i]);
	}
	qb_map_foreach(m, my_traverse, NULL);

	ck_assert_int_eq(qb_map_count_get(m), (26*2 + 10));

	order = 0;
	qb_map_foreach(m, check_order, &order);

	for (i = 0; i < 26; i++) {
		removed = qb_map_rm(m, chars[i + 10]);
		ck_assert(removed);
	}

	c[0] = '\0';
	c[1] = '\0';
	removed = qb_map_rm(m, c);
	ck_assert(!removed);

	qb_map_foreach(m, my_traverse, NULL);

	ck_assert_int_eq(qb_map_count_get(m), 26+10);

	order = 0;
	qb_map_foreach(m, check_order2, &order);

	for (i = 25; i >= 0; i--) {
		qb_map_put(m, chars[i + 10], chars[i + 10]);
	}
	order = 0;
	qb_map_foreach(m, check_order, &order);

	c[0] = '0';
	p = qb_map_get(m, c);
	ck_assert(p && *p == *c);

	c[0] = 'A';
	p = qb_map_get(m, c);
	ck_assert(p && *p == *c);

	c[0] = 'a';
	p = qb_map_get(m, c);
	ck_assert(p && *p == *c);

	c[0] = 'z';
	p = qb_map_get(m, c);
	ck_assert(p && *p == *c);

	c[0] = '!';
	p = qb_map_get(m, c);
	ck_assert(p == NULL);

	c[0] = '=';
	p = qb_map_get(m, c);
	ck_assert(p == NULL);

	c[0] = '|';
	p = qb_map_get(m, c);
	ck_assert(p == NULL);

	qb_map_destroy(m);
}

static void
my_map_notification(uint32_t event,
		    char* key, void* old_value,
		    void* value, void* user_data)
{
	notified_key = key;
	notified_value = old_value;
	notified_new_value = value;
	notified_user_data = user_data;
	notified_event_prev = notified_event;
	notified_event = event;
}

static void
my_map_notification_2(uint32_t event,
		      char* key, void* old_value,
		      void* value, void* user_data)
{
}

static void
test_map_remove(qb_map_t *m)
{
	const char * a, *b, *c, *d;
	int32_t i;
	int32_t removed;
	const char *remove_ch[] = {"o","m","k","j","i","g","f","e","d","b","a",	NULL};

	i = qb_map_notify_add(m, NULL, my_map_notification,
			      (QB_MAP_NOTIFY_DELETED|
			       QB_MAP_NOTIFY_REPLACED|
			       QB_MAP_NOTIFY_RECURSIVE),
			      m);
	ck_assert_int_eq(i, 0);

	for (i = 0; chars[i]; i++) {
		qb_map_put(m, chars[i], chars[i]);
	}

	a = "0";
	qb_map_put(m, a, a);
	ck_assert(notified_key == chars[0]);
	ck_assert(notified_value == chars[0]);
	ck_assert(notified_user_data == m);
	notified_key = NULL;
	notified_value = NULL;
	notified_user_data = NULL;

	b = "5";
	removed = qb_map_rm(m, b);
	ck_assert(removed);
	ck_assert(notified_key == chars[5]);
	ck_assert(notified_value == chars[5]);
	ck_assert(notified_user_data == m);
	notified_key = NULL;
	notified_value = NULL;
	notified_user_data = NULL;

	d = "1";
	qb_map_put(m, d, d);
	ck_assert(notified_key == chars[1]);
	ck_assert(notified_value == chars[1]);
	ck_assert(notified_user_data == m);
	notified_key = NULL;
	notified_value = NULL;

	c = "2";
	removed = qb_map_rm(m, c);
	ck_assert(removed);
	ck_assert(notified_key == chars[2]);
	ck_assert(notified_value == chars[2]);
	notified_key = NULL;
	notified_value = NULL;

	for (i = 0; remove_ch[i]; i++) {
		removed = qb_map_rm(m, remove_ch[i]);
		ck_assert(removed);
	}

	qb_map_destroy(m);
}

static void
test_map_notifications_basic(qb_map_t *m)
{
	int32_t i;

/* with global notifier */
	i = qb_map_notify_add(m, NULL, my_map_notification,
			      (QB_MAP_NOTIFY_INSERTED|
			       QB_MAP_NOTIFY_DELETED|
			       QB_MAP_NOTIFY_REPLACED|
			       QB_MAP_NOTIFY_RECURSIVE),
			       m);
	ck_assert_int_eq(i, 0);

	notified_key = NULL;
	notified_value = NULL;
	notified_new_value = NULL;

/* insert */
	qb_map_put(m, "garden", "grow");
	ck_assert_str_eq(notified_key, "garden");
	ck_assert_str_eq(notified_new_value, "grow");
	ck_assert(notified_user_data == m);

/* update */
	qb_map_put(m, "garden", "green");
	ck_assert_str_eq(notified_key, "garden");
	ck_assert_str_eq(notified_value, "grow");
	ck_assert_str_eq(notified_new_value, "green");
	ck_assert(notified_user_data == m);

/* delete */
	qb_map_rm(m, "garden");
	ck_assert_str_eq(notified_key, "garden");
	ck_assert_str_eq(notified_value, "green");
	ck_assert(notified_user_data == m);

/* no event with notifier removed */
	i = qb_map_notify_del(m, NULL, my_map_notification,
			      (QB_MAP_NOTIFY_INSERTED|
			       QB_MAP_NOTIFY_DELETED|
			       QB_MAP_NOTIFY_REPLACED|
			       QB_MAP_NOTIFY_RECURSIVE));
	ck_assert_int_eq(i, 0);
	notified_key = NULL;
	notified_value = NULL;
	notified_new_value = NULL;
	qb_map_put(m, "age", "67");
	ck_assert(notified_key == NULL);
	ck_assert(notified_value == NULL);
	ck_assert(notified_new_value == NULL);

/* deleting a non-existing notification */
	i = qb_map_notify_del(m, "a", my_map_notification,
			      (QB_MAP_NOTIFY_INSERTED|
			       QB_MAP_NOTIFY_DELETED|
			       QB_MAP_NOTIFY_REPLACED|
			       QB_MAP_NOTIFY_RECURSIVE));
	ck_assert_int_eq(i, -ENOENT);

/* test uniquess */
	qb_map_put(m, "fred", "null");
	i = qb_map_notify_add(m, "fred", my_map_notification,
			      QB_MAP_NOTIFY_REPLACED, m);
	ck_assert_int_eq(i, 0);
	i = qb_map_notify_add(m, "fred", my_map_notification,
			      QB_MAP_NOTIFY_REPLACED, m);
	ck_assert_int_eq(i, -EEXIST);
}

/* test free'ing notifier
 *
 * input:
 *   only one can be added
 *   can only be added with NULL key (global)
 * output:
 *   is the last notifier called (after deleted or replaced)
 *   recursive is implicit
 */
static void
test_map_notifications_free(qb_map_t *m)
{
	int32_t i;
	i = qb_map_notify_add(m, "not global", my_map_notification,
			      QB_MAP_NOTIFY_FREE, m);
	ck_assert_int_eq(i, -EINVAL);
	i = qb_map_notify_add(m, NULL, my_map_notification,
			      QB_MAP_NOTIFY_FREE, m);
	ck_assert_int_eq(i, 0);
	i = qb_map_notify_add(m, NULL, my_map_notification_2,
			      QB_MAP_NOTIFY_FREE, m);
	ck_assert_int_eq(i, -EEXIST);
	i = qb_map_notify_del_2(m, NULL, my_map_notification,
			      QB_MAP_NOTIFY_FREE, m);
	ck_assert_int_eq(i, 0);
	i = qb_map_notify_add(m, NULL, my_map_notification,
			      (QB_MAP_NOTIFY_FREE |
			       QB_MAP_NOTIFY_REPLACED |
			       QB_MAP_NOTIFY_DELETED |
			       QB_MAP_NOTIFY_RECURSIVE), m);
	ck_assert_int_eq(i, 0);

	qb_map_put(m, "garden", "grow");

/* update */
	qb_map_put(m, "garden", "green");
	ck_assert_int_eq(notified_event_prev, QB_MAP_NOTIFY_REPLACED);
	ck_assert_int_eq(notified_event, QB_MAP_NOTIFY_FREE);

/* delete */
	qb_map_rm(m, "garden");
	ck_assert_int_eq(notified_event_prev, QB_MAP_NOTIFY_DELETED);
	ck_assert_int_eq(notified_event, QB_MAP_NOTIFY_FREE);
}

static void
test_map_notifications_prefix(qb_map_t *m)
{
	int32_t i;


/* with prefix notifier */
	i = qb_map_notify_add(m, "add", my_map_notification,
			      (QB_MAP_NOTIFY_INSERTED|
			       QB_MAP_NOTIFY_DELETED|
			       QB_MAP_NOTIFY_REPLACED|
			       QB_MAP_NOTIFY_RECURSIVE),
			       &i);
	ck_assert_int_eq(i, 0);

/* insert */
	qb_map_put(m, "adder", "snake");
	ck_assert_str_eq(notified_key, "adder");
	ck_assert_str_eq(notified_new_value, "snake");
	ck_assert(notified_user_data == &i);

/* insert (no match) */
	notified_key = NULL;
	notified_value = NULL;
	notified_new_value = NULL;
	qb_map_put(m, "adjust", "it");
	ck_assert(notified_key == NULL);
	ck_assert(notified_value == NULL);
	ck_assert(notified_new_value == NULL);

/* update */
	qb_map_put(m, "adder", "+++");
	ck_assert_str_eq(notified_key, "adder");
	ck_assert_str_eq(notified_value, "snake");
	ck_assert_str_eq(notified_new_value, "+++");

/* delete */
	qb_map_rm(m, "adder");
	ck_assert_str_eq(notified_key, "adder");
	ck_assert_str_eq(notified_value, "+++");

}

static void
test_map_traverse_ordered(qb_map_t *m)
{
	int32_t i;
	const char *p;
	char *result;
	void *data;
	qb_map_iter_t *it = qb_map_iter_create(m);

	for (i = 0; chars[i]; i++) {
		qb_map_put(m, chars[i], chars[i]);
	}
	result = calloc(sizeof(char), 26 * 2 + 10 + 1);

	i = 0;
	for (p = qb_map_iter_next(it, &data); p; p = qb_map_iter_next(it, &data)) {
		result[i] = *(char*) data;
		i++;
	}
	qb_map_iter_free(it);
	ck_assert_str_eq(result,
			 "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	qb_map_destroy(m);
}

static int32_t
traverse_and_remove_func(const char *key, void *value, void *data)
{
	int kk = random() % 30;
	qb_map_t *m = (qb_map_t *)data;
	qb_map_rm(m, chars[kk]);
	qb_map_put(m, chars[kk+30], key);
	return QB_FALSE;
}

static void
test_map_iter_safety(qb_map_t *m, int32_t ordered)
{
	void *data;
	void *data2;
	const char *p;
	const char *p2;
	qb_map_iter_t *it;
	qb_map_iter_t *it2;
	int32_t found_good = QB_FALSE;

	qb_map_put(m, "aaaa", "aye");
	qb_map_put(m, "bbbb", "bee");
	qb_map_put(m, "cccc", "sea");

	it = qb_map_iter_create(m);
	it2 = qb_map_iter_create(m);
	while ((p = qb_map_iter_next(it, &data)) != NULL) {
		printf("1: %s == %s\n", p, (char*)data);
		if (strcmp(p, "bbbb") == 0) {
			qb_map_rm(m, "bbbb");
			qb_map_rm(m, "cccc");
			qb_map_rm(m, "aaaa");
			qb_map_put(m, "fffff", "yum");
			while ((p2 = qb_map_iter_next(it2, &data2)) != NULL) {
				printf("2: %s == %s\n", p2, (char*)data2);
				if (strcmp(p2, "fffff") == 0) {
					qb_map_put(m, "ggggg", "good");
				}
			}
			qb_map_iter_free(it2);
		}
		if (strcmp(p, "ggggg") == 0) {
			found_good = QB_TRUE;
		}
	}
	qb_map_iter_free(it);

	if (ordered) {
		ck_assert_int_eq(found_good, QB_TRUE);
	}

	qb_map_destroy(m);
}

static void
test_map_iter_prefix(qb_map_t *m)
{
	void *data;
	const char *p;
	qb_map_iter_t *it;
	int count;

	qb_map_put(m, "aaaa", "aye");
	qb_map_put(m, "facc", "nope");
	qb_map_put(m, "abbb", "bee");
	qb_map_put(m, "a.ac", "nope");
	qb_map_put(m, "aacc", "yip");
	qb_map_put(m, "cacc", "nope");
	qb_map_put(m, "c", "----");

	count = 0;
	it = qb_map_pref_iter_create(m, "aa");
	while ((p = qb_map_iter_next(it, &data)) != NULL) {
		printf("1: %s == %s\n", p, (char*)data);
		count++;
	}
	qb_map_iter_free(it);
	ck_assert_int_eq(count, 2);

	count = 0;
	it = qb_map_pref_iter_create(m, "a");
	while ((p = qb_map_iter_next(it, &data)) != NULL) {
		printf("2: %s == %s\n", p, (char*)data);
		count++;
	}
	qb_map_iter_free(it);
	ck_assert_int_eq(count, 4);

	count = 0;
	it = qb_map_pref_iter_create(m, "zz");
	while ((p = qb_map_iter_next(it, &data)) != NULL) {
		printf("??: %s == %s\n", p, (char*)data);
		count++;
	}
	qb_map_iter_free(it);
	ck_assert_int_eq(count, 0);

	count = 0;
	it = qb_map_pref_iter_create(m, "c");
	while ((p = qb_map_iter_next(it, &data)) != NULL) {
		printf("3: %s == %s\n", p, (char*)data);
		count++;
	}
	qb_map_iter_free(it);
	ck_assert_int_eq(count, 2);

	qb_map_destroy(m);
}


static void
test_map_traverse_unordered(qb_map_t *m)
{
	int32_t i;
	srand(time(NULL));
	for (i = 0; i < 30; i++) {
		qb_map_put(m, chars[i], chars[i]);
	}
	qb_map_foreach(m, traverse_and_remove_func, m);
	qb_map_destroy(m);
}

static int32_t
my_counter_traverse(const char *key, void *value, void *data)
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
	float secs;
	void *value;
	qb_util_stopwatch_t *sw;

	ck_assert(m != NULL);
	sw = qb_util_stopwatch_create();

#define MAX_WORDS 100000

	/*
	 * Load with dictionary
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	count = 0;
	while (fgets(word, sizeof(word), fp) && count < MAX_WORDS) {
		w = strdup(word);
		qb_map_put(m, w, w);
		count++;
	}
	qb_util_stopwatch_stop(sw);
	ck_assert_int_eq(qb_map_count_get(m), count);
	fclose(fp);
	secs = qb_util_stopwatch_sec_elapsed_get(sw);
	ops = (float)count / secs;
	qb_log(LOG_INFO, "%25s %12.2f puts/sec (%d/%fs)\n", test_name, ops, count, secs);

	/*
	 * Verify dictionary produces correct values
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	count2 = 0;
	while (fgets(word, sizeof(word), fp) && count2 < MAX_WORDS) {
		value = qb_map_get(m, word);
		ck_assert_str_eq(word, value);
		count2++;
	}
	qb_util_stopwatch_stop(sw);
	fclose(fp);

	secs = qb_util_stopwatch_sec_elapsed_get(sw);
	ops = (float)count2 / secs;
	qb_log(LOG_INFO, "%25s %12.2f gets/sec (%d/%fs)\n", test_name, ops, count2, secs);

	/*
	 * time the iteration
	 */
	count2 = 0;
	qb_util_stopwatch_start(sw);
	qb_map_foreach(m, my_counter_traverse, &count2);
	qb_util_stopwatch_stop(sw);
	ck_assert_int_eq(qb_map_count_get(m), count2);
	secs = qb_util_stopwatch_sec_elapsed_get(sw);
	ops = (float)count2 / secs;
	qb_log(LOG_INFO, "%25s %12.2f iters/sec (%d/%fs)\n", test_name, ops, count2, secs);

	/*
	 * Delete all dictionary entries
	 */
	fp = fopen("/usr/share/dict/words", "r");
	qb_util_stopwatch_start(sw);
	count2 = 0;
	while (fgets(word, sizeof(word), fp) && count2 < MAX_WORDS) {
		res = qb_map_rm(m, word);
		ck_assert_int_eq(res, QB_TRUE);
		count2++;
	}
	qb_util_stopwatch_stop(sw);
	ck_assert_int_eq(qb_map_count_get(m), 0);
	fclose(fp);

	secs = qb_util_stopwatch_sec_elapsed_get(sw);
	ops = (float)count2 / secs;
	qb_log(LOG_INFO, "%25s %12.2f dels/sec (%d/%fs)\n", test_name, ops, count2, secs);
}

static void test_accents_load(qb_map_t *m, const char* test_name)
{
	int i;
	int32_t res = 0;
	int32_t count = 0;
	int32_t count2;
	void *value;

	ck_assert(m != NULL);
	/*
	 * Load accented names
	 */
	for (i=0; i<sizeof(composers)/sizeof(char*); i++) {
		qb_map_put(m, strdup(composers[i]), strdup(composers[i]));
		count++;
	}
	ck_assert_int_eq(qb_map_count_get(m), count);

	/*
	 * Verify dictionary produces correct values
	 */
	count2 = 0;
	for (i=0; i<sizeof(composers)/sizeof(char*); i++) {
		value = qb_map_get(m, composers[i]);
		ck_assert_str_eq(value, composers[i]);
		count2++;
	}
	ck_assert_int_eq(qb_map_count_get(m), count2);

	/*
	 * Delete all dictionary entries
	 */
	for (i=0; i<sizeof(composers)/sizeof(char*); i++) {
		res = qb_map_rm(m, composers[i]);
		ck_assert_int_eq(res, QB_TRUE);
	}
	ck_assert_int_eq(qb_map_count_get(m), 0);
}

START_TEST(test_skiplist_simple)
{
	qb_map_t *m = qb_skiplist_create();
	test_map_simple(m, __func__);
}
END_TEST

START_TEST(test_hashtable_simple)
{
	qb_map_t *m = qb_hashtable_create(32);
	test_map_simple(m, __func__);
}
END_TEST

START_TEST(test_trie_simple)
{
	qb_map_t *m = qb_trie_create();
	test_map_simple(m, __func__);
}
END_TEST

START_TEST(test_skiplist_search)
{
	qb_map_t *m = qb_skiplist_create();
	test_map_search(m);
}
END_TEST

START_TEST(test_trie_search)
{
	qb_map_t *m = qb_trie_create();
	test_map_search(m);
}
END_TEST

START_TEST(test_skiplist_remove)
{
	qb_map_t *m = qb_skiplist_create();
	test_map_remove(m);
}
END_TEST

START_TEST(test_hashtable_remove)
{
	qb_map_t *m = qb_hashtable_create(256);
	test_map_remove(m);
}
END_TEST

START_TEST(test_trie_notifications)
{
	qb_map_t *m;
	m = qb_trie_create();
	test_map_remove(m);
	m = qb_trie_create();
	test_map_notifications_basic(m);
	m = qb_trie_create();
	test_map_notifications_prefix(m);
	m = qb_trie_create();
	test_map_notifications_free(m);
	m = qb_trie_create();
	test_map_notifications_iter(m);
}
END_TEST

START_TEST(test_hash_notifications)
{
	qb_map_t *m;
	m = qb_hashtable_create(256);
	test_map_notifications_basic(m);
	m = qb_hashtable_create(256);
	test_map_notifications_free(m);
}
END_TEST

START_TEST(test_skiplist_notifications)
{
	qb_map_t *m;
	m = qb_skiplist_create();
	test_map_notifications_basic(m);
	m = qb_skiplist_create();
	test_map_notifications_free(m);
}
END_TEST

START_TEST(test_skiplist_traverse)
{
	qb_map_t *m;
	m = qb_skiplist_create();
	test_map_traverse_ordered(m);

	m = qb_skiplist_create();
	test_map_traverse_unordered(m);
	m = qb_skiplist_create();
	test_map_iter_safety(m, QB_TRUE);
}
END_TEST

START_TEST(test_hashtable_traverse)
{
	qb_map_t *m;
	m = qb_hashtable_create(256);
	test_map_traverse_unordered(m);
	m = qb_hashtable_create(256);
	test_map_iter_safety(m, QB_FALSE);
}
END_TEST

START_TEST(test_trie_traverse)
{
	qb_map_t *m;
	m = qb_trie_create();
	test_map_traverse_unordered(m);
	m = qb_trie_create();
	test_map_iter_safety(m, QB_FALSE);
	m = qb_trie_create();
	test_map_iter_prefix(m);
}
END_TEST

START_TEST(test_skiplist_load)
{
	qb_map_t *m;
	if (access("/usr/share/dict/words", R_OK) != 0) {
		printf("no dict/words - not testing\n");
		return;
	}
	m = qb_skiplist_create();
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
	m = qb_hashtable_create(100000);
	test_map_load(m, __func__);
}
END_TEST

START_TEST(test_trie_load)
{
	qb_map_t *m;
	if (access("/usr/share/dict/words", R_OK) != 0) {
		printf("no dict/words - not testing\n");
		return;
	}
	m = qb_trie_create();
	test_map_load(m, __func__);
}
END_TEST

START_TEST(test_skiplist_accents)
{
	qb_map_t *m;
	m = qb_skiplist_create();
	test_accents_load(m, __func__);
}
END_TEST

START_TEST(test_hashtable_accents)
{
	qb_map_t *m;
	m = qb_hashtable_create(16);
	test_accents_load(m, __func__);
}
END_TEST

START_TEST(test_trie_accents)
{
	qb_map_t *m;
	m = qb_trie_create();
	test_accents_load(m, __func__);
}
END_TEST


/*
 * From Honza: https://github.com/asalkeld/libqb/issues/44
 */
START_TEST(test_trie_partial_iterate)
{
        qb_map_t *map;
        qb_map_iter_t *iter;
        const char *res;
        char *item;
	int rc;

        ck_assert((map = qb_trie_create()) != NULL);
        qb_map_put(map, strdup("testobj.testkey"), strdup("one"));
        qb_map_put(map, strdup("testobj.testkey2"), strdup("two"));

        iter = qb_map_pref_iter_create(map, "testobj.");
        ck_assert(iter != NULL);
        res = qb_map_iter_next(iter, (void **)&item);
        fprintf(stderr, "%s = %s\n", res, item);
        qb_map_iter_free(iter);

        item = qb_map_get(map, "testobj.testkey");
        ck_assert_str_eq(item, "one");

        rc = qb_map_rm(map, "testobj.testkey");
        ck_assert(rc == QB_TRUE);

        item = qb_map_get(map, "testobj.testkey");
        ck_assert(item == NULL);

}
END_TEST


static Suite *
map_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("qb_map");

	add_tcase(s, tc, test_skiplist_simple);
	add_tcase(s, tc, test_hashtable_simple);
	add_tcase(s, tc, test_trie_simple);
	add_tcase(s, tc, test_trie_partial_iterate);
	add_tcase(s, tc, test_skiplist_remove);
	add_tcase(s, tc, test_hashtable_remove);
	add_tcase(s, tc, test_trie_notifications);
	add_tcase(s, tc, test_hash_notifications);
	add_tcase(s, tc, test_skiplist_notifications);
	add_tcase(s, tc, test_skiplist_search);

/*
 * 	No hashtable_search as it assumes an ordered
 *	collection
 */
	add_tcase(s, tc, test_trie_search);
	add_tcase(s, tc, test_skiplist_traverse);
	add_tcase(s, tc, test_hashtable_traverse);
	add_tcase(s, tc, test_trie_traverse);
	add_tcase(s, tc, test_skiplist_load, 30);
	add_tcase(s, tc, test_hashtable_load, 30);
	add_tcase(s, tc, test_trie_load, 30);

	add_tcase(s, tc, test_skiplist_accents);
	add_tcase(s, tc, test_hashtable_accents);
	add_tcase(s, tc, test_trie_accents);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;

	Suite *s = map_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	atexit(qb_log_fini);
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
