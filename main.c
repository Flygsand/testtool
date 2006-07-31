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

#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* return if there is some error (oposed to a failes check) */
#define TESTTOOL_ERROR_EXIT 2

bool silent = false;
bool echo = false;
bool use_debugger = false;
char *debugger = NULL;
unsigned char expected_returncode = 0;

static void usage(int code) __attribute__ ((noreturn));
static void usage(int code) {
	printf("%s: run a command and check its output\n",
		program_invocation_short_name);
	printf("Syntax: %s [options]"
			" [--debugger=debugger [debugger options]]"
			" [--] program [program options] 3<rules-file\n",
		program_invocation_name);
	puts(" Possible options are:");
	puts("	--version: print version and exit");
	puts("	--help: print this screen and exit");
	puts("	--silent: only print errors or unexpected events");
	puts("	--echo: echo commands before executing them");
	puts("	--debugger: debugger (and its options) start the program in");
	exit(code);
}

static const char **createarguments(int *count, char **args, int argc) {
	const char **results, **p;
	int argumentcount;
	int i;

	argumentcount = argc+1;
	if( use_debugger ) {
		if( debugger == NULL )
			argumentcount += 2;
		else
			argumentcount++;
	}
	*count = argumentcount;
	results = malloc(sizeof(char*)*argumentcount);
	p = results;
	if( use_debugger ) {
		if( debugger == NULL ) {
			assert(argumentcount > 1);
			*(p++) = "valgrind";
			*(p++) = "--log-fd=5";
			argumentcount -= 2;
		} else {
			assert(argumentcount > 0);
			*(p++) = debugger;
			argumentcount --;
		}
	}
	for( i = 0; i < argc; i++ ) {
		assert(argumentcount > 0);
		*(p++) = args[i];
		argumentcount --;
	}
	assert(argumentcount == 1);
	*p = NULL;
	return results;
}

static int start(const char **arguments) {
	pid_t child,w;
	int status = EXIT_SUCCESS;

	child = fork();
	if( child == 0 ) {
		execvp(arguments[0],(char**)arguments);
		perror("TESTTOOL: error starting program: ");
		raise(SIGUSR2);
		exit(EXIT_FAILURE);
	}
	if( child < 0 ) {
		fprintf(stderr, "%s: error forking: %s\n",
				program_invocation_short_name,
				strerror(errno));
		return TESTTOOL_ERROR_EXIT;
	}
	w = waitpid(child, &status, 0);
	if( WIFEXITED(status) ) {
		if( WEXITSTATUS(status) != expected_returncode ) {
			fprintf(stderr, "%s: got returncode %d"
					" instead of expected %d\n",
					program_invocation_short_name,
					WEXITSTATUS(status),
					expected_returncode);
			return EXIT_FAILURE;
		} else {
			return status;
		}
	} else if( WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR2) {
		fprintf(stderr, "%s: Could not start %s\n",
				program_invocation_short_name,
				arguments[0]);

		return TESTTOOL_ERROR_EXIT;
	} else {
		fprintf(stderr, "%s: Abnormal termination of %s\n",
				program_invocation_short_name,
				arguments[0]);
		return EXIT_FAILURE;
	}
}

static const struct option longopts[] = {
	{"debugger",		optional_argument,	NULL,	'd'},
	{"help",		no_argument,		NULL,	'h'},
	{"version",		no_argument,		NULL,	'v'},
	{"silent",		no_argument,		NULL,	's'},
	{"echo",		no_argument,		NULL,	'e'},
	{NULL,			0,			NULL,	0}
};

int main(int argc, char *argv[]) {
	int c;
	const char **arguments;
	int argumentcount;
	int status;

	if( argc <= 1 )
		usage(TESTTOOL_ERROR_EXIT);

	opterr = 0;
	while( (c = getopt_long(argc, argv, "+hvsed:", longopts, NULL)) != -1 ) {
		if( c == 'd' ) {
			use_debugger = true;
			if( optarg != NULL ) {
				debugger = strdup(optarg);
				if( debugger == NULL ) {
					fputs("Out of memory!\n", stderr);
					exit(TESTTOOL_ERROR_EXIT);
				}
			} else
				debugger = NULL;
			break;
		}
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

	arguments = createarguments(&argumentcount, argv+optind, argc-optind);

	if( echo ) {
		int i;
		for( i = 0; i+1 < argumentcount; i++,putchar(' ') ) {
			putchar('\'');
			assert(arguments[i] != NULL);
			fputs(arguments[i], stdout);
			putchar('\'');
		}
		putchar('\n');
	}

	status = start(arguments);

	free(arguments);
	free(debugger);
	return status;
}
