/*
 * Copyright 2018 Ian Johnson
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/** The permissions to use when creating the plan directory. */
#define PLAN_DIR_PERMS 0755
/** The permissions to use when creating a plan file. */
#define PLAN_FILE_PERMS 0644
/** The program's version string. */
#define VERSION "0.1.0"

static char *progname = "reading";

/** SUBCOMMANDS */
/** Execute the given subcommand with the given arguments. */
static void subcommand(char command, int argc, char **argv);
/** Add the given file as a new plan with the given name (using file name if NULL). */
static void add(const char *pathname, const char *planname);
/** Delete the plan with the given name. */
static void delete(const char *plan);
/** Advance the given plan to the next entry. */
static void next(const char *plan);
/** Revert the given plan to the previous entry. */
static void previous(const char *plan);
/** Set the current entry of the given plan. */
static void set(const char *plan, int entry);
/** Show the next num entries of the given plan (or all plans if plan is NULL). */
static void show(const char *plan, int num);
/** Show a summary for the given plan (all plans if plan is NULL). */
static void summary(const char *plan);

/** HELPERS */
/** Appends s2 onto s1, where s1 must have been dynamically allocated. */
static char *append(char *s1, const char *s2);
/** Recursively creates the given directory. */
static int mkdir_recursive(const char *dirpath);
/**
 * Parse an integer from the given string (base 10).  If the given string does
 * not represent an integer because it is either out of range or contains
 * invalid characters, errno is set to ERANGE or EINVAL, respectively.  The
 * return value for an invalid input is undefined and should not be relied
 * upon.
 */
static int parseint(const char *str);
/**
 * Return the path to the plan directory, ensuring it exists (exiting if this
 * is impossible).  The returned string should be freed when it is no longer
 * needed.
 */
static char *plandir(void);
/** remalloc(3) that exits on failure. */
static void *xrealloc(void *ptr, size_t size);

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-dnpV] [-a file] [-s num] [-t entry] [plan]\n",
	    progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	if (argc < 1) 
		usage();
	else
		progname = basename(argv[0]);

	if (argc == 1) 
		summary(NULL);
	else if (*argv[1] == '-' && strlen(argv[1]) == 2) 
		subcommand(argv[1][1], argc - 2, argv + 2);
	else if (argc == 2) 
		summary(argv[1]);
	else 
		usage();

	return 0;
}

static void
subcommand(char command, int argc, char **argv)
{
	switch (command) {
	case 'a':
		if (argc == 1)
			add(argv[0], NULL);
		else if (argc == 2)
			add(argv[0], argv[1]);
		else
			usage();
		break;
	case 'd':
		if (argc == 1)
			delete(argv[0]);
		else
			usage();
		break;
	case 'n':
		if (argc == 1)
			next(argv[0]);
		else
			usage();
		break;
	case 'p':
		if (argc == 1)
			previous(argv[0]);
		else
			usage();
		break;
	case 's':
		if (argc == 2) {
			int num;
			errno = 0;
			num = parseint(argv[0]);
			if (errno != 0)
				err(1, "bad argument to '-s'");
			show(argv[1], num);
		} else {
			usage();
		}
		break;
	case 't':
		if (argc == 2) {
			int entry;
			errno = 0;
			entry = parseint(argv[0]);
			if (errno != 0)
				err(1, "bad argument to '-t'");
			show(argv[1], entry);
		} else {
			usage();
		}
		break;
	case 'V':
		printf("reading %s\n", VERSION);
		break;
	default:
		usage();
	}
}


static void
add(const char *pathname, const char *planname)
{
	FILE *original, *plan, *status;
	char *path;
	char buf[BUFSIZ];
	size_t got;

	if (!(original = fopen(pathname, "r")))
		err(1, "could not open input file '%s'", pathname);

	path = append(plandir(), "/");
	/* Either use the provided plan name or try to deduce one. */
	if (planname) {
		append(path, planname);
	} else {
		char *tmp = strdup(pathname);
		path = append(path, basename(tmp));
		free(tmp);
	}

	if (!(plan = fopen(path, "w")))
		err(1, "could not create plan file '%s'", path);
	/* Copy input file to a plan in the plans directory. */
	while ((got = fread(buf, 1, sizeof buf, original)) > 0)
		if (fwrite(buf, 1, got, plan) == 0)
			err(1, "could not write to plan file '%s'", path);
	if (ferror(original))
		err(1, "could not read from input file '%s'", pathname);

	fclose(plan);
	fclose(original);

	/* Create the corresponding status file. */
	path = append(path, ".status");
	if (!(status = fopen(path, "w")))
		err(1, "could not create plan status file '%s'", path);
	if (fprintf(status, "1") < 0)
		err(1, "could not write to plan status file '%s'", path);
	fclose(status);

	free(path);
}

static void
delete(const char *plan)
{
	char *path = append(append(plandir(), "/"), plan);
	if (unlink(path) < 0) {
		if (errno == ENOENT)
			errx(1, "plan '%s' does not exist", plan);
		else
			err(1, "could not remove plan file '%s'", path);
	}
	path = append(path, ".status");
	if (unlink(path) < 0)
		err(1, "could not remove plan status file '%s'", path);
}

static void
next(const char *plan)
{
}

static void
previous(const char *plan)
{
}

static void
set(const char *plan, int entry)
{
}

static void
show(const char *plan, int num)
{
}

static void
summary(const char *plan)
{
	printf("A summary\n");
}

static char *append(char *s1, const char *s2)
{
	size_t z1 = strlen(s1);
	size_t z2 = strlen(s2);
	s1 = xrealloc(s1, z1 + z2 + 1);

	memcpy(s1 + z1, s2, z2);
	s1[z1 + z2] = '\0';

	return s1;
}

static int mkdir_recursive(const char *dirpath)
{
	char *path = strdup(dirpath);
	char *tmp = path + 1; /* pointer to the next '/' in the path */
	int errno_old = errno;
	int retval = 0;

	while ((tmp = strchr(tmp, '/'))) {
		*tmp = '\0';
		errno = 0;
		mkdir(path, PLAN_DIR_PERMS);
		if (errno != 0 && errno != EEXIST) {
			retval = -1;
			goto EXIT;
		}
		*tmp++ = '/';
	}
	errno = 0;
	mkdir(path, PLAN_DIR_PERMS);
	if (errno != 0 && errno != EEXIST)
		retval = -1;

EXIT:
	/* It's not good for a function to set errno to 0. */
	if (errno == 0)
		errno = errno_old;
	free(tmp);
	return retval;
}

static int
parseint(const char *str)
{
	char *end;
	long val = strtol(str, &end, 10);

	if (*str == '\0' || *end != '\0') {
		errno = EINVAL;
		return 0;
	} else if (val < INT_MIN || val > INT_MAX) {
		errno = ERANGE;
		return 0;
	} else {
		return (int)val;
	}
}

static char *plandir(void)
{
	char *dirpath;

	/*
	 * The following locations are tried, in this order:
	 * 1. $READING_PLAN_DIR
	 * 2. $XDG_DATA_HOME/reading
	 * 3. $HOME/.local/share/reading
	 */
	if ((dirpath = getenv("READING_PLAN_DIR")))
		dirpath = strdup(dirpath);
	else if ((dirpath = getenv("XDG_DATA_HOME")))
		dirpath = append(strdup(dirpath), "/reading");
	else if ((dirpath = getenv("HOME")))
		dirpath = append(strdup(dirpath), "/.local/share/reading");
	else
		errx(1, "could not find plan directory");

	if (mkdir_recursive(dirpath))
		err(1, "could not create plan directory '%s'", dirpath);
	return dirpath;
}

static void *xrealloc(void *ptr, size_t size)
{
	void *res = realloc(ptr, size);
	if (!res)
		errx(1, "out of memory");
	return res;
}
