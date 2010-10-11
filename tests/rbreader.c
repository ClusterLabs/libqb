
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbrb.h>
#include <qb/qbutil.h>

#define BUFFER_CHUNK_SIZE (50*50*10)
static qb_ringbuffer_t *rb = NULL;

static void sigterm_handler(int32_t num)
{
	printf("reader: %s(%d)\n", __func__, num);
	qb_rb_close(rb, QB_FALSE);
	exit(0);
}

static void libqb_log_writer(const char *file_name,
			     int32_t file_line,
			     int32_t severity, const char *msg)
{
	printf("libqb:reader: %s:%d %s\n", file_name, file_line, msg);
}

int32_t main(int32_t argc, char *argv[])
{
	ssize_t num_read;
	int8_t buffer[BUFFER_CHUNK_SIZE];
	int32_t keep_reading = 1;

	signal(SIGINT, sigterm_handler);

	qb_util_set_log_function(libqb_log_writer);

	rb = qb_rb_open("tester", BUFFER_CHUNK_SIZE * 3,
			QB_RB_FLAG_SHARED_PROCESS | QB_RB_FLAG_CREATE, 0);

	if (rb == NULL) {
		printf("reader: failed to create ringbuffer\n");
		return -1;
	}
	while (keep_reading) {
		num_read =
		    qb_rb_chunk_read(rb, buffer, BUFFER_CHUNK_SIZE, 5500);
		if (num_read == -1) {
			printf("reader: nothing to read\n");
			//usleep(1);
		}
	}
	qb_rb_close(rb, QB_FALSE);
	return 0;
}
