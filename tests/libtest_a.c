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

#include <stdio.h>
#include <qb/qbplugin_comp.h>

/*
 * Version 0 of the interface
 */
static int32_t iface1_constructor(void *context);

static void iface1_destructor(void *context);

static void iface1_func1(void);

static void iface1_func2(void);

static void iface1_func3(void);

/*
 * Version 1 of the interface
 */
static int32_t iface1_ver1_constructor(void *context);

static void iface1_ver1_destructor(void *context);

static void iface1_ver1_func1(void);

static void iface1_ver1_func2(void);

static void iface1_ver1_func3(void);

struct iface_list {
	void (*iface1_func1) (void);
	void (*iface1_func2) (void);
	void (*iface1_func3) (void);
};

struct iface_ver1_list {
	void (*iface1_ver1_func1) (void);
	void (*iface1_ver1_func2) (void);
	void (*iface1_ver1_func3) (void);
};

static struct iface_list iface_list = {
	.iface1_func1 = iface1_func1,
	.iface1_func2 = iface1_func2,
	.iface1_func3 = iface1_func3,
};

static struct iface_list iface_ver1_list = {
	.iface1_func1 = iface1_ver1_func1,
	.iface1_func2 = iface1_ver1_func2,
	.iface1_func3 = iface1_ver1_func3,
};

static struct plugin_iface iface1[2] = {
	/* version 0 */
	{
	 .name = "A_iface1",
	 .version = 0,
	 .versions_replace = 0,
	 .versions_replace_count = 0,
	 .dependencies = 0,
	 .dependency_count = 0,
	 .constructor = iface1_constructor,
	 .destructor = iface1_destructor,
	 .interfaces = NULL},
	/* version 1 */
	{
	 .name = "A_iface1",
	 .version = 1,
	 .versions_replace = 0,
	 .versions_replace_count = 0,
	 .dependencies = 0,
	 .dependency_count = 0,
	 .constructor = iface1_ver1_constructor,
	 .destructor = iface1_ver1_destructor,
	 .interfaces = NULL}
};

static struct plugin_comp test_comp = {
	.iface_count = 2,
	.ifaces = iface1
};

static int32_t iface1_constructor(void *context)
{
	printf("A - version 0 constructor context %p\n", context);
	return (0);
}

static void iface1_destructor(void *context)
{
	printf("A - version 0 destructor context %p\n", context);
}

static void iface1_func1(void)
{
	printf("A - version 0 func1\n");
}

static void iface1_func2(void)
{
	printf("A - version 0 func2\n");
}

static void iface1_func3(void)
{
	printf("A - version 0 func3\n");
}

static int32_t iface1_ver1_constructor(void *context)
{
	printf("A - version 1 constructor context %p\n", context);
	return (0);
}

static void iface1_ver1_destructor(void *context)
{
	printf("A - version 1 destructor context %p\n", context);
}

static void iface1_ver1_func1(void)
{
	printf("A - version 1 func1\n");
}

static void iface1_ver1_func2(void)
{
	printf("A - version 1 func2\n");
}

static void iface1_ver1_func3(void)
{
	printf("A - version 1 func3\n");
}

__attribute__ ((constructor))
static void register_this_component(void)
{
	plugin_interfaces_set(&iface1[0], &iface_list);
	plugin_interfaces_set(&iface1[1], &iface_ver1_list);
	plugin_component_register(&test_comp);
}
