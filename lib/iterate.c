/*-
 * Copyright (c) 2007 Joerg Sonnenberger <joerg@NetBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <nbcompat.h>

#if HAVE_ERR_H
#include <err.h>
#endif

#include "lib.h"

#ifndef __UNCONST
#define __UNCONST(a)	((void *)(unsigned long)(const void *)(a))
#endif

/*
 * Generic iteration function:
 * - get new entries from srciter, stop on NULL
 * - call matchiter for those entries, stop on non-null return value.
 */
int
iterate_pkg_generic_src(int (*matchiter)(const char *, void *),
    void *match_cookie, const char *(*srciter)(void *), void *src_cookie)
{
	int retval;
	const char *entry;

	retval = 0;

	while ((entry = (*srciter)(src_cookie)) != NULL) {
		if ((retval = (*matchiter)(entry, match_cookie)) != 0)
			break;
	}

	return retval;
}

static const char *
pkg_dir_iter(void *cookie)
{
	DIR *dirp = cookie;
	struct dirent *dp;
	size_t len;

	while ((dp = readdir(dirp)) != NULL) {
#if defined(DT_UNKNOWN) && defined(DT_DIR)
		if (dp->d_type != DT_UNKNOWN && dp->d_type != DT_REG)
			continue;
#endif
		len = strlen(dp->d_name);
		/* .tbz or .tgz suffix length + some prefix*/
		if (len < 5)
			continue;
		if (memcmp(dp->d_name + len - 4, ".tgz", 4) == 0 ||
		    memcmp(dp->d_name + len - 4, ".tbz", 4) == 0)
			return dp->d_name;
	}
	return NULL;
}

/*
 * Call matchiter for every package in the directory.
 */
int
iterate_local_pkg_dir(const char *dir, int (*matchiter)(const char *, void *),
    void *cookie)
{
	DIR *dirp;
	int retval;

	if ((dirp = opendir(dir)) == NULL)
		return -1;

	retval = iterate_pkg_generic_src(matchiter, cookie, pkg_dir_iter, dirp);

	if (closedir(dirp) == -1)
		return -1;
	return retval;
}

static const char *
pkg_db_iter(void *cookie)
{
	DIR *dirp = cookie;
	struct dirent *dp;

	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0)
			continue;
		if (strcmp(dp->d_name, "..") == 0)
			continue;
		if (strcmp(dp->d_name, "pkgdb.byfile.db") == 0)
			continue;
		if (strcmp(dp->d_name, ".cookie") == 0)
			continue;
		if (strcmp(dp->d_name, "pkg-vulnerabilities") == 0)
			continue;
#if defined(DT_UNKNOWN) && defined(DT_DIR)
		if (dp->d_type != DT_UNKNOWN && dp->d_type != DT_DIR)
			continue;
#endif
		return dp->d_name;
	}
	return NULL;
}

/*
 * Call matchiter for every installed package.
 */
int
iterate_pkg_db(int (*matchiter)(const char *, void *), void *cookie)
{
	DIR *dirp;
	int retval;

	if ((dirp = opendir(_pkgdb_getPKGDB_DIR())) == NULL)
		return -1;

	retval = iterate_pkg_generic_src(matchiter, cookie, pkg_db_iter, dirp);

	if (closedir(dirp) == -1)
		return -1;
	return retval;
}

static int
match_by_basename(const char *pkg, void *cookie)
{
	const char *target = cookie;
	const char *pkg_version;

	if ((pkg_version = strrchr(pkg, '-')) == NULL) {
		warnx("Entry %s in pkgdb is not a valid package name", pkg);
		return 0;
	}
	if (strncmp(pkg, target, pkg_version - pkg) == 0 &&
	    strlen(target) == pkg_version - pkg)
		return 1;
	else
		return 0;
}

static int
match_by_pattern(const char *pkg, void *cookie)
{
	const char *pattern = cookie;

	return pkg_match(pattern, pkg);	
}

struct add_matching_arg {
	lpkg_head_t *pkghead;
	size_t got_match;
	int (*match_fn)(const char *pkg, void *cookie);
	void *cookie;
};

static int
match_and_add(const char *pkg, void *cookie)
{
	struct add_matching_arg *arg = cookie;
	lpkg_t *lpp;

	if ((*arg->match_fn)(pkg, arg->cookie) == 1) {
		arg->got_match = 1;

		lpp = alloc_lpkg(pkg);
		TAILQ_INSERT_TAIL(arg->pkghead, lpp, lp_link);
	}
	return 0;
}

/*
 * Find all installed packages with the given basename and add them
 * to pkghead.
 * Returns -1 on error, 0 if no match was found and 1 otherwise.
 */
int
add_installed_pkgs_by_basename(const char *pkgbase, lpkg_head_t *pkghead)
{
	struct add_matching_arg arg;

	arg.pkghead = pkghead;
	arg.got_match = 0;
	arg.match_fn = match_by_basename;
	arg.cookie = __UNCONST(pkgbase);

	if (iterate_pkg_db(match_and_add, &arg) == -1) {
		warnx("could not process pkgdb");
		return -1;
	}
	return arg.got_match;
}

/*
 * Match all installed packages against pattern, add the matches to pkghead.
 * Returns -1 on error, 0 if no match was found and 1 otherwise.
 */
int
add_installed_pkgs_by_pattern(const char *pattern, lpkg_head_t *pkghead)
{
	struct add_matching_arg arg;

	arg.pkghead = pkghead;
	arg.got_match = 0;
	arg.match_fn = match_by_pattern;
	arg.cookie = __UNCONST(pattern);

	if (iterate_pkg_db(match_and_add, &arg) == -1) {
		warnx("could not process pkgdb");
		return -1;
	}
	return arg.got_match;
}

struct best_installed_match_arg {
	const char *pattern;
	char *best_current_match;
};

static int
match_best_installed(const char *pkg, void *cookie)
{
	struct best_installed_match_arg *arg = cookie;

	switch (pkg_order(arg->pattern, pkg, arg->best_current_match)) {
	case 0:
	case 2:
		/*
		 * Either current package doesn't match or
		 * the older match is better. Nothing to do.
		 */
		break;
	case 1:
		/* Current package is better, remember it. */
		free(arg->best_current_match);
		if ((arg->best_current_match = strdup(pkg)) == NULL)
			return -1;
		break;
	}
	return 0;
}

/*
 * Returns a copy of the name of best matching package.
 * If no package matched the pattern or an error occured, return NULL.
 */
char *
find_best_matching_installed_pkg(const char *pattern)
{
	struct best_installed_match_arg arg;

	arg.pattern = pattern;
	arg.best_current_match = NULL;

	if (iterate_pkg_db(match_best_installed, &arg) == -1) {
		warnx("could not process pkgdb");
		return NULL;
	}

	return arg.best_current_match;
}

struct call_matching_arg {
	const char *pattern;
	int (*call_fn)(const char *pkg, void *cookie);
	void *cookie;
};

static int
match_and_call(const char *pkg, void *cookie)
{
	struct call_matching_arg *arg = cookie;

	if (pkg_match(arg->pattern, pkg) == 1) {
		return (*arg->call_fn)(pkg, arg->cookie);
	} else 
		return 0;
}

/*
 * Find all packages that match the given pattern and call the function
 * for each of them. Iteration stops if the callback return non-0.
 * Returns -1 on error, 0 if the iteration finished or whatever the
 * callback returned otherwise.
 */
int
match_installed_pkgs(const char *pattern, int (*cb)(const char *, void *),
    void *cookie)
{
	struct call_matching_arg arg;

	arg.pattern = pattern;
	arg.call_fn = cb;
	arg.cookie = cookie;

	return iterate_pkg_db(match_and_call, &arg);
}
