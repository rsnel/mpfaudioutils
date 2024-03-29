/* verbose.h - some verbosity stuff
 *
 * Copyright (C) 2019  Rik Snel <rik@snel.it>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef INCLUDE_MPF_VERBOSE_H
#define INCLUDE_MPF_VERBOSE_H 1

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>

/* these macros are for textual user readable output, the first argument
 * to each macro must be a double quoted string, so: VERBOSE(msg); is wrong
 * and VERBOSE("%s", msg); is correct (and also good practise). */
#define WHINE(a,...) do { \
	fprintf(stderr, "%s:" a "\n", exec_name, ## __VA_ARGS__); \
} while(0)
extern char *exec_name;
extern char *cb_prefix;
extern int debug;
extern int verbose;
#define WARNING(a,...) WHINE("warning:" a, ## __VA_ARGS__)
#define ERROR(a,...) WHINE("error:" a, ## __VA_ARGS__)
#define DEBUG(a,...) do { \
	if (debug) WHINE("debug:" a, ## __VA_ARGS__); } while (0)
#define VERBOSE(a,...) do { if (verbose) WHINE(a, ## __VA_ARGS__); } while (0)
#define ABORT(msg,...)	do { \
	WHINE(msg, ## __VA_ARGS__); \
	abort(); \
} while (0)
#define BUG(msg,...)            ABORT("fatal:BUG:" msg, ## __VA_ARGS__)
#define FATAL(msg,...)            ABORT("fatal:" msg, ## __VA_ARGS__)
void verbose_init(char*);

#endif /* INCLUDE_MPF_VERBOSE_H */
