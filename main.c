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

static bool silent = false;
static bool echo = false;
static bool annotate = false;
static bool use_debugger = false;
static bool readrules = false;
static char *debugger = NULL;
static char *outfile = NULL;
static int outfile_fd = -1;
static unsigned char expected_returncode = 0;
static int command_fd = -1;

static void usage(int code) __attribute__ ((noreturn));
static void usage(int code) {
	printf("%s: run a command and check its output\n",
		program_invocation_short_name);
	printf("Syntax: %s [options]"
			" [--debugger=debugger [debugger options]]"
			" [--] program [program options]\n",
		program_invocation_name);
	printf("or: %s --rules [options]"
			" [--debugger=debugger [debugger options]]"
			" [--] program [program options] 3<rules-file\n",
		program_invocation_name);
	puts(" Possible options are:");
	puts("	--version: print version and exit");
	puts("	--help: print this screen and exit");
	puts("	--silent: only print errors or unexpected events");
	puts("	--echo: echo commands before executing them");
	puts("	--annotate: annotate lines (to debug rules)");
	puts("	--rules: read rules (default from fd 3)");
	puts("	--debugger: debugger (and its options) start the program in");
	puts("	--outfile: file to save stdoutput into");
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

static bool readcontroldata(int fd) {
	static char buffer[1000];
	static size_t len = 0;
	ssize_t got;

	got = read(fd, buffer+len, 1000-len);
	if( got <= 0 ) { /* End of file */
		return true;
	}
	write(2, buffer, got);
	return false;
}

struct linecheck {
	struct linecheck *next;
	char *line;
	size_t found;
	size_t len;
};

struct expectdata {
	bool ignoreunknown;
	struct linecheck *ignore;
	struct linecheck *expect;
	size_t overlong, unexpected, malformed;
	char buffer[1000];
	size_t len;
	bool overrun;
} errorexpect = { false, NULL, NULL, 0, 0, 0, "", 0, false},
  outexpect = { true, NULL, NULL, 0, 0, 0, "", 0, false};

static void checkline(char *line, size_t len, struct expectdata *expect, int outfd) {
	bool print = false;;
	struct linecheck *p;
	size_t efflen = len;
	if( len > 0 && line[len-1] == '\n' )
		efflen--;
	for( p = expect->expect; p != NULL ; p = p->next ) {
		if( efflen == p->len && strncmp(p->line, line, efflen) == 0 ) {
			p->found++;
			break;
		}
	}
	if( p != NULL ) {
		if( annotate && !silent )
			dprintf(outfd, "EXPECTED(%d):", outfd);
	} else {
		for( p = expect->ignore; p != NULL ; p = p->next ) {
			if( efflen == p->len && 
					strncmp(p->line, line, efflen) == 0 ) {
				p->found++;
				break;
			}
		}
		if( p != NULL ) {
			if( annotate && !silent )
				dprintf(outfd, "IGNORED(%d):", outfd);
		} else if( expect->ignoreunknown ) {
			if( annotate && !silent )
				dprintf(outfd, "NORMAL(%d):", outfd);
		} else {
			expect->unexpected += 1;
			print = true;
			if( annotate )
				dprintf(outfd, "UNEXPECTED(%d):", outfd);
		}
	}

	if( outfd == 1 && outfile_fd >= 0 ) {
		ssize_t written = write(outfile_fd, line, len);
		if( written != len ) {
			fprintf(stderr,"%s: Error writing to %s: %s\n",
				program_invocation_short_name,
				outfile, strerror(errno));
			exit(TESTTOOL_ERROR_EXIT);
		}
	}

	if( print || !silent ) {
		write(outfd, line, len);
		if( line[len-1] != '\n' ) {
			dprintf(outfd, "[UNTERMINATED/OVERLONG]\n");
		}
	} else {
		if( line[len-1] != '\n' ) {
			dprintf(outfd, "UNTERMINATED/OVERLONG LINE(%d)\n",
					outfd);
		}
	}
}

static bool readlinedata(int fd, struct expectdata *expect, int outfd) {
	ssize_t got;
	int i, linestart;

	got = read(fd, expect->buffer+expect->len, sizeof(expect->buffer)-expect->len);
	if( got == 0 ) { /* End of file */
		if( expect->len > 0 ) {
			expect->malformed++;
			checkline(expect->buffer, expect->len, expect, outfd);
		}
		return true;
	}
	if( got < 0 ) {
		fprintf(stderr, "%s: Error reading data: %s\n",
				program_invocation_short_name,
				strerror(errno));
		return true;
	}
	linestart = 0;
	for( i = expect->len ; i < expect->len+got ; i++ ) {
		if( expect->buffer[i] == '\n' ) {
			if( ! expect->overrun )
				checkline(expect->buffer+linestart, i-linestart+1,
						expect, outfd);
			expect->overrun = false;
			linestart = i+1;
		}
		if( expect->buffer[i] == '\0' ) {
			expect->malformed++;
			expect->buffer[i] = '0';
		}
	}
	expect->len += got;
	if( linestart == 0 && expect->len == sizeof(expect->buffer) ) {
		expect->overrun = true;
		expect->overlong++;
		checkline(expect->buffer, expect->len, expect, outfd);
		expect->len = 0;
	} else if( linestart == expect->len )
		expect->len = 0;
	else {
		expect->len -= linestart;
		memmove(expect->buffer, expect->buffer+linestart, expect->len);
	}

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
	struct linecheck *p;

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
		cfds[1] = open("/dev/null", O_NOCTTY|O_APPEND|O_RDONLY);
		if( cfds[1] < 0 ) {
			fprintf(stderr, "%s: error opening /dev/null: %s\n",
					program_invocation_short_name,
					strerror(errno));
			return TESTTOOL_ERROR_EXIT;
		}
	}

	child = fork();
	if( child == 0 ) {
		/*if( outfile_fd >= 0 )
			close(outfile_fd);*/
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
				if( readcontroldata(cfds[0]) ) {
					close(cfds[0]);
					cfds[0] = -1;
				}

			}
			if( efds[0] > 0 && FD_ISSET(efds[0],&readfds) ) {
				if( readlinedata(efds[0], &errorexpect, 2) ) {
					close(efds[0]);
					efds[0] = -1;
				}
			}
			if( ofds[0] > 0 && FD_ISSET(ofds[0],&readfds) ) {
				if( readlinedata(ofds[0], &outexpect, 1) ) {
					close(ofds[0]);
					ofds[0] = -1;
				}
			}
		}
	}
	if( outexpect.unexpected > 0 || errorexpect.unexpected > 0 ) {
		fprintf(stderr,
			"%s: %lu unexpected lines in stdout, %lu in stderr\n",
			program_invocation_short_name,
			(unsigned long)outexpect.unexpected,
			(unsigned long)errorexpect.unexpected);
		result = EXIT_FAILURE;
	}
	if( outexpect.overlong > 0 || errorexpect.overlong > 0 ) {
		fprintf(stderr,
			"%s: %lu overlong lines in stdout, %lu in stderr\n",
			program_invocation_short_name,
			(unsigned long)outexpect.overlong,
			(unsigned long)errorexpect.overlong);
		result = EXIT_FAILURE;
	}
	if( outexpect.malformed > 0 || errorexpect.malformed > 0 ) {
		fprintf(stderr,
			"%s: %lu text-violations in stdout, %lu in stderr\n",
			program_invocation_short_name,
			(unsigned long)outexpect.malformed,
			(unsigned long)errorexpect.malformed);
		result = EXIT_FAILURE;
	}
	for( p = errorexpect.expect; p != NULL ; p = p->next ) {
		if( p->found <= 0 ) {
			fprintf(stderr, "%s: missed expected line(2): %s\n",
				program_invocation_short_name, p->line);
			result = EXIT_FAILURE;
		}
	}
	for( p = outexpect.expect; p != NULL ; p = p->next ) {
		if( p->found <= 0 ) {
			fprintf(stderr, "%s: missed expected line(1): %s\n",
				program_invocation_short_name, p->line);
			result = EXIT_FAILURE;
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

static enum {
	AT_stderr,
	AT_stdout,
} addto = AT_stderr;

static bool readruleline(const char *buffer, size_t len) {
	struct linecheck *n;
	struct linecheck **next;
	char *e;

	if( len <= 0 || buffer[0] == '#' )
		return true;

	if( addto == AT_stdout )
		next = &outexpect.ignore;
	else
		next = &errorexpect.ignore;
	switch( buffer[0] ) {
		case 'r':
			buffer++;len--;
			if( buffer[0] == 'e' && len > 0) {
				buffer++;len--;
				if( buffer[0] == 't' && len > 0) {
					buffer++;len--;
					if( buffer[0] == 'u' && len > 0) {
						buffer++;len--;
						if( buffer[0] == 'r' && len > 0) {
							buffer++;len--;
							if( buffer[0] == 'n' && len > 0) {
								buffer++;len--;
								if( buffer[0] == 's' && 
									len > 0) {
									buffer++;len--;
								}
							}
						}
					}
				}
			}
			if( buffer[0] == ' ' && len > 0 ) {
				buffer++;len--;
			}
			expected_returncode = strtol(buffer, &e, 0);
			while( *e == ' ' || *e == '\t' ) 
				e++;
			if( *e != '\0' ) {
				fputs("Unparsable returns rule\n",
						stderr);
				return false;
			}
			return true;
		case 's':
			if( len > 7 || (len == 7 && buffer[6] != '*')) {
				fputs("Too long rule starting with s\n",
						stderr);
				return false;
			}
			if( len >= 6 ) {
				if( strncmp(buffer, "stderr", 6) == 0 ) {
					addto = AT_stderr;
					errorexpect.ignoreunknown = len == 7;
					return true;
				} else if( strncmp(buffer, "stdout", 6) == 0 ) {
					addto = AT_stdout;
					outexpect.ignoreunknown = len == 7;
					return true;
				}
			}
			fputs("Unparseable s-rule\n", stderr);
			return false;
		case '*':
			buffer++;len--;
			if( addto == AT_stdout )
				next = &outexpect.expect;
			else
				next = &errorexpect.expect;
			assert(buffer[0] == '=');
		case '=':
			buffer++;len--;
			n = calloc(1,sizeof(struct linecheck));
			if( n == NULL )
				return false;
			n->line = strndup(buffer, len);
			assert( n->line != NULL);
			n->len = len;
			n->next = *next;
			*next = n;

			return true;
	}
	fputs("Unknown rule\n", stderr);
	return false;
}

static bool read_rules(void) {
	char buffer[2000];
	size_t len = 0;
	ssize_t got;
	int fd = (command_fd < 0)?3:command_fd;
	int linestart = 0;
	int i;

	while( (got = read(fd, buffer+len, sizeof(buffer)-len)) > 0) {

		for( i = len ; i < len+got ; i++ ) {
			if( buffer[i] == '\n' || buffer[i] == '\0' ) {
				buffer[i] = '\0';
				if( !readruleline(buffer+linestart,i-linestart))
					return false;
				linestart = i+1;
			}
		}
		len += got;
		if( linestart > 0 ) {
			len -= linestart;
			if( len > 0 )
				memmove(buffer, buffer+linestart, len);
		}
	}
	if( got < 0 ) {
		fprintf(stderr,
			"Error reading rules from file-descriptor %d: %s\n",
			fd, strerror(errno));
		return false;
	} else if( len > 0 ) {
		fprintf(stderr,
			"Unterminated line at end of rules\n");
		return false;
	}
	return true;
}

static const struct option longopts[] = {
	{"debugger",		optional_argument,	NULL,	'd'},
	{"help",		no_argument,		NULL,	'h'},
	{"version",		no_argument,		NULL,	'v'},
	{"silent",		no_argument,		NULL,	's'},
	{"echo",		no_argument,		NULL,	'e'},
	{"annotate",		no_argument,		NULL,	'a'},
	{"rules",		no_argument,		NULL,	'r'},
	{"outfile",		required_argument,	NULL,	'o'},
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
	while( (c = getopt_long(argc, argv, "+hvsearo:d::", longopts, NULL)) != -1 ) {
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
			case 'a':
				annotate = true;
				break;
			case 'r':
				readrules = true;
				break;
			case 'o':
				free(outfile);
				outfile = strdup(optarg);
				if( outfile == NULL ) {
					fputs("Out of memory!\n", stderr);
					exit(TESTTOOL_ERROR_EXIT);
				}
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
		free(debugger);
		free(outfile);
		exit(TESTTOOL_ERROR_EXIT);
	}

	if( readrules ) {
		if( !read_rules() ) {
			free(debugger);
			free(outfile);
			exit(TESTTOOL_ERROR_EXIT);
		}
	}

	if( outfile != NULL ) {
		outfile_fd = open(outfile, O_CREAT|O_TRUNC|O_NOFOLLOW|O_WRONLY, 0666);
		if( outfile_fd < 0 ) {
			fprintf(stderr,"%s: Error opening file %s: %s\n",
					program_invocation_short_name,
					outfile, strerror(errno));
			free(debugger);
			free(outfile);
			exit(TESTTOOL_ERROR_EXIT);
		}
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

	if( outfile_fd >= 0 )
		close(outfile_fd);

	free(arguments);
	free(debugger);
	free(outfile);
	return status;
}
