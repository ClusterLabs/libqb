/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <check.h>
#include <qb/qbhash.h>

qb_hdb_handle_t handle;
unsigned int distribution[65536];


START_TEST (test_hash_init)
{
	int res;
	handle = 0;

	res = qb_hash_initialize (&handle, 3, 17);

	ck_assert_int_ne (handle, 0);
	ck_assert_int_eq (res, 0);

	memset (distribution, 0, sizeof (distribution));
}
END_TEST

START_TEST (test_hash_load)
{
	char word[1000];
	FILE *fp;
	int hashes_done = 0;

	/*
	 * Load hash table with dictionary
	 */
	fp = fopen ("/usr/share/dict/words", "r");
	while (fgets (word, sizeof (word), fp)) {
		word[strlen (word) - 1] = '\0';
		qb_hash_key_set (handle, word, word, strlen (word) + 1);
		hashes_done += 1;
	}
	fclose (fp);

	fail_unless (1 == 1);
}
END_TEST

START_TEST (test_hash_verify)
{
	char word[1000];
	FILE *fp;
	void *value;
	uint64_t value_len;

	/*
	 * Verify dictionary produces correct values
	 */
	fp = fopen ("/usr/share/dict/words", "r");
	while (fgets (word, sizeof (word), fp)) {
		word[strlen (word) - 1] = '\0';
		qb_hash_key_get (handle, word, &value, &value_len);
		ck_assert_str_eq (word, value);
	}
	fclose (fp);
}
END_TEST

START_TEST (test_hash_delete)
{
	char word[1000];
	FILE *fp;
	int res;

	/*
	 * Delete all dictionary entries
	 */
	fp = fopen ("/usr/share/dict/words", "r");
	while (fgets (word, sizeof (word), fp)) {
		word[strlen (word) - 1] = '\0';
		res = qb_hash_key_delete (handle, word);
		ck_assert_int_eq (res, 0);
	}
	fclose (fp);

}
END_TEST

static Suite *hash_suite (void)
{
	TCase *tc_load;
	TCase *tc_verify;
	TCase *tc_delete;
	Suite *s = suite_create ("hashtable");

	tc_load = tcase_create ("load");
	tcase_add_test (tc_load, test_hash_init);
	tcase_add_test (tc_load, test_hash_load);
	suite_add_tcase (s, tc_load);

	tc_verify = tcase_create ("verify");
	tcase_add_test (tc_verify, test_hash_verify);
	suite_add_tcase (s, tc_verify);

	tc_delete = tcase_create ("delete");
	tcase_add_test (tc_delete, test_hash_delete);
	suite_add_tcase (s, tc_delete);

	return s;
}

int main (void)
{
	int number_failed;

	Suite *s = hash_suite ();
	SRunner *sr = srunner_create (s);
	srunner_run_all (sr, CK_NORMAL);
	number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

