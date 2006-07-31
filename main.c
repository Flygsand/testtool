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
#include <sys/stat.h>
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
#include <fcntl.h>

/* return if there is some error (oposed to a failes check) */
#define TESTTOOL_ERROR_EXIT 2

bool silent = false;
bool echo = false;
bool use_debugger = false;
char *debugger = NULL;
unsigned char expected_returncode = 0;
int command_fd = -1;

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
			*(p++) = "--log-fd=3";
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

static bool readcontroldata(int fd, int *status) {
	static char buffer[1000];
	static size_t len = 0;
	static bool overrun = false;
	ssize_t got;

	got = read(fd, buffer+len, 1000-len);
	if( got == 0 ) { /* End of file */
		return true;
	}
	write(2, buffer, got);
	return false;
}
static bool readerrordata(int fd, int *status) {
	static char buffer[1000];
	static size_t len = 0;
	static bool overrun = false;
	ssize_t got;

	got = read(fd, buffer+len, 1000-len);
	if( got == 0 ) { /* End of file */
		return true;
	}
	*status = EXIT_FAILURE;
	write(2, buffer, got);
	return false;
}
static bool readoutdata(int fd, int *status) {
	static char buffer[1000];
	static size_t len = 0;
	static bool overrun = false;
	ssize_t got;

	got = read(fd, buffer+len, 1000-len);
	if( got == 0 ) { /* End of file */
		return true;
	}
	write(2, buffer, got);
	return false;
}

static int start(const char **arguments) {
	pid_t child,w;
	int status;
	int result = EXIT_SUCCESS;
	int ofds[2];
	int efds[2];
	int cfds[2] = {-1, -1};
	int e;

	if( pipe(ofds) != 0 ) {
		fprintf(stderr, "%s: error creating pipe: %s\n",
				program_invocation_short_name,
				strerror(errno));
		return TESTTOOL_ERROR_EXIT;
	}
	if( pipe(efds) != 0 ) {
		fprintf(stderr, "%s: error creating pipe: %s\n",
				program_invocation_short_name,
				strerror(errno));
		return TESTTOOL_ERROR_EXIT;
	}
	if( use_debugger && (debugger == NULL || command_fd >= 0) ) {
		if( pipe(cfds) != 0 ) {
			fprintf(stderr, "%s: error creating pipe: %s\n",
					program_invocation_short_name,
					strerror(errno));
			return TESTTOOL_ERROR_EXIT;
		}
	} else {
		cfds[1] = open("/dev/null", O_NOCTTY|O_APPEND);
		if( cfds[1] < 0 ) {
			fprintf(stderr, "%s: error opening /dev/null: %s\n",
					program_invocation_short_name,
					strerror(errno));
			return TESTTOOL_ERROR_EXIT;
		}
	}

	child = fork();
	if( child == 0 ) {
		if( cfds[0] > 0 )
			close(cfds[0]);
		close(ofds[0]);
		close(efds[0]);
		if( ofds[1] >= 0 && ofds[1] != 1 ) {
			if( dup2(ofds[1],1) == -1 ) {
				perror("TESTTOOL: error dup'ing pipe: ");
				raise(SIGUSR2);
				exit(EXIT_FAILURE);
			}
			close(ofds[1]);
		}
		if( efds[1] >= 0 && efds[1] != 2 ) {
			if( dup2(efds[1],2) == -1 ) {
				perror("TESTTOOL: error dup'ing pipe: ");
				raise(SIGUSR2);
				exit(EXIT_FAILURE);
			}
			close(efds[1]);
		}
		if( command_fd < 0 )
			command_fd = 3;
		if( cfds[1] >= 0 && cfds[1] != command_fd ) {
			if( dup2(cfds[1],command_fd) == -1 ) {
				perror("TESTTOOL: error dup'ing pipe: ");
				raise(SIGUSR2);
				exit(EXIT_FAILURE);
			}
			close(cfds[1]);
		}
		execvp(arguments[0],(char**)arguments);
		perror("TESTTOOL: error starting program: ");
		raise(SIGUSR2);
		exit(EXIT_FAILURE);
	}
	e = errno;
	close(cfds[1]);
	close(efds[1]);
	close(ofds[1]);
	if( child < 0 ) {
		fprintf(stderr, "%s: error forking: %s\n",
				program_invocation_short_name,
				strerror(e));
		if( cfds[0] > 0 )
			close(cfds[0]);
		close(efds[0]);
		close(ofds[0]);
		return TESTTOOL_ERROR_EXIT;
	}
	/* read data */
	while( true ) {
		fd_set readfds;
		int max = cfds[0];
		FD_ZERO(&readfds);
		if( cfds[0] > 0 )
			FD_SET(cfds[0], &readfds);
		if( efds[0] > 0 )
			FD_SET(efds[0], &readfds);
		if( efds[0] > max )
			max = efds[0];
		if( ofds[0] > 0 )
			FD_SET(ofds[0], &readfds);
		if( ofds[0] > max )
			max = ofds[0];
		if( max == -1 )
			break;
		e = select(max+1, &readfds, NULL, NULL, NULL);
		if( e < 0 ) {
			e = errno;
			if( e != EINTR ) {
				if( cfds[0] > 0 )
					close(cfds[0]);
				if( efds[0] > 0 )
					close(efds[0]);
				if( ofds[0] > 0 )
					close(ofds[0]);
				fprintf(stderr, "%s: error waiting for output: %s\n",
						program_invocation_short_name,
						strerror(e));
				return TESTTOOL_ERROR_EXIT;
			}
		} else {
			if( cfds[0] > 0 && FD_ISSET(cfds[0],&readfds) ) {
				if( readcontroldata(cfds[0], &result) ) {
					close(cfds[0]);
					cfds[0] = -1;
				}

			}
			if( efds[0] > 0 && FD_ISSET(efds[0],&readfds) ) {
				if( readerrordata(efds[0], &result) ) {
					close(efds[0]);
					efds[0] = -1;
				}
			}
			if( ofds[0] > 0 && FD_ISSET(ofds[0],&readfds) ) {
				if( readoutdata(ofds[0], &result) ) {
					close(ofds[0]);
					ofds[0] = -1;
				}
			}
		}
	}
	if( cfds[0] > 0 )
		close(cfds[0]);
	if( efds[0] > 0 )
		close(efds[0]);
	if( ofds[0] > 0 )
		close(ofds[0]);
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
			return result;
		}
	} else if( WIFSIGNALED(status) && WTERMSIG(status) == SIGUSR2) {
		fprintf(stderr, "%s: Could not start %s\n",
				program_invocation_short_name,
				arguments[0]);

		return TESTTOOL_ERROR_EXIT;
	} else if( WIFSIGNALED(status) ) {
		fprintf(stderr, "%s: Program %s killed by signal %d\n",
				program_invocation_short_name,
				arguments[0], (int)(WTERMSIG(status)));

		return EXIT_FAILURE;
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
