/*  This file is part of "testtool"
 *  Copyright (C) 2006 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>

/* return if there is some error (oposed to a failes check) */
#define TESTTOOL_ERROR_EXIT 2

bool silent = false;
bool echo = false;

static void usage(int code) __attribute__ ((noreturn));
static void usage(int code) {
	printf("%s: run a command and check its output\n", 
		program_invocation_short_name);
	printf("Syntax: %s [options] [--] program [program options]\n", 
		program_invocation_name);
	puts(" Possible options are:");
	puts("	--version: print version and exit");
	puts("	--help: print this screen and exit");
	puts("	--silent: only print errors or unexpected events");
	puts("	--echo: echo commands before executing them");
	exit(code);
}

static const struct option longopts[] = {
	{"help",		no_argument,		NULL,	'h'},
	{"version",		no_argument,		NULL,	'v'},
	{"silent",		no_argument,		NULL,	's'},
	{"echo",		no_argument,		NULL,	'e'},
	{NULL,			0,			NULL,	0}
};

int main(int argc, char *argv[]) {
	int c;

	if( argc <= 1 )
		usage(TESTTOOL_ERROR_EXIT);

	opterr = 0;
	while( (c = getopt_long(argc, argv, "+hvse", longopts, NULL)) != -1 ) {
		switch( c ) {
			case '?':
				fprintf(stderr, 
					"%s: Unexpected option '%c'!\n",
					program_invocation_short_name, optopt);
				exit(TESTTOOL_ERROR_EXIT);
			case 'h':
				usage(EXIT_SUCCESS);
			case 'v':
				printf("%s version %s\n", PACKAGE, VERSION);
				exit(EXIT_SUCCESS);
			case 's':
				silent = true;
				break;
			case 'e':
				echo = true;
				break;
			default:
				fprintf(stderr, 
					"%s: Unexpected getopt_long return '%c'!\n",
					program_invocation_short_name, c);
				exit(TESTTOOL_ERROR_EXIT);
		}
	}

	if( optind >= argc ) {
		fprintf(stderr, "%s: no program to start specified!\n",
				program_invocation_short_name);
		exit(TESTTOOL_ERROR_EXIT);
	}

	if( echo ) {
		int i;
		for( i = optind; i < argc; i++,putchar(' ') ) {
			putchar('\'');
			fputs(argv[i], stdout);
			putchar('\'');
		}
		putchar('\n');
	}

	// TODO....

	return EXIT_SUCCESS;
}
