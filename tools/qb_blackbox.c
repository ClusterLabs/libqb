/*
 * Copyright (C) 2012 Andrew Beekhof <andrew@beekhof.net>
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

#include <qb/qblog.h>

int
main(int argc, char **argv)
{
	int lpc = 0;

	qb_log_init("qb_blackbox", LOG_USER, LOG_TRACE);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_TRACE);

	for(lpc = 1; lpc < argc && argv[lpc] != NULL; lpc++) {
		printf("Dumping the contents of %s\n", argv[lpc]);
		qb_log_blackbox_print_from_file(argv[lpc]);
	}
	return 0;
}
