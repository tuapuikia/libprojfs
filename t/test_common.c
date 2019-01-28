/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the NOTICE file distributed with this library for additional
   information regarding copyright ownership.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library, in the file COPYING; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE		// for basename() in <string.h>
				// and getopt_long() in <getopt.h>

#include "../include/config.h"

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_common.h"

#include "../include/projfs_notify.h"

#define MOUNT_ARGS_USAGE "<lower-path> <mount-path>"

#define MAX_RETVAL_NAME_LEN 40

#define retval_entry(s) #s, -s

struct retval {
	const char *name;
	int val;
};

// list based on VFS API convert_result_to_errno()
static const struct retval errno_retvals[] = {
	{ "null",	0		},
	{ "allow", 	PROJFS_ALLOW	},
	{ "deny",	PROJFS_DENY	},
	{ retval_entry(EBADF)		},
	{ retval_entry(EINPROGRESS)	},
	{ retval_entry(EINVAL)		},
	{ retval_entry(EIO)		},
	{ retval_entry(ENODEV)		},
	{ retval_entry(ENOENT)		},
	{ retval_entry(ENOMEM)		},
	{ retval_entry(ENOTSUP)		},
	{ retval_entry(EPERM)		},
	{ retval_entry(ENOSYS)		},
	{ NULL,		0		}
};

#define VFSAPI_PREFIX "PrjFS_Result_"
#define VFSAPI_PREFIX_LEN (sizeof(VFSAPI_PREFIX) - 1)

#ifdef PROJFS_VFSAPI
#define get_retvals(v) ((v) ? vfsapi_retvals : errno_retvals)

#define retval_vfsapi_entry(s) #s, s

// list based on VFS API convert_result_to_errno()
static const struct retval vfsapi_retvals[] = {
	{ "null",	PrjFS_Result_Invalid			},
	{ "allow",	PrjFS_Result_Success			},
	{ "deny",	PrjFS_Result_EAccessDenied		},
	{ retval_vfsapi_entry(PrjFS_Result_Invalid)		},
	{ retval_vfsapi_entry(PrjFS_Result_Success)		},
	{ retval_vfsapi_entry(PrjFS_Result_Pending)		},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidArgs)	},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidOperation)	},
	{ retval_vfsapi_entry(PrjFS_Result_ENotSupported)	},
	{ retval_vfsapi_entry(PrjFS_Result_EDriverNotLoaded)	},
	{ retval_vfsapi_entry(PrjFS_Result_EOutOfMemory)	},
	{ retval_vfsapi_entry(PrjFS_Result_EFileNotFound)	},
	{ retval_vfsapi_entry(PrjFS_Result_EPathNotFound)	},
	{ retval_vfsapi_entry(PrjFS_Result_EAccessDenied)	},
	{ retval_vfsapi_entry(PrjFS_Result_EInvalidHandle)	},
	{ retval_vfsapi_entry(PrjFS_Result_EIOError)		},
	{ retval_vfsapi_entry(PrjFS_Result_ENotYetImplemented)	},
	{ NULL,		0					}
};
#else /* !PROJFS_VFSAPI */
#define get_retvals(v) errno_retvals
#endif /* !PROJFS_VFSAPI */

static const struct option all_long_opts[] = {
	{ "help", no_argument, NULL, TEST_OPT_NUM_HELP },
	{ "retval", required_argument, NULL, TEST_OPT_NUM_RETVAL}
};

struct opt_usage {
	const char *usage;
	int req;
};

static const struct opt_usage all_opts_usage[] = {
	{ NULL, 1 },
	{ "allow|deny|null|<error>", 1 }
};

/* option values */
static int retval;

static unsigned int opt_set_flags = TEST_OPT_NONE;

static const char *get_program_name(const char *program)
{
	const char *basename;

	basename = strrchr(program, '/');
	if (basename != NULL)
		program = basename + 1;

	// remove libtool script prefix, if any
	if (strncmp(program, "lt-", 3) == 0)
		program += 3;

	return program;
}

static void exit_usage(int err, const char *argv0, struct option *long_opts,
		       const char *args_usage)
{
	FILE *file = err ? stderr : stdout;

	fprintf(file, "Usage: %s", get_program_name(argv0));

	while (long_opts->name != NULL) {
		const struct opt_usage *opt_usage;

		opt_usage = &all_opts_usage[long_opts->val];

		fprintf(file, " %s--%s%s%s%s",
			(opt_usage->req ? "[" : ""),
			long_opts->name,
			(opt_usage->usage == NULL ? "" : " "),
			(opt_usage->usage == NULL ? "" : opt_usage->usage),
			(opt_usage->req ? "]" : ""));

		++long_opts;
	}

	fprintf(file, "%s%s\n",
		(*args_usage == '\0' ? "" : " "),
		args_usage);

	exit(err ? EXIT_FAILURE : EXIT_SUCCESS);
}

void test_exit_error(const char *argv0, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", get_program_name(argv0));

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

long int test_parse_long(const char *arg, int base)
{
	long int val;
	char *end;

	errno = 0;
	val = strtol(arg, &end, base);
	if (errno > 0 || end == arg || *end != '\0') {
		errno = EINVAL;
		val = 0;
	}

	return val;
}

int test_parse_retsym(int vfsapi, const char *retsym, int *retval)
{
	const struct retval *retvals = get_retvals(vfsapi);
	int ret = -1;
	int i = 0;

	while (retvals[i].name != NULL) {
		const char *name = retvals[i].name;

		if (!strcasecmp(name, retsym) ||
		    (vfsapi &&
		     !strncmp(name, VFSAPI_PREFIX, VFSAPI_PREFIX_LEN) &&
		     !strcasecmp(name + VFSAPI_PREFIX_LEN, retsym))) {
			ret = 0;
			*retval = retvals[i].val;
			break;
		}

		++i;
	}

	return ret;
}

static struct option *get_long_opts(unsigned int opt_flags)
{
	struct option *long_opts;
	unsigned int tmp_flags = opt_flags;
	size_t num_opts = 0;
	int opt_idx = 0;
	int opt_num = 0;

	// slow counting, but obvious, and only needs to execute once
	while (tmp_flags > 0) {
		if ((tmp_flags & 1) == 1)
			++num_opts;
		tmp_flags >>= 1;
	}

	long_opts = calloc(num_opts + 1, sizeof(struct option));
	if (long_opts == NULL) {
		fprintf(stderr, "unable to get options array: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (opt_flags > 0) {
		unsigned int opt_flag = (0x0001 << opt_num);

		if ((opt_flags & opt_flag) > 0)
			memcpy(&long_opts[opt_idx++], &all_long_opts[opt_num],
			       sizeof(struct option));

		opt_flags &= ~opt_flag;
		++opt_num;
	}

	return long_opts;
}

void test_parse_opts(int argc, char *const argv[], unsigned int opt_flags,
		     int min_args, int max_args, char *args[],
		     const char *args_usage)
{
	int vfsapi = (opt_flags & TEST_OPT_VFSAPI) ? 1 : 0;
	struct option *long_opts;
	int num_args;
	int arg_idx = 0;
	int err = 0;
	int val;

	opt_flags |= TEST_OPT_HELP;
	opt_flags &= ~TEST_OPT_VFSAPI;		// exclude VFS API option

	long_opts = get_long_opts(opt_flags);

	opterr = 0;
	do {
		val = getopt_long(argc, argv, "h", long_opts, NULL);
		if (val < 0)
			break;

		switch (val) {
		case 'h':
		case TEST_OPT_NUM_HELP:
			exit_usage(0, argv[0], long_opts, args_usage);

		case TEST_OPT_NUM_RETVAL:
			if (test_parse_retsym(vfsapi, optarg, &retval) < 0)
				test_exit_error(argv[0], "invalid retval: %s",
						optarg);
			else
				opt_set_flags |= TEST_OPT_RETVAL;
			break;

		case '?':
			if (optopt > 0)
				test_exit_error(argv[0], "invalid option: -%c",
						optopt);
			else
				test_exit_error(argv[0], "invalid option: %s",
						argv[optind - 1]);

		default:
			test_exit_error(argv[0], "unknown getopt code: %d",
					val);
		}
	}
	while (!err);

	num_args = argc - optind;
	if (err || num_args < min_args || num_args > max_args)
		exit_usage(1, argv[0], long_opts, args_usage);

	while (optind < argc)
		args[arg_idx++] = argv[optind++];

	while (num_args++ < max_args)
		args[arg_idx++] = NULL;
}

void test_parse_mount_opts(int argc, char *const argv[],
			   unsigned int opt_flags,
			   const char **lower_path, const char **mount_path)
{
	char *args[2];

	test_parse_opts(argc, argv, opt_flags, 2, 2, args, MOUNT_ARGS_USAGE);

	*lower_path = args[0];
	*mount_path = args[1];
}

unsigned int test_get_opts(unsigned int opt_flags, ...)
{
	unsigned int opt_flag = TEST_OPT_HELP;
	unsigned int ret_flags = TEST_OPT_NONE;
	va_list ap;

	opt_flags &= ~TEST_OPT_VFSAPI;		// exclude VFS API option

	va_start(ap, opt_flags);

	while (opt_flags != TEST_OPT_NONE) {
		unsigned int ret_flag;
		int *i;

		opt_flag <<= 1;
		if ((opt_flags & opt_flag) == TEST_OPT_NONE)
			continue;
		opt_flags &= ~opt_flag;

		ret_flag = opt_set_flags & opt_flag;
		ret_flags |= ret_flag;

		switch (opt_flag) {
			case TEST_OPT_RETVAL:
				i = va_arg(ap, int*);
				if (ret_flag != TEST_OPT_NONE)
					*i = retval;
				break;

			default:
				err(EXIT_FAILURE,
				    "unknown option flag: %u", opt_flag);
		}
	}

	va_end(ap);

	return ret_flags;
}

struct projfs *test_start_mount(const char *lowerdir, const char *mountdir,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data)
{
	struct projfs *fs;

	fs = projfs_new(lowerdir, mountdir, handlers, handlers_size,
			user_data);

	if (fs == NULL)
		err(EXIT_FAILURE, "unable to create filesystem");

	if (projfs_start(fs) < 0)
		err(EXIT_FAILURE, "unable to start filesystem");

	return fs;
}

void *test_stop_mount(struct projfs *fs)
{
	return projfs_stop(fs);
}

#ifdef PROJFS_VFSAPI
void test_start_vfsapi_mount(const char *storageRootFullPath,
			     const char *virtualizationRootFullPath,
			     PrjFS_Callbacks callbacks,
			     unsigned int poolThreadCount,
			     PrjFS_MountHandle** mountHandle)
{
	PrjFS_Result ret;

	ret = PrjFS_StartVirtualizationInstance(storageRootFullPath,
						virtualizationRootFullPath,
						callbacks, poolThreadCount,
						mountHandle);

	if (ret != PrjFS_Result_Success)
		err(EXIT_FAILURE, "unable to start filesystem: %d", ret);
}

void test_stop_vfsapi_mount(PrjFS_MountHandle* mountHandle)
{
	PrjFS_StopVirtualizationInstance(mountHandle);
}
#endif /* PROJFS_VFSAPI */

static void signal_handler(int sig)
{
	(void) sig;
}

void test_wait_signal(void)
{
	int tty = isatty(STDIN_FILENO);

	if (tty < 0)
		warn("unable to check stdin");
	else if (tty) {
		printf("hit Enter to stop: ");
		getchar();
	}
	else {
		struct sigaction sa;

		memset(&sa, 0, sizeof(struct sigaction));
		sa.sa_handler = signal_handler;
		sigemptyset(&(sa.sa_mask));
		sa.sa_flags = 0;

		/* replace libfuse's handler so we can exit tests cleanly */
		if (sigaction(SIGTERM, &sa, 0) < 0)
			warn("unable to set signal handler");
		else
			pause();
	}
}

