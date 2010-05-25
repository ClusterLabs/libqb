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

#ifndef QB_PLUGIN_COMP_H_DEFINED
#define QB_PLUGIN_COMP_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

/*
 * plugin Interface
 */
struct plugin_iface {
	const char *name;			/* Name of the interface */
	int version;			/* Version of this interface */
	int *versions_replace;		/* Versions that this interface can replace */
	int versions_replace_count;	/* Count of entries in version_replace */
	char **dependencies;		/* Dependent interfaces */
	size_t dependency_count;	/* Count of entires in dependencies */
	int (*constructor) (void *context);	/* Constructor for this interface */
	void (*destructor) (void *context);	/* Constructor for this interface */
	void **interfaces;		/* List of functions in interface */
};

/*
 * plugin Component
 */
struct plugin_comp {
	struct plugin_iface *ifaces;	/* List of interfaces in this component */
	size_t iface_count;		/* size of ifaces list */
};

extern void plugin_component_register (struct plugin_comp *comp);

static inline void plugin_interfaces_set (struct plugin_iface *iface, void *iface_list)
{
	iface->interfaces = (void **)iface_list;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* QB_PLUGIN_COMP_H_DEFINED */

