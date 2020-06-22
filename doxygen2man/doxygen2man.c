/*
 * Copyright (C) 2018-2020 Red Hat, Inc.  All rights reserved.
 *
 * Author: Christine Caulfield <ccaulfie@redhat.com>
 *
 * This software licensed under GPL-2.0+
 */


/*
 * NOTE: this code is very rough, it does the bare minimum to parse the
 * XML out from doxygen and is probably very fragile to changes in that XML
 * schema. It probably leaks memory all over the place too.
 *
 * In its favour, it *does* generate nice man pages and should only be run very ocasionally
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <libxml/tree.h>
#include <qb/qblist.h>
#include <qb/qbmap.h>

/*
 * This isn't a maximum size, it just defines how long a parameter
 * type can get before we decide it's not worth lining everything up.
 * It's mainly to stop function pointer types (which can get VERY long because
 * of all *their* parameters) making everything else 'line-up' over separate lines
 */
#define LINE_LENGTH 80

static int print_ascii = 1;
static int print_man = 0;
static int print_params = 0;
static int num_functions = 0;
static int quiet=0;
static const char *man_section="3";
static const char *package_name="Package";
static const char *header="Programmer's Manual";
static const char *company="Red Hat";
static const char *output_dir="./";
static const char *xml_dir = "./xml/";
static const char *xml_file;
static const char *manpage_date = NULL;
static const char *headerfile = NULL;
static const char *header_prefix = "";
static long manpage_year = LONG_MIN;
static long start_year = 2010;
static struct qb_list_head params_list;
static struct qb_list_head retval_list;
static qb_map_t *function_map;
static qb_map_t *structures_map;
static qb_map_t *used_structures_map;

struct param_info {
	char *paramname;
	char *paramtype;
	char *paramdesc;
	struct param_info *next;
	struct qb_list_head list;
};

struct struct_info {
	enum {STRUCTINFO_STRUCT, STRUCTINFO_ENUM} kind;
	char *structname;
	struct qb_list_head params_list; /* our params */
	struct qb_list_head list;
};

static char *get_texttree(int *type, xmlNode *cur_node, char **returntext, char **notetext);
static void traverse_node(xmlNode *parentnode, const char *leafname, void (do_members(xmlNode*, void*)), void *arg);

static void free_paraminfo(struct param_info *pi)
{
	free(pi->paramname);
	free(pi->paramtype);
	free(pi->paramdesc);
	free(pi);
}

static char *get_attr(xmlNode *node, const char *tag)
{
	xmlAttr *this_attr;

	for (this_attr = node->properties; this_attr; this_attr = this_attr->next) {
		if (this_attr->type == XML_ATTRIBUTE_NODE && strcmp((char *)this_attr->name, tag) == 0) {
			return strdup((char *)this_attr->children->content);
		}
	}
	return NULL;
}

static char *get_child(xmlNode *node, const char *tag)
{
	xmlNode *this_node;
	xmlNode *child;
	char buffer[1024] = {'\0'};
	char *refid = NULL;
	char *declname = NULL;

	for (this_node = node->children; this_node; this_node = this_node->next) {
		if ((strcmp( (char*)this_node->name, "declname") == 0)) {
			declname = strdup((char*)this_node->children->content);
		}

		if ((this_node->type == XML_ELEMENT_NODE && this_node->children) && ((strcmp((char *)this_node->name, tag) == 0))) {
			refid = NULL;
			for (child = this_node->children; child; child = child->next) {
				if (child->content) {
					strncat(buffer, (char *)child->content, sizeof(buffer)-1);
				}

				if ((strcmp( (char*)child->name, "ref") == 0)) {
					if (child->children->content) {
						strncat(buffer,(char *)child->children->content, sizeof(buffer)-1);
					}
					refid = get_attr(child, "refid");
				}
			}
		}
		if (declname && refid) {
			qb_map_put(used_structures_map, refid, declname);
		}
	}
	return strdup(buffer);
}

static struct param_info *find_param_by_name(struct qb_list_head *list, const char *name)
{
	struct qb_list_head *iter;
	struct param_info *pi;

	qb_list_for_each(iter, list) {
		pi = qb_list_entry(iter, struct param_info, list);
		if (strcmp(pi->paramname, name) == 0) {
			return pi;
		}
	}
	return NULL;
}

static int not_all_whitespace(char *string)
{
	unsigned int i;

	for (i=0; i<strlen(string); i++) {
		if (string[i] != ' ' &&
		    string[i] != '\n' &&
		    string[i] != '\r' &&
		    string[i] != '\t')
			return 1;
	}
	return 0;
}

static void get_param_info(xmlNode *cur_node, struct qb_list_head *list)
{
	xmlNode *this_tag;
	xmlNode *sub_tag;
	char *paramname = NULL;
	char *paramdesc = NULL;
	struct param_info *pi;

	/* This is not robust, and very inflexible */
	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
		for (sub_tag = this_tag->children; sub_tag; sub_tag = sub_tag->next) {
			if (sub_tag->type == XML_ELEMENT_NODE && strcmp((char *)sub_tag->name, "parameternamelist") == 0) {
				paramname = (char*)sub_tag->children->next->children->content;
			}
			if (sub_tag->type == XML_ELEMENT_NODE && strcmp((char *)sub_tag->name, "parameterdescription") == 0 &&
			    paramname) {
				paramdesc = (char*)sub_tag->children->next->children->content;

				/* Add text to the param_map */
				pi = find_param_by_name(list, paramname);
				if (pi) {
					pi->paramdesc = paramdesc;
				}
				else {
					pi = malloc(sizeof(struct param_info));
					if (pi) {
						pi->paramname = paramname;
						pi->paramdesc = paramdesc;
						pi->paramtype = NULL; /* retval */
						qb_list_add_tail(&pi->list, list);
					}
				}
			}
		}
	}
}

static char *get_text(xmlNode *cur_node, char **returntext, char **notetext)
{
	xmlNode *this_tag;
	xmlNode *sub_tag;
	char *kind;
	char buffer[4096] = {'\0'};

	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
		if (this_tag->type == XML_TEXT_NODE && strcmp((char *)this_tag->name, "text") == 0) {
			if (not_all_whitespace((char*)this_tag->content)) {
				strncat(buffer, (char*)this_tag->content, sizeof(buffer)-1);
				strncat(buffer, "\n", sizeof(buffer)-1);
			}
		}
		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "emphasis") == 0) {
			if (print_man) {
				strncat(buffer, "\\fB", sizeof(buffer)-1);
			}
			strncat(buffer, (char*)this_tag->children->content, sizeof(buffer)-1);
			if (print_man) {
				strncat(buffer, "\\fR", sizeof(buffer)-1);
			}
		}

		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "ref") == 0) {
			if (print_man) {
				strncat(buffer, "\\fI", sizeof(buffer)-1);
			}
			strncat(buffer, (char*)this_tag->children->content, sizeof(buffer)-1);
			if (print_man) {
				strncat(buffer, "\\fR", sizeof(buffer)-1);
			}
		}

		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "itemizedlist") == 0) {
			for (sub_tag = this_tag->children; sub_tag; sub_tag = sub_tag->next) {
				if (sub_tag->type == XML_ELEMENT_NODE && strcmp((char *)sub_tag->name, "listitem") == 0) {
					strncat(buffer, (char*)sub_tag->children->children->content, sizeof(buffer)-1);
					strncat(buffer, "\n", sizeof(buffer)-1);
				}
			}
		}

		/* Look for subsections - return value & params */
		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "simplesect") == 0) {
			char *tmp;

			kind = get_attr(this_tag, "kind");
			tmp = get_text(this_tag->children, NULL, NULL);

			if (returntext && strcmp(kind, "return") == 0) {
				*returntext = tmp;
			}
			if (notetext && strcmp(kind, "note") == 0) {
				*notetext = tmp;
			}
		}

		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "parameterlist") == 0) {
			kind = get_attr(this_tag, "kind");
			if (strcmp(kind, "param") == 0) {
				get_param_info(this_tag, &params_list);
			}
			if (strcmp(kind, "retval") == 0) {
				get_param_info(this_tag, &retval_list);
			}
		}
	}
	return strdup(buffer);
}

static void read_structname(xmlNode *cur_node, void *arg)
{
	struct struct_info *si=arg;
	xmlNode *this_tag;

	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
		if (strcmp((char*)this_tag->name, "compoundname") == 0) {
			si->structname = strdup((char*)this_tag->children->content);
		}
	}
}


static void read_headername(xmlNode *cur_node, void *arg)
{
	char **h_file = arg;
	xmlNode *this_tag;

	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
		if (strcmp((char*)this_tag->name, "compoundname") == 0) {
			*h_file = strdup((char*)this_tag->children->content);
		}
	}
}


/* Called from traverse_node() */
static void read_struct(xmlNode *cur_node, void *arg)
{
	xmlNode *this_tag;
	struct struct_info *si=arg;
	struct param_info *pi;
	char fullname[1024];
	char *type = NULL;
	char *name = NULL;
	const char *args="";

	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
		if (strcmp((char*)this_tag->name, "type") == 0) {
			type = (char*)this_tag->children->content;
			/* If type is NULL then look for a ref - it's probably an external struct or typedef */
			if (type == NULL) {
				type = get_child(this_tag, "ref");
			}
		}
		if (strcmp((char*)this_tag->name, "name") == 0) {
			name = (char*)this_tag->children->content;
		}
		if (this_tag->children && strcmp((char*)this_tag->name, "argsstring") == 0) {
			args = (char*)this_tag->children->content;
		}
	}

	if (name) {
		pi = malloc(sizeof(struct param_info));
		if (pi) {
			snprintf(fullname, sizeof(fullname), "%s%s", name, args);
			pi->paramtype = type?strdup(type):strdup("");
			pi->paramname = strdup(fullname);
			pi->paramdesc = NULL;
			qb_list_add_tail(&pi->list, &si->params_list);
		}
	}
}

static int read_structure_from_xml(const char *refid, const char *name)
{
	char fname[PATH_MAX];
	xmlNode *rootdoc;
	xmlDocPtr doc;
	struct struct_info *si;
	struct stat st;
	int ret = -1;

	snprintf(fname, sizeof(fname),  "%s/%s.xml", xml_dir, refid);

	/* Don't call into libxml if the file does not exist - saves unwanted error messages */
	if (stat(fname, &st) == -1) {
		return -1;
	}

	doc = xmlParseFile(fname);
	if (doc == NULL) {
		fprintf(stderr, "Error: unable to open xml file for %s\n", refid);
		return -1;
	}

	rootdoc = xmlDocGetRootElement(doc);
	if (!rootdoc) {
		fprintf(stderr, "Can't find \"document root\"\n");
		return -1;
	}

	si = malloc(sizeof(struct struct_info));
	if (si) {
		si->kind = STRUCTINFO_STRUCT;
		qb_list_init(&si->params_list);
		traverse_node(rootdoc, "memberdef", read_struct, si);
		traverse_node(rootdoc, "compounddef", read_structname, si);
		ret = 0;
		qb_map_put(structures_map, refid, si);
	}
	xmlFreeDoc(doc);

	return ret;
}


static void print_param(FILE *manfile, struct param_info *pi, int field_width, int bold, const char *delimiter)
{
	const char *asterisks = "  ";
	char *type = pi->paramtype;
	int typelength = strlen(type);

	/* Reformat pointer params so they look nicer */
	if (typelength > 0 && pi->paramtype[typelength-1] == '*') {
		asterisks=" *";
		type = strdup(pi->paramtype);
		type[typelength-1] = '\0';

		/* Cope with double pointers */
		if (typelength > 1 && pi->paramtype[typelength-2] == '*') {
			asterisks="**";
			type[typelength-2] = '\0';
		}

		/* Tidy function pointers */
		if (typelength > 1 && pi->paramtype[typelength-2] == '(') {
			asterisks="(*";
			type[typelength-2] = '\0';
		}
	}

	fprintf(manfile, "    %s%-*s%s%s\\fI%s\\fP%s\n",
		bold?"\\fB":"", field_width, type,
		asterisks, bold?"\\fP":"", pi->paramname, delimiter);

	if (type != pi->paramtype) {
		free(type);
	}
}

static void print_structure(FILE *manfile, struct struct_info *si)
{
	struct param_info *pi;
	struct qb_list_head *iter;
	unsigned int max_param_length=0;

	fprintf(manfile, ".nf\n");
	fprintf(manfile, "\\fB\n");

	qb_list_for_each(iter, &si->params_list) {
		pi = qb_list_entry(iter, struct param_info, list);
		if (strlen(pi->paramtype) > max_param_length) {
			max_param_length = strlen(pi->paramtype);
		}
	}

	if (si->kind == STRUCTINFO_STRUCT) {
		fprintf(manfile, "struct %s {\n", si->structname);
	} else if (si->kind == STRUCTINFO_ENUM) {
		fprintf(manfile, "enum %s {\n", si->structname);
	} else {
		fprintf(manfile, "%s {\n", si->structname);
	}

	qb_list_for_each(iter, &si->params_list) {
		pi = qb_list_entry(iter, struct param_info, list);
		print_param(manfile, pi, max_param_length, 0,";");
	}
	fprintf(manfile, "};\n");

	fprintf(manfile, "\\fP\n");
	fprintf(manfile, ".fi\n");
}

char *get_texttree(int *type, xmlNode *cur_node, char **returntext, char **notetext)
{
	xmlNode *this_tag;
	char *tmp = NULL;
	char buffer[4096] = {'\0'};

	for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {

		if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "para") == 0) {
			tmp = get_text(this_tag, returntext, notetext);
			strncat(buffer, tmp, sizeof(buffer)-1);
			strncat(buffer, "\n", sizeof(buffer)-1);
			free(tmp);
		}
	}

	if (buffer[0]) {
		return strdup(buffer);
	} else {
		return NULL;
	}
}

/* The text output is VERY basic and just a check that it's working really */
static void print_text(char *name, char *def, char *brief, char *args, char *detailed,
		       struct qb_list_head *param_list, char *returntext, char *notetext)
{
	printf(" ------------------ %s --------------------\n", name);
	printf("NAME\n");
	if (brief) {
		printf("        %s - %s\n", name, brief);
	} else {
		printf("        %s\n", name);
	}

	printf("SYNOPSIS\n");
	printf("        #include <%s%s>\n", header_prefix, headerfile);
	printf("        %s %s\n\n", name, args);

	printf("DESCRIPTION\n");
	printf("        %s\n", detailed);

	if (returntext) {
		printf("RETURN VALUE\n");
		printf("        %s\n", returntext);
	}
	if (notetext) {
		printf("NOTE\n");
		printf("        %s\n", notetext);
	}
}

/* Print a long string with para marks in it. */
static void man_print_long_string(FILE *manfile, char *text)
{
	char *next_nl;
	char *current = text;

	next_nl = strchr(text, '\n');
	while (next_nl && *next_nl != '\0') {
		*next_nl = '\0';
		if (strlen(current)) {
			fprintf(manfile, ".PP\n%s\n", current);
		}

		*next_nl = '\n';
		current = next_nl+1;
		next_nl = strchr(current, '\n');
	}
}

static void print_manpage(char *name, char *def, char *brief, char *args, char *detailed,
			  struct qb_list_head *param_map, char *returntext, char *notetext)
{
	char manfilename[PATH_MAX];
	char gendate[64];
	const char *dateptr = gendate;
	FILE *manfile;
	time_t t;
	struct tm *tm;
	qb_map_iter_t *map_iter;
	struct qb_list_head *iter;
	struct qb_list_head *tmp;
	const char *p;
	void *data;
	unsigned int max_param_type_len;
	unsigned int max_param_name_len;
	unsigned int num_param_descs;
	int param_count = 0;
	int param_num = 0;
	struct param_info *pi;

	t = time(NULL);
	tm = localtime(&t);
	if (!tm) {
		perror("unable to get localtime");
		exit(1);
	}
	strftime(gendate, sizeof(gendate), "%Y-%m-%d", tm);

	if (manpage_date) {
		dateptr = manpage_date;
	}
	if (manpage_year == LONG_MIN) {
		manpage_year = tm->tm_year+1900;
	}

	snprintf(manfilename, sizeof(manfilename), "%s/%s.%s", output_dir, name, man_section);
	manfile = fopen(manfilename, "w+");
	if (!manfile) {
		perror("unable to open output file");
		printf("%s", manfilename);
		exit(1);
	}

	/* Work out the length of the parameters, so we can line them up   */
	max_param_type_len = 0;
	max_param_name_len = 0;
	num_param_descs = 0;

	qb_list_for_each(iter, &params_list) {
		pi = qb_list_entry(iter, struct param_info, list);

		/* It's mainly macros that break this,
		 * macros need more work
		 */
		if (!pi->paramtype) {
			pi->paramtype = strdup("");
		}
		if ((strlen(pi->paramtype) < LINE_LENGTH) &&
		    (strlen(pi->paramtype) > max_param_type_len)) {
			max_param_type_len = strlen(pi->paramtype);
		}
		if (strlen(pi->paramname) > max_param_name_len) {
			max_param_name_len = strlen(pi->paramname);
		}
		if (pi->paramdesc) {
			num_param_descs++;
		}
		param_count++;
	}

	/* Off we go */

	fprintf(manfile, ".\\\"  Automatically generated man page, do not edit\n");
	fprintf(manfile, ".TH %s %s %s \"%s\" \"%s\"\n", name, man_section, dateptr, package_name, header);

	fprintf(manfile, ".SH NAME\n");
	if (brief) {
		fprintf(manfile, "%s \\- %s\n", name, brief);
	} else {
		fprintf(manfile, "%s\n", name);
	}

	fprintf(manfile, ".SH SYNOPSIS\n");
	fprintf(manfile, ".nf\n");
	fprintf(manfile, ".B #include <%s%s>\n", header_prefix, headerfile);
	fprintf(manfile, ".sp\n");
	fprintf(manfile, "\\fB%s\\fP(\n", def);


	qb_list_for_each(iter, &params_list) {
		pi = qb_list_entry(iter, struct param_info, list);

		print_param(manfile, pi, max_param_type_len, 1, ++param_num < param_count?",":"");
	}

	fprintf(manfile, ");\n");
	fprintf(manfile, ".fi\n");

	if (print_params && num_param_descs) {
		fprintf(manfile, ".SH PARAMS\n");

		qb_list_for_each(iter, &params_list) {
		pi = qb_list_entry(iter, struct param_info, list);
			fprintf(manfile, "\\fB%-*s \\fP\\fI%s\\fP\n", (int)max_param_name_len, pi->paramname,
				pi->paramdesc);
			fprintf(manfile, ".PP\n");
		}
	}

	fprintf(manfile, ".SH DESCRIPTION\n");
	man_print_long_string(manfile, detailed);

	if (qb_map_count_get(used_structures_map)) {
		int first_struct = 1;

		map_iter = qb_map_iter_create(used_structures_map);
		for (p = qb_map_iter_next(map_iter, &data); p; p = qb_map_iter_next(map_iter, &data)) {
			struct struct_info *si;
			const char *refid = p;
			char *refname = data;

			/* If it's not been read in - go and look for it */
			si = qb_map_get(structures_map, refid);
			if (!si) {
				if (!read_structure_from_xml(refid, refname)) {
					si = qb_map_get(structures_map, refid);
				}
			}

			/* Only print header if the struct files exist - sometimes they don't */
			if (si && first_struct) {
				fprintf(manfile, ".SH STRUCTURES\n");
				first_struct = 0;
			}
			if (si) {
				print_structure(manfile, si);
			}

		}
		qb_map_iter_free(map_iter);

		fprintf(manfile, ".RE\n");
	}

	if (returntext) {
		fprintf(manfile, ".SH RETURN VALUE\n");
		man_print_long_string(manfile, returntext);
	}
	if (notetext) {
		fprintf(manfile, ".SH NOTE\n");
		man_print_long_string(manfile, notetext);
	}

	qb_list_for_each(iter, &retval_list) {
		pi = qb_list_entry(iter, struct param_info, list);

		fprintf(manfile, "\\fB%-*s \\fP\\fI%s\\fP\n", 10, pi->paramname,
			pi->paramdesc);
		fprintf(manfile, ".PP\n");
	}

	fprintf(manfile, ".SH SEE ALSO\n");
	fprintf(manfile, ".PP\n");
	fprintf(manfile, ".nh\n");
	fprintf(manfile, ".ad l\n");

	param_num = 0;
	map_iter = qb_map_iter_create(function_map);
	for (p = qb_map_iter_next(map_iter, &data); p; p = qb_map_iter_next(map_iter, &data)) {

		/* Exclude us! */
		if (strcmp(data, name)) {
			fprintf(manfile, "\\fI%s(%s)%s", (char *)data, man_section,
				param_num < (num_functions - 1)?", ":"");
		}
		param_num++;
	}
	qb_map_iter_free(map_iter);

	fprintf(manfile, "\n");
	fprintf(manfile, ".ad\n");
	fprintf(manfile, ".hy\n");
	fprintf(manfile, ".SH \"COPYRIGHT\"\n");
	fprintf(manfile, ".PP\n");
	fprintf(manfile, "Copyright (C) %4ld-%4ld %s, Inc. All rights reserved.\n", start_year, manpage_year, company);
	fclose(manfile);

	/* Free the params & retval info */
	qb_list_for_each_safe(iter, tmp, &params_list) {
		pi = qb_list_entry(iter, struct param_info, list);
		qb_list_del(&pi->list);
		free_paraminfo(pi);
	}

	qb_list_for_each_safe(iter, tmp, &retval_list) {
		pi = qb_list_entry(iter, struct param_info, list);
		qb_list_del(&pi->list);
		free_paraminfo(pi);
	}

	/* Free used-structures map */
	map_iter = qb_map_iter_create(used_structures_map);
	for (p = qb_map_iter_next(map_iter, &data); p; p = qb_map_iter_next(map_iter, &data)) {
		qb_map_rm(used_structures_map, p);
		free(data);
	}
}

/* Same as traverse_members, but to collect function names */
static void collect_functions(xmlNode *cur_node, void *arg)
{
	xmlNode *this_tag;
	char *kind;
	char *name = NULL;

	if (cur_node->name && strcmp((char *)cur_node->name, "memberdef") == 0) {

		kind = get_attr(cur_node, "kind");
		if (kind && strcmp(kind, "function") == 0) {

			for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
				if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "name") == 0) {
					name = strdup((char *)this_tag->children->content);
				}
			}

			if (name) {
				qb_map_put(function_map, name, name);
				num_functions++;
			}
		}
	}
}

/* Same as traverse_members, but to collect enums. The behave like structures for,
   but, for some reason, are in the main XML file rather than their own */
static void collect_enums(xmlNode *cur_node, void *arg)
{
	xmlNode *this_tag;
	struct struct_info *si;
	char *kind;
	char *refid = NULL;
	char *name = NULL;

	if (cur_node->name && strcmp((char *)cur_node->name, "memberdef") == 0) {

		kind = get_attr(cur_node, "kind");
		if (kind && strcmp(kind, "enum") == 0) {
			refid = get_attr(cur_node, "id");

			for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next) {
				if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "name") == 0) {
					name = strdup((char *)this_tag->children->content);
				}
			}

			if (name) {
				si = malloc(sizeof(struct struct_info));
				if (si) {
					si->kind = STRUCTINFO_ENUM;
					qb_list_init(&si->params_list);
					si->structname = strdup(name);
					traverse_node(cur_node, "enumvalue", read_struct, si);
					qb_map_put(structures_map, refid, si);
				}
			}
		}
	}
}

static void traverse_members(xmlNode *cur_node, void *arg)
{
	xmlNode *this_tag;

	if (cur_node->name && strcmp((char *)cur_node->name, "memberdef") == 0) {
		char *kind = NULL;
		char *def = NULL;
		char *args = NULL;
		char *name = NULL;
		char *brief = NULL;
		char *detailed = NULL;
		char *returntext = NULL;
		char *notetext = NULL;
		int type;

		kind=def=args=name=NULL;

		kind = get_attr(cur_node, "kind");

		for (this_tag = cur_node->children; this_tag; this_tag = this_tag->next)
		{
			if (!this_tag->children || !this_tag->children->content)
				continue;

			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "definition") == 0)
				def = strdup((char *)this_tag->children->content);
			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "argsstring") == 0)
				args = strdup((char *)this_tag->children->content);
			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "name") == 0)
				name = strdup((char *)this_tag->children->content);

			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "briefdescription") == 0) {
				brief = get_texttree(&type, this_tag, &returntext, &notetext);
				if (brief) {
					/*
					 * apparently brief text contains extra trailing space and 2 \n.
					 * remove them.
					 */
					brief[strlen(brief) - 3] = '\0';
				}
			}
			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "detaileddescription") == 0) {
				detailed = get_texttree(&type, this_tag, &returntext, &notetext);
			}
			/* Get all the params */
			if (this_tag->type == XML_ELEMENT_NODE && strcmp((char *)this_tag->name, "param") == 0) {
				char *param_type = get_child(this_tag, "type");
				char *param_name = get_child(this_tag, "declname");
				struct param_info *pi = malloc(sizeof(struct param_info));
				if (pi) {
					pi->paramname = param_name;
					pi->paramtype = param_type;
					pi->paramdesc = NULL;
					qb_list_add_tail(&pi->list, &params_list);
				}
			}
		}

		if (kind && strcmp(kind, "function") == 0) {

			/* Make sure function has a doxygen description */
			if (!detailed) {
				fprintf(stderr, "No doxygen description for function '%s' - please fix this\n", name);
				return;
			}

			if (print_man) {
				if (!quiet) {
					printf("Printing manpage for %s\n", name);
				}
				print_manpage(name, def, brief, args, detailed, &params_list, returntext, notetext);
			}
			else {
				print_text(name, def, brief, args, detailed, &params_list, returntext, notetext);
			}

		}

		free(kind);
		free(def);
		free(args);
		free(name);
	}
}


static void traverse_node(xmlNode *parentnode, const char *leafname, void (do_members(xmlNode*, void*)), void *arg)
{
	xmlNode *cur_node;

	for (cur_node = parentnode->children; cur_node; cur_node = cur_node->next) {

		if (cur_node->type == XML_ELEMENT_NODE && cur_node->name
		    && strcmp((char*)cur_node->name, leafname)==0) {
			do_members(cur_node, arg);
			continue;
		}
		if (cur_node->type == XML_ELEMENT_NODE) {
			traverse_node(cur_node, leafname, do_members, arg);
		}
	}
}


static void usage(char *name)
{
	printf("Usage:\n");
	printf("      %s [OPTIONS] <XML file>\n", name);
	printf("\n");
	printf(" This is a tool to generate API manpages from a doxygen-annotated header file.\n");
	printf(" First run doxygen on the file and then run this program against the main XML file\n");
	printf(" it created and the directory containing the ancilliary files. It will then\n");
	printf(" output a lot of *.3 man page files which you can then ship with your library.\n");
	printf("\n");
	printf(" You will need to invoke this program once for each .h file in your library,\n");
	printf(" using the name of the generated .xml file. This file will usually be called\n");
	printf(" something like <include-file>_8h.xml, eg qbipcs_8h.xml\n");
	printf("\n");
	printf(" If you want HTML output then simpy use nroff on the generated files as you\n");
	printf(" would do with any other man page.\n");
	printf("\n");
	printf("       -a            Print ASCII dump of man pages to stdout\n");
	printf("       -m            Write man page files to <output dir>\n");
	printf("       -P            Print PARAMS section\n");
	printf("       -s <s>        Write man pages into section <s> <default 3)\n");
	printf("       -p <package>  Use <package> name. default <Package>\n");
	printf("       -H <header>   Set header (default \"Programmer's Manual\"\n");
	printf("       -I <include>  Set include filename (default taken from xml)\n");
	printf("       -i <prefix>   Prefix for include files. eg qb/ (default \"\")\n");
	printf("       -C <company>  Company name in copyright (defaults to Red Hat)\n");
	printf("       -D <date>     Date to print at top of man pages (format not checked, default: today)\n");
	printf("       -S <year>     Start year to print at end of copyright line (default: 2010)\n");
	printf("       -Y <year>     Year to print at end of copyright line (default: today's year)\n");
	printf("       -o <dir>      Write all man pages to <dir> (default .)\n");
	printf("       -d <dir>      Directory for XML files (./xml/)\n");
	printf("       -h            Print this usage text\n");
}

static long get_year(char *optionarg, char optionchar)
{
	long year = strtol(optionarg, NULL, 10);
	/*
	 * Don't make too many assumptions about the year. I was on call at the
	 * 2000 rollover. #experience
	 */
	if (year == LONG_MIN || year == LONG_MAX ||
	    year < 1900) {
		fprintf(stderr, "Value passed to -%c is not a valid year number\n", optionchar);
		return 0;
	}
	return year;
}

int main(int argc, char *argv[])
{
	xmlNode *rootdoc;
	xmlDocPtr doc;
	int opt;
	char xml_filename[PATH_MAX];

	while ( (opt = getopt_long(argc, argv, "H:amqPD:Y:s:S:d:o:p:f:I:i:C:h?", NULL, NULL)) != EOF)
	{
		switch(opt)
		{
			case 'a':
				print_ascii = 1;
				print_man = 0;
				break;
			case 'm':
				print_man = 1;
				print_ascii = 0;
				break;
			case 'P':
				print_params = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case 'I':
				headerfile = optarg;
				break;
			case 'i':
				header_prefix = optarg;
				break;
			case 'C':
				company = optarg;
				break;
			case 's':
				man_section = optarg;
				break;
			case 'S':
				start_year = get_year(optarg, 'S');
				if (start_year == 0) {
					return 1;
				}
				break;
			case 'd':
				xml_dir = optarg;
				break;
			case 'D':
				manpage_date = optarg;
				break;
			case 'Y':
				manpage_year = get_year(optarg, 'Y');
				if (manpage_year == 0) {
					return 1;
				}
				break;
			case 'p':
				package_name = optarg;
				break;
			case 'H':
				header = optarg;
				break;
			case 'o':
				output_dir = optarg;
				break;
			case '?':
			case 'h':
				usage(argv[0]);
				return 0;
		}
	}

	if (argv[optind]) {
		xml_file = argv[optind];
	}
	if (!xml_file) {
		usage(argv[0]);
		exit(1);
	}

	if (!quiet) {
		printf("reading %s ... ", xml_file);
	}

	snprintf(xml_filename, sizeof(xml_filename), "%s/%s", xml_dir, xml_file);
	doc = xmlParseFile(xml_filename);
	if (doc == NULL) {
		fprintf(stderr, "Error: unable to read xml file %s\n", xml_filename);
		exit(1);
	}

	rootdoc = xmlDocGetRootElement(doc);
	if (!rootdoc) {
		fprintf(stderr, "Can't find \"document root\"\n");
		exit(1);
	}
	if (!quiet)
		printf("done.\n");

	/* Get our header file name */
	if (!headerfile) {
		traverse_node(rootdoc, "compounddef", read_headername, &headerfile);
	}
	/* Default to *something* if it all goes wrong */
	if (!headerfile) {
		headerfile = "unknown.h";
	}

	qb_list_init(&params_list);
	qb_list_init(&retval_list);
	structures_map = qb_hashtable_create(10);
	function_map = qb_hashtable_create(10);
	used_structures_map = qb_hashtable_create(10);

	/* Collect functions */
	traverse_node(rootdoc, "memberdef", collect_functions, NULL);

	/* Collect enums */
	traverse_node(rootdoc, "memberdef", collect_enums, NULL);

	/* print pages */
	traverse_node(rootdoc, "memberdef", traverse_members, NULL);

	return 0;
}
