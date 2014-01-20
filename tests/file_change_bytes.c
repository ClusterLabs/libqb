/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
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

#include <sys/types.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void usage(void)
{
	printf("Usage: [ -i input_file] [ -o output_file ] [ -n no_bytes]\n");
	printf("Changes no_bytes (default 1024) in input_file (default = - = stdin) and store\n");
	printf("result to output_file (default = - = stdout). It's possible to use same file\n");
	printf("as both input and output\n");

	exit(1);
}

static void init_rand(void) {
	unsigned int init_v;

	init_v = time(NULL) + getpid();

	srand(init_v);
}

int main(int argc, char *argv[])
{
	FILE *fi, *fo;
	int i;
	const char *input_file_name;
	const char *output_file_name;
	int no_bytes;
	char *ep;
	int ch;
	unsigned char *data;
	size_t data_size;
	size_t data_pos;
	size_t input_data_size;
	unsigned char buf[1024];

	input_file_name = "-";
	output_file_name = "-";
	no_bytes = 1024;

	while ((ch = getopt(argc, argv, "hi:o:n:")) != -1) {
		switch (ch) {
		case 'i':
			input_file_name = optarg;
			break;
		case 'n':
			no_bytes = strtol(optarg, &ep, 10);
			if (no_bytes < 0 || *ep != '\0') {
				warnx("illegal number -- %s", argv[2]);
				usage();
			}
			break;
		case 'o':
			output_file_name = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (strcmp(input_file_name, "-") == 0) {
		fi = stdin;
	} else {
		fi = fopen(input_file_name, "rb");
		if (fi == NULL) {
			err(1, "%s", input_file_name);
		}
	}

	/*
	 * Open and fully read input file
	 */
	data = NULL;
	data_size = 0;
	data_pos = 0;
	while ((input_data_size = fread(buf, 1, sizeof(buf), fi)) != 0) {
		if (data_pos + input_data_size >= data_size) {
			data_size = (data_size + input_data_size) * 2;
			assert((data = realloc(data, data_size)) != NULL);
		}
		memcpy(data + data_pos, buf, input_data_size);
		data_pos += input_data_size;
	}
	fclose(fi);

	/*
	 * Change bytes
	 */
	init_rand();

	for (i = 0; i < no_bytes; i++) {
		data[rand() % data_pos] = rand();
	}

	/*
	 * Fully write ouput file
	 */
	if (strcmp(output_file_name, "-") == 0) {
		fo = stdout;
	} else {
		fo = fopen(output_file_name, "wb");
		if (fo == NULL) {
			err(1, "%s", output_file_name);
		}
	}
	assert(fwrite(data, 1, data_pos, fo) == data_pos);
	fclose(fo);

	free(data);

	return (0);
}
