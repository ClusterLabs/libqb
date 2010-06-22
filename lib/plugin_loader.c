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
#include <stdlib.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#ifdef QB_SOLARIS
#include <iso/limits_iso.h>
#endif
#include <qb/qbhdb.h>
#include <qb/qbplugin_comp.h>
#include <qb/qbplugin.h>

struct plugin_component_instance {
	struct plugin_iface *ifaces;
	int32_t iface_count;
	qb_handle_t comp_handle;
	void *dl_handle;
	int32_t refcount;
	char library_name[256];
};

struct plugin_iface_instance {
	qb_handle_t component_handle;
	void *context;
	void (*destructor) (void *context);
};

QB_HDB_DECLARE(plugin_component_instance_database, NULL);

QB_HDB_DECLARE(plugin_iface_instance_database, NULL);

/*
static struct hdb_handle_database plugin_component_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0
};

static struct hdb_handle_database plugin_iface_instance_database = {
	.handle_count	= 0,
	.handles	= 0,
	.iterator	= 0
};
*/

static qb_handle_t g_component_handle = 0xFFFFFFFF;

#if defined(QB_LINUX) || defined(QB_SOLARIS)
static int32_t plugin_select_so(const struct dirent *dirent)
#else
static int32_t plugin_select_so(struct dirent *dirent)
#endif
{
	size_t len;
	/*
	 * TODO check .lcrso > .so
	 */
	len = strlen(dirent->d_name);
	if (len > 3) {
		if (strcmp(".so", dirent->d_name + len - 3) == 0) {
			return (1);
		}
	}
	return (0);
}

#if defined(QB_LINUX) || defined(QB_SOLARIS)
static int32_t pathlist_select(const struct dirent *dirent)
#else
static int32_t pathlist_select(struct dirent *dirent)
#endif
{
	if (fnmatch("*.conf", dirent->d_name, 0) == 0) {
		return (1);
	}

	return (0);
}

static inline struct plugin_component_instance *plugin_comp_find(const char
								 *iface_name,
								 uint32_t
								 version,
								 uint32_t
								 *iface_number)
{
	struct plugin_component_instance *instance;
	void *instance_p = NULL;
	qb_handle_t component_handle = 0;
	int32_t i;

	/*
	 * Try to find interface in already loaded component
	 */
	qb_hdb_iterator_reset(&plugin_component_instance_database);
	while (qb_hdb_iterator_next(&plugin_component_instance_database,
				    &instance_p, &component_handle) == 0) {

		instance = (struct plugin_component_instance *)instance_p;

		for (i = 0; i < instance->iface_count; i++) {
			if ((strcmp(instance->ifaces[i].name, iface_name) == 0)
			    && instance->ifaces[i].version == version) {

				*iface_number = i;
				return (instance);
			}
		}
		qb_hdb_handle_put(&plugin_component_instance_database,
				  component_handle);
	}

	return (NULL);
}

static inline int32_t plugin_lib_loaded(char *library_name)
{
	struct plugin_component_instance *instance;
	void *instance_p = NULL;
	qb_handle_t component_handle = 0;

	/*
	 * Try to find interface in already loaded component
	 */
	qb_hdb_iterator_reset(&plugin_component_instance_database);
	while (qb_hdb_iterator_next(&plugin_component_instance_database,
				    (void *)&instance_p,
				    &component_handle) == 0) {

		instance = (struct plugin_component_instance *)instance_p;

		if (strcmp(instance->library_name, library_name) == 0) {
			return (1);
		}

		qb_hdb_handle_put(&plugin_component_instance_database,
				  component_handle);
	}

	return (0);
}

enum { PATH_LIST_SIZE = 128 };
const char *path_list[PATH_LIST_SIZE];
uint32_t path_list_entries = 0;

static void defaults_path_build(void)
{
	char cwd[1024];
	char *res;

	res = getcwd(cwd, sizeof(cwd));
	if (res != NULL && (path_list[0] = strdup(cwd)) != NULL) {
		path_list_entries++;
	}

	path_list[path_list_entries++] = PLUGINSODIR;
}

static void ld_library_path_build(void)
{
	char *ld_library_path;
	char *my_ld_library_path;
	char *p_s, *ptrptr;

	ld_library_path = getenv("LD_LIBRARY_PATH");
	if (ld_library_path == NULL) {
		return;
	}
	my_ld_library_path = strdup(ld_library_path);
	if (my_ld_library_path == NULL) {
		return;
	}

	p_s = strtok_r(my_ld_library_path, ":", &ptrptr);
	while (p_s != NULL) {
		char *p = strdup(p_s);
		if (p && path_list_entries < PATH_LIST_SIZE) {
			path_list[path_list_entries++] = p;
		}
		p_s = strtok_r(NULL, ":", &ptrptr);
	}

	free(my_ld_library_path);
}

static int32_t ldso_path_build(const char *path, const char *filename)
{
	FILE *fp;
	char string[1024];
	char filename_cat[1024];
	char newpath[1024];
	char *newpath_tmp;
	char *new_filename;
	int32_t j;
	struct dirent **scandir_list;
	uint32_t scandir_entries;

	snprintf(filename_cat, sizeof(filename_cat), "%s/%s", path, filename);
	if (filename[0] == '*') {
		scandir_entries = scandir(path,
					  &scandir_list,
					  pathlist_select, alphasort);
		if (scandir_entries == 0) {
			return 0;
		} else if (scandir_entries == -1) {
			return -1;
		} else {
			for (j = 0; j < scandir_entries; j++) {
				ldso_path_build(path, scandir_list[j]->d_name);
			}
		}
	}

	fp = fopen(filename_cat, "r");
	if (fp == NULL) {
		return (-1);
	}

	while (fgets(string, sizeof(string), fp)) {
		char *p;
		if (strlen(string) > 0)
			string[strlen(string) - 1] = '\0';
		if (strncmp(string, "include", strlen("include")) == 0) {
			newpath_tmp = string + strlen("include") + 1;
			for (j = strlen(string);
			     string[j] != ' ' &&
			     string[j] != '/' && j > 0; j--) {
			}
			string[j] = '\0';
			new_filename = &string[j] + 1;
			strcpy(newpath, path);
			strcat(newpath, "/");
			strcat(newpath, newpath_tmp);
			ldso_path_build(newpath, new_filename);
			continue;
		}
		p = strdup(string);
		if (p && path_list_entries < PATH_LIST_SIZE) {
			path_list[path_list_entries++] = p;
		}
	}
	fclose(fp);
	return (0);
}

#if defined (QB_SOLARIS) && !defined(HAVE_SCANDIR)
static int32_t scandir(const char *dir, struct dirent ***namelist,
		   int32_t (*filter) (const struct dirent *),
		   int32_t (*compar) (const struct dirent **,
				  const struct dirent **))
{
	DIR *d;
	struct dirent *entry;
	struct dirent *result;
	struct dirent **names = NULL;
	int32_t namelist_items = 0, namelist_size = 0;
	size_t len;
	int32_t return_code;

	d = opendir(dir);
	if (d == NULL)
		return -1;

	names = NULL;

	len = offsetof(struct dirent, d_name)+pathconf(dir, _PC_NAME_MAX) + 1;
	entry = malloc(len);

	for (return_code = readdir_r(d, entry, &result);
	     dirent != NULL && return_code == 0;
	     return_code = readdir_r(d, entry, &result)) {

		struct dirent *tmpentry;
		if ((filter != NULL) && ((*filter) (result) == 0)) {
			continue;
		}
		if (namelist_items >= namelist_size) {
			struct dirent **tmp;
			namelist_size += 512;
			if ((uint32_t)namelist_size > INT_MAX) {
				errno = EOVERFLOW;
				goto fail;
			}
			tmp = realloc(names,
				      namelist_size * sizeof(struct dirent *));
			if (tmp == NULL) {
				goto fail;
			}
			names = tmp;
		}
		tmpentry = malloc(result->d_reclen);
		if (tmpentry == NULL) {
			goto fail;
		}
		(void)memcpy(tmpentry, result, result->d_reclen);
		names[namelist_items++] = tmpentry;
	}
	(void)closedir(d);
	if ((namelist_items > 1) && (compar != NULL)) {
		qsort(names, namelist_items, sizeof(struct dirent *),
		      (int32_t (*)(const void *, const void *))compar);
	}

	*namelist = names;

	return namelist_items;

fail:
	{
		int32_t err = errno;
		(void)closedir(d);
		while (namelist_items != 0) {
			namelist_items--;
			free(*namelist[namelist_items]);
		}
		free(entry);
		free(names);
		*namelist = NULL;
		errno = err;
		return -1;
	}
}
#endif

#if defined (QB_SOLARIS) && !defined(HAVE_ALPHASORT)
static int32_t alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}
#endif

static int32_t interface_find_and_load(const char *path,
				   const char *iface_name,
				   int32_t version, struct plugin_component_instance
				   **instance_ret, uint32_t *iface_number)
{
	struct plugin_component_instance *instance;
	void *dl_handle;
	struct dirent **scandir_list;
	int32_t scandir_entries;
	uint32_t libs_to_scan;
	char dl_name[1024];
#ifdef QB_SOLARIS
	void (*comp_reg) (void);
#endif

	scandir_entries =
	    scandir(path, &scandir_list, plugin_select_so, alphasort);
	if (scandir_entries > 0)
		/*
		 * no error so load the object
		 */
		for (libs_to_scan = 0; libs_to_scan < scandir_entries;
		     libs_to_scan++) {
			/*
			 * Load objects, scan them, unload them if they are not a match
			 */
			snprintf(dl_name, sizeof(dl_name), "%s/%s",
				 path, scandir_list[libs_to_scan]->d_name);
			/*
			 * Don't reload already loaded libraries
			 */
			if (plugin_lib_loaded(dl_name)) {
				continue;
			}
			dl_handle = dlopen(dl_name, RTLD_NOW);
			if (dl_handle == NULL) {
				fprintf(stderr, "%s: open failed: %s\n",
					dl_name, dlerror());
				continue;
			}
/*
 * constructors don't work in Solaris dlopen, so we have to specifically call
 * a function to register the component
 */
#ifdef QB_SOLARIS
			comp_reg =
			    dlsym(dl_handle,
				  "corosync_plugin_component_register");
			comp_reg();
#endif
			instance =
			    plugin_comp_find(iface_name, version, iface_number);
			if (instance) {
				instance->dl_handle = dl_handle;
				strcpy(instance->library_name, dl_name);
				goto found;
			}

			/*
			 * No matching interfaces found, try next shared object
			 */
			if (g_component_handle != 0xFFFFFFFF) {
				qb_hdb_handle_destroy
				    (&plugin_component_instance_database,
				     g_component_handle);
				g_component_handle = 0xFFFFFFFF;
			}
			dlclose(dl_handle);
		}

	/* scanning for pluginso loop */
	if (scandir_entries > 0) {
		int32_t i;
		for (i = 0; i < scandir_entries; i++) {
			free(scandir_list[i]);
		}
		free(scandir_list);
	}
	g_component_handle = 0xFFFFFFFF;
	return -1;

found:
	*instance_ret = instance;
	if (scandir_entries > 0) {
		int32_t i;
		for (i = 0; i < scandir_entries; i++) {
			free(scandir_list[i]);
		}
		free(scandir_list);
	}
	g_component_handle = 0xFFFFFFFF;
	return 0;
}

static uint32_t plugin_initialized = 0;

int32_t plugin_ifact_reference(qb_handle_t * iface_handle,
			   const char *iface_name,
			   int32_t version, void **iface, void *context)
{
	struct plugin_iface_instance *iface_instance;
	struct plugin_component_instance *instance;
	uint32_t iface_number;
	int32_t res;
	uint32_t i;

	/*
	 * Determine if the component is already loaded
	 */
	instance = plugin_comp_find(iface_name, version, &iface_number);
	if (instance) {
		goto found;
	}

	if (plugin_initialized == 0) {
		plugin_initialized = 1;
		defaults_path_build();
		ld_library_path_build();
		ldso_path_build("/etc", "ld.so.conf");
	}
// TODO error checking in this code is weak
	/*
	 * Search through all pluginso files for desired interface
	 */
	for (i = 0; i < path_list_entries; i++) {
		res = interface_find_and_load(path_list[i],
					      iface_name,
					      version,
					      &instance, &iface_number);

		if (res == 0) {
			goto found;
		}
	}

	/*
	 * No matching interfaces found in all shared objects
	 */
	return (-1);

found:
	*iface = instance->ifaces[iface_number].interfaces;
	if (instance->ifaces[iface_number].constructor) {
		instance->ifaces[iface_number].constructor(context);
	}
	qb_hdb_handle_create(&plugin_iface_instance_database,
			     sizeof(struct plugin_iface_instance),
			     iface_handle);
	qb_hdb_handle_get(&plugin_iface_instance_database,
			  *iface_handle, (void *)&iface_instance);
	iface_instance->component_handle = instance->comp_handle;
	iface_instance->context = context;
	iface_instance->destructor = instance->ifaces[iface_number].destructor;
	qb_hdb_handle_put(&plugin_iface_instance_database, *iface_handle);
	return (0);
}

int32_t plugin_ifact_release(qb_handle_t handle)
{
	struct plugin_iface_instance *iface_instance;
	int32_t res = 0;

	res = qb_hdb_handle_get(&plugin_iface_instance_database,
				handle, (void *)&iface_instance);

	if (iface_instance->destructor) {
		iface_instance->destructor(iface_instance->context);
	}

	qb_hdb_handle_put(&plugin_component_instance_database,
			  iface_instance->component_handle);
	qb_hdb_handle_put(&plugin_iface_instance_database, handle);
	qb_hdb_handle_destroy(&plugin_iface_instance_database, handle);

	return (res);
}

void plugin_component_register(struct plugin_comp *comp)
{
	struct plugin_component_instance *instance;
	static qb_handle_t comp_handle;

	qb_hdb_handle_create(&plugin_component_instance_database,
			     sizeof(struct plugin_component_instance),
			     &comp_handle);
	qb_hdb_handle_get(&plugin_component_instance_database,
			  comp_handle, (void *)&instance);

	instance->ifaces = comp->ifaces;
	instance->iface_count = comp->iface_count;
	instance->comp_handle = comp_handle;
	instance->dl_handle = NULL;

	qb_hdb_handle_put(&plugin_component_instance_database, comp_handle);

	g_component_handle = comp_handle;
}
