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

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** The permissions to use when creating the plan directory. */
#define PLAN_DIR_PERMS 0755
/** The program's version string. */
#define VERSION "0.1.0"

/** Clamp the middle value to lie within the range specified by low and high. */
#define clamp(low, val, high) ((val) < (low) ? (low) : \
                               (val) > (high) ? (high) : (val))

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

/** INTERNAL PLAN FUNCTIONS */
/**
 * Return the number of entries in the given plan.  The program will exit if
 * this cannot be done (e.g. if the plan does not exist).
 */
static int plan_count_entries(const char *plan);
/**
 * Given an already-opened plan file, consume characters until the beginning of
 * the next entry.  If there are no more entries after this one, a non-zero
 * value is returned; otherwise, zero is returned.  The next character to be
 * read after a call to this function will be the first character in the
 * entry's title.
 */
static int plan_file_next_entry(FILE *planfile);
/**
 * Like plan_file_next_entry, but print the entry title and description as they
 * are encountered instead of just skipping them.  It is an error to call this
 * function if the next character in the file is not the start of an entry.
 */
static int plan_file_print_entry(FILE *planfile);
/**
 * Return the current entry of the given plan.  The program will abort if this
 * cannot be done (e.g. if the plan does not exist).
 */
static int plan_get_entry(const char *plan);
/**
 * Set the current entry of the given plan.  Unlike the set subcommand, this
 * function does not check to make sure that the entry stays within the bounds
 * of the plan's available entries.
 */
static void plan_set_entry(const char *plan, int entry);

/** HELPERS */
/** Append s2 onto s1, where s1 must have been dynamically allocated. */
static char *append(char *s1, const char *s2);
/** Return whether s1 ends with the string s2. */
static bool ends_with(const char *s1, const char *s2);
/** Recursively create the given directory. */
static int mkdir_recursive(const char *dirpath);
/**
 * Parse an integer from the given string (base 10).  If the given string does
 * not represent an integer because it is either out of range or contains
 * invalid characters, errno is set to ERANGE or EINVAL, respectively.  The
 * return value for an invalid input is undefined and should not be relied
 * upon.
 */
static int parse_int(const char *str);
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
			num = parse_int(argv[0]);
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
			entry = parse_int(argv[0]);
			if (errno != 0)
				err(1, "bad argument to '-t'");
			set(argv[1], entry);
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

	free(path);
}

static void
next(const char *plan)
{
	int entries = plan_count_entries(plan);
	int entry = plan_get_entry(plan);
	plan_set_entry(plan, clamp(1, entry + 1, entries + 1));
}

static void
previous(const char *plan)
{
	int entries = plan_count_entries(plan);
	int entry = plan_get_entry(plan);
	plan_set_entry(plan, clamp(1, entry - 1, entries + 1));
}

static void
set(const char *plan, int entry)
{
	int entries = plan_count_entries(plan);
	plan_set_entry(plan, clamp(1, entry, entries + 1));
}

static void
show(const char *plan, int num)
{
	int entry;
	char *path = append(append(plandir(), "/"), plan);
	FILE *planfile;
	int c;

	if (!(planfile = fopen(path, "r"))) {
		if (errno == ENOENT)
			errx(1, "plan '%s' does not exist", plan);
		else
			err(1, "could not open plan file '%s' for reading",
			    path);
	}
	entry = plan_get_entry(plan);

	if (!isblank(c = getc(planfile))) {
		/*
		 * If we're starting on an entry, then calling
		 * plan_file_next_entry will skip over it and go to the second
		 * entry.  If we're not starting on an entry, then calling
		 * plan_file_next_entry will skip to the beginning of the
		 * *first* entry.  To make sure we do the same thing in both
		 * cases, we ensure that 'entry' holds the number of skip calls
		 * we must make.
		 */
		entry--;
		ungetc(c, planfile);
	}
	for (int i = 0; i < entry; i++)
		plan_file_next_entry(planfile);
	for (int i = 0; i < num; i++)
		if (plan_file_print_entry(planfile))
			break;

	fclose(planfile);
}

static void
summary(const char *plan)
{
	if (plan) {
		int entries = plan_count_entries(plan);
		int entry = plan_get_entry(plan);

		if (entry > entries) {
			printf("%s (end of plan)\n", plan);
		} else {
			printf("%s (%d/%d): ", plan, plan_get_entry(plan),
			    plan_count_entries(plan));
			show(plan, 1);
		}
	} else {
		char *path = plandir();
		DIR *dir;
		struct dirent *file;

		if (!(dir = opendir(path)))
			err(1, "could not open plan directory (%s)",
			    plandir());

		errno = 0;
		while ((file = readdir(dir)))
			if (file->d_name[0] != '.' &&
			    !ends_with(file->d_name, ".status"))
				summary(file->d_name);

		if (errno != 0)
			err(1, "could not read from plan directory (%s)",
			    plandir());
		closedir(dir);
	}
}

static int
plan_count_entries(const char *plan)
{
	char *path = append(append(plandir(), "/"), plan);
	FILE *planfile;
	int entries = 0;
	int c;

	if (!(planfile = fopen(path, "r"))) {
		if (errno == ENOENT)
			errx(1, "plan '%s' does not exist", plan);
		else
			err(1, "could not open plan file '%s' for reading",
			    path);
	}
	if ((c = getc(planfile)) == EOF)
		return 0;
	if (!isblank(c))
		entries++;
	while (!plan_file_next_entry(planfile))
		entries++;
	fclose(planfile);

	return entries;
}

static int
plan_file_next_entry(FILE *planfile)
{
	int c;

	/* Skip lines until we reach one that doesn't start with a space. */
	do {
		while ((c = getc(planfile)) != '\n')
			if (c == EOF)
				return 1;
	} while (isblank(c = getc(planfile)));
	if (c == EOF)
		return 1;
	ungetc(c, planfile);

	return 0;
}

static int
plan_file_print_entry(FILE *planfile)
{
	int c;

	/* Print title line first. */
	while ((c = getc(planfile)) != '\n')
		if (c == EOF) {
			putchar('\n');
			return 1;
		} else {
			putchar(c);
		}
	putchar('\n');

	/* Process description lines separately so they can be indented. */
	while (isblank(c = getc(planfile))) {
		/* Replace all leading whitespace with a single tab. */
		while (isblank(c = getc(planfile)))
			;
		ungetc(c, planfile);
		putchar('\t');
		while ((c = getc(planfile)) != '\n')
			if (c == EOF) {
				putchar('\n');
				return 1;
			} else {
				putchar(c);
			}
		putchar('\n');
	}
	if (c == EOF)
		return 1;
	/* c now contains the first character in the next entry. */
	ungetc(c, planfile);

	return 0;
}

static int
plan_get_entry(const char *plan)
{
	char *path = append(append(append(plandir(), "/"), plan), ".status");
	FILE *status;
	int entry;
	char buf[32];
	size_t got;

	if (!(status = fopen(path, "r"))) {
		if (errno == ENOENT)
			errx(1, "status for plan '%s' not found", plan);
		else
			err(1, "could not access plan status file '%s'", path);
	}

	/* Make sure we have enough space left for the 0 byte at the end. */
	got = fread(buf, 1, sizeof buf - 1, status);
	if (ferror(status))
		err(1, "could not read from plan status file '%s'", path);
	if (!feof(status))
		errx(1, "malformed status file '%s' (too long)", path);
	fclose(status);
	buf[got] = '\0';

	errno = 0;
	entry = parse_int(buf);
	if (errno != 0)
		err(1, "malformed status file '%s' (expected number)", path);

	free(path);
	return entry;
}

static void
plan_set_entry(const char *plan, int entry)
{
	char *path = append(append(append(plandir(), "/"), plan), ".status");
	FILE *status;

	if (!(status = fopen(path, "w")))
		err(1, "could not open plan status file '%s' for writing",
		    path);
	if (fprintf(status, "%d", entry) < 0)
		err(1, "could not write to plan status file '%s'", path);

	fclose(status);
	free(path);
}

static bool
ends_with(const char *s1, const char *s2)
{
	size_t z1 = strlen(s1);
	size_t z2 = strlen(s2);
	return !strcmp(s1 + z1 - z2, s2);
}

static char *
append(char *s1, const char *s2)
{
	size_t z1 = strlen(s1);
	size_t z2 = strlen(s2);
	s1 = xrealloc(s1, z1 + z2 + 1);

	memcpy(s1 + z1, s2, z2);
	s1[z1 + z2] = '\0';

	return s1;
}

static int
mkdir_recursive(const char *dirpath)
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
	/* It's not good for a utility function to set errno to 0. */
	if (errno == 0)
		errno = errno_old;
	free(tmp);
	return retval;
}

static int
parse_int(const char *str)
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

static char *
plandir(void)
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

static void *
xrealloc(void *ptr, size_t size)
{
	void *res = realloc(ptr, size);
	if (!res)
		errx(1, "out of memory");
	return res;
}
