/*
 * Copyright (C) 2006 Steven Dake (sdake@redhat.com)
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

#include <config.h>

#include <check.h>
#include <assert.h>
#include <unistd.h>
#include <qb/qbhdb.h>
#include <qb/qbplugin.h>

struct iface {
	void (*func1) (void);
	void (*func2) (void);
	void (*func3) (void);
};

START_TEST (test_plugin)
{
	qb_hdb_handle_t a_ifact_handle_ver0;
	qb_hdb_handle_t b_ifact_handle_ver0;
	struct iface *a_iface_ver0;
	struct iface *a_iface_ver1;
	void *a_iface_ver0_p;
	void *a_iface_ver1_p;

	qb_hdb_handle_t a_ifact_handle_ver1;
	qb_hdb_handle_t b_ifact_handle_ver1;
	struct iface *b_iface_ver0;
	struct iface *b_iface_ver1;
	void *b_iface_ver0_p;
	void *b_iface_ver1_p;

	unsigned int res;

	setenv ("LD_LIBRARY_PATH", ".libs", 1);
	/*
	 * Reference version 0 and 1 of A and B interfaces
	 */
	res = plugin_ifact_reference (
		&a_ifact_handle_ver0,
		"A_iface1",
		0, /* version 0 */
		&a_iface_ver0_p,
		(void *)0xaaaa0000);
	ck_assert_int_eq (res, 0);

	a_iface_ver0 = (struct iface *)a_iface_ver0_p;

	res = plugin_ifact_reference (
		&b_ifact_handle_ver0,
		"B_iface1",
		0, /* version 0 */
		&b_iface_ver0_p,
		(void *)0xbbbb0000);
	ck_assert_int_eq (res, 0);

	b_iface_ver0 = (struct iface *)b_iface_ver0_p;

	res = plugin_ifact_reference (
		&a_ifact_handle_ver1,
		"A_iface1",
		1, /* version 1 */
		&a_iface_ver1_p,
		(void *)0xaaaa1111);
	ck_assert_int_eq (res, 0);

	a_iface_ver1 = (struct iface *)a_iface_ver1_p;

	res = plugin_ifact_reference (
		&b_ifact_handle_ver1,
		"B_iface1",
		1, /* version 1 */
		&b_iface_ver1_p,
		(void *)0xbbbb1111);
	ck_assert_int_eq (res, 0);

	b_iface_ver1 = (struct iface *)b_iface_ver1_p;

	a_iface_ver0->func1();
	a_iface_ver0->func2();
	a_iface_ver0->func3();

	plugin_ifact_release (a_ifact_handle_ver0);

	a_iface_ver1->func1();
	a_iface_ver1->func2();
	a_iface_ver1->func3();

	plugin_ifact_release (a_ifact_handle_ver1);

	b_iface_ver0->func1();
	b_iface_ver0->func2();
	b_iface_ver0->func3();

	plugin_ifact_release (b_ifact_handle_ver0);

	b_iface_ver1->func1();
	b_iface_ver1->func2();
	b_iface_ver1->func3();

	plugin_ifact_release (b_ifact_handle_ver1);
}
END_TEST

static Suite *hash_suite (void)
{
	TCase *tc_plugin;
	Suite *s = suite_create ("plugin");
	tc_plugin = tcase_create ("load");
	tcase_add_test (tc_plugin, test_plugin);
	suite_add_tcase (s, tc_plugin);

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

