/*
 * Copyright (C) 2006 Steven Dake <sdake@redhat.com>
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

START_TEST(test_plugin)
{
	qb_handle_t a_ifact_handle_ver0;
	qb_handle_t b_ifact_handle_ver0;
	struct iface *a_iface_ver0;
	struct iface *a_iface_ver1;
	void *a_iface_ver0_p;
	void *a_iface_ver1_p;

	qb_handle_t a_ifact_handle_ver1;
	qb_handle_t b_ifact_handle_ver1;
	struct iface *b_iface_ver0;
	struct iface *b_iface_ver1;
	void *b_iface_ver0_p;
	void *b_iface_ver1_p;

	int32_t res;

	setenv("LD_LIBRARY_PATH", ".libs", 1);
	/*
	 * Reference version 0 and 1 of A and B interfaces
	 */
	res = plugin_ifact_reference(&a_ifact_handle_ver0, "A_iface1", 0,	/* version 0 */
				     &a_iface_ver0_p, (void *)0xaaaa0000);
	ck_assert_int_eq(res, 0);

	a_iface_ver0 = (struct iface *)a_iface_ver0_p;

	res = plugin_ifact_reference(&b_ifact_handle_ver0, "B_iface1", 0,	/* version 0 */
				     &b_iface_ver0_p, (void *)0xbbbb0000);
	ck_assert_int_eq(res, 0);

	b_iface_ver0 = (struct iface *)b_iface_ver0_p;

	res = plugin_ifact_reference(&a_ifact_handle_ver1, "A_iface1", 1,	/* version 1 */
				     &a_iface_ver1_p, (void *)0xaaaa1111);
	ck_assert_int_eq(res, 0);

	a_iface_ver1 = (struct iface *)a_iface_ver1_p;

	res = plugin_ifact_reference(&b_ifact_handle_ver1, "B_iface1", 1,	/* version 1 */
				     &b_iface_ver1_p, (void *)0xbbbb1111);
	ck_assert_int_eq(res, 0);

	b_iface_ver1 = (struct iface *)b_iface_ver1_p;

	a_iface_ver0->func1();
	a_iface_ver0->func2();
	a_iface_ver0->func3();

	plugin_ifact_release(a_ifact_handle_ver0);

	a_iface_ver1->func1();
	a_iface_ver1->func2();
	a_iface_ver1->func3();

	plugin_ifact_release(a_ifact_handle_ver1);

	b_iface_ver0->func1();
	b_iface_ver0->func2();
	b_iface_ver0->func3();

	plugin_ifact_release(b_ifact_handle_ver0);

	b_iface_ver1->func1();
	b_iface_ver1->func2();
	b_iface_ver1->func3();

	plugin_ifact_release(b_ifact_handle_ver1);
}

END_TEST static Suite *plugin_suite(void)
{
	TCase *tc_plugin;
	Suite *s = suite_create("plugin");
	tc_plugin = tcase_create("load");
	tcase_add_test(tc_plugin, test_plugin);
	suite_add_tcase(s, tc_plugin);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = plugin_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
