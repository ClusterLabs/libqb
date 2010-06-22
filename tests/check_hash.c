/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#include <stdio.h>
#include <unistd.h>
#include <check.h>
#include <qb/qbhash.h>

START_TEST(test_hash_load)
{
	char word[1000];
	FILE *fp;
	int32_t res = 0;
	qb_handle_t handle = 0;
	void *value;
	uint64_t value_len;

	if (access("/usr/share/dict/words", R_OK) != 0) {
		printf("no dict/words - not testing\n");
		return;
	}

	res = qb_hash_initialize(&handle, 17, 0);

	ck_assert_int_ne(handle, 0);
	ck_assert_int_eq(res, 0);
	/*
	 * Load hash table with dictionary
	 */
	fp = fopen("/usr/share/dict/words", "r");
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		res = qb_hash_key_set(handle, word, word, strlen(word) + 1);
		//if (res < 0) {
		//      printf ("FAILED to insert %s : %s\n", word, strerror(errno));
		//}
		ck_assert_int_eq(res, 0);
	}
	fclose(fp);

	/*
	 * Verify dictionary produces correct values
	 */
	fp = fopen("/usr/share/dict/words", "r");
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		qb_hash_key_get(handle, word, &value, &value_len);
		ck_assert_str_eq(word, value);
	}
	fclose(fp);

	/*
	 * Delete all dictionary entries
	 */
	fp = fopen("/usr/share/dict/words", "r");
	while (fgets(word, sizeof(word), fp)) {
		word[strlen(word) - 1] = '\0';
		res = qb_hash_key_delete(handle, word);
		ck_assert_int_eq(res, 0);
	}
	fclose(fp);
}

END_TEST static Suite *hash_suite(void)
{
	TCase *tc_load;
	Suite *s = suite_create("hashtable");

	tc_load = tcase_create("load_and_verify");
	tcase_add_test(tc_load, test_hash_load);
	tcase_set_timeout(tc_load, 10);
	suite_add_tcase(s, tc_load);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = hash_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
