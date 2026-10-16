/* os9conv.c: messages, names, paths and converting Microware's files for the
 * m68k-os9-* front ends; see os9conv.h. Moved here from m68k-os9-xcc.c
 * (2026-10-08) so that m68k-os9-l68, m68k-os9-libgen and m68k-os9-r68 share
 * it.
 *
 * MIT License; see elf2mod.c.
 */

#include "os9conv.h"
#include "os9link.h"
#include "hostsys.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libiberty.h>

#define RANLIB	os9_gnu("ranlib")
#define AR	os9_gnu("ar")
#define ROF2ELF	os9_tool("rof2elf")

const char *os9_who = "m68k-os9";
int os9_quiet;

struct os9conv os9conv = { .save = SAVE_BESIDE, .elfdir = "m68k-os9-elf", .obj = OBJ_ELF };

void os9_error(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", os9_who);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	os9_cleanup();
	exit(1);
}

void os9_note(const char *format, ...)
{
	va_list ap;

	if (os9_quiet)
		return;
	fprintf(stderr, "%s: ", os9_who);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
}

int ci_eq(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	return *a == *b;
}

int ci_prefix(const char *s, const char *prefix)
{
	for (; *prefix != '\0'; s++, prefix++)
		if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
			return 0;
	return 1;
}

/* ----- Paths ----- */

const char *path_suffix(const char *path)
{
	const char *base = lbasename(path);
	const char *dot = strrchr(base, '.');
	return dot != NULL ? dot : base + strlen(base);
}

char *path_stem(const char *path, int lower)
{
	const char *base = lbasename(path);
	const char *dot = strrchr(base, '.');
	size_t length = dot != NULL ? (size_t)(dot - base) : strlen(base);
	char *s = xmalloc(length + 1);
	memcpy(s, base, length);
	s[length] = '\0';
	if (lower)
		for (char *p = s; *p != '\0'; p++)
			*p = tolower((unsigned char)*p);
	return s;
}

char *path_dir(const char *path)
{
	const char *base = lbasename(path);
	size_t length = base - path;
	while (length > 1 && host_is_sep(path[length - 1]))
		length--;
	char *d = xmalloc(length + 1);
	memcpy(d, path, length);
	d[length] = '\0';
	return d;
}

int file_starts_with(const char *path, const char *bytes, size_t length)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;
	char buffer[16];
	size_t got = fread(buffer, 1, length, fp);
	fclose(fp);
	return got == length && memcmp(buffer, bytes, length) == 0;
}

char *os9_module_name(const char *output)
{
	const char *base = lbasename(output);
	size_t length = strlen(base);
	if (length > 4 && strcmp(base + length - 4, ".mod") == 0)
		length -= 4;
	char *name = xmalloc(length + 1);
	memcpy(name, base, length);
	name[length] = '\0';
	return name;
}

int file_is_elf(const char *path)
{
	return file_starts_with(path, "\177ELF", 4);
}

int file_is_ar(const char *path)
{
	return file_starts_with(path, "!<arch>\n", 8);
}

/* (also Ultra C 1.x's older layout, $2D00D5BB, which rof2elf reads) */
int file_is_libgen(const char *path)
{
	return file_starts_with(path, "\x2D\x00\xD5\xBC", 4) || file_starts_with(path, "\x2D\x00\xD5\xBB", 4);
}

/* ----- Settings ----- */

void os9conv_set_save(const char *value)
{
	if (ci_eq(value, "beside"))
		os9conv.save = SAVE_BESIDE;
	else if (ci_eq(value, "subdir"))
		os9conv.save = SAVE_SUBDIR;
	else if (ci_eq(value, "cache"))
		os9conv.save = SAVE_CACHE;
	else
		os9_error("unknown -elf-save=%s (beside, subdir or cache)", value);
}

void os9conv_set_obj(const char *value, unsigned allowed, const char *option)
{
	static const char *const names[] = { "elf", "rof", "ucc", "gas" };
	char list[64] = "";
	for (int i = 0; i < 4; i++) {
		if (!(allowed & (1u << i)))
			continue;
		if (ci_eq(value, names[i])) {
			os9conv.obj = (enum os9_obj)i;
			return;
		}
		if (*list != '\0')
			strcat(list, ", ");
		strcat(list, names[i]);
	}
	os9_error("unknown %s%s (%s)", option, value, list);
}

void os9conv_environment(unsigned allowed)
{
	const char *env = getenv("M68K_OS9_ELF");
	if (env == NULL || *env == '\0')
		return;
	char *copy = xstrdup(env), *next;
	for (char *item = copy; item != NULL; item = next) {
		next = strchr(item, ',');
		if (next != NULL)
			*next++ = '\0';
		if (*item == '\0')
			continue;
		char *value = strchr(item, '=');
		if (value == NULL)
			os9_error("M68K_OS9_ELF: %s: expected name=value", item);
		*value++ = '\0';
		if (ci_eq(item, "save"))
			os9conv_set_save(value);
		else if (ci_eq(item, "dir"))
			os9conv.elfdir = xstrdup(value);
		else if (ci_eq(item, "cache"))
			os9conv.cache = xstrdup(value);
		else if (ci_eq(item, "obj"))
			/* Object formats only: an assembly syntax would turn every
			 * object of a build into a source. */
			os9conv_set_obj(value, allowed & ((1u << OBJ_ELF) | (1u << OBJ_ROF) | (1u << OBJ_UCC)),
				"M68K_OS9_ELF obj=");
		else
			os9_error("M68K_OS9_ELF: unknown setting %s", item);
	}
	free(copy);
}

int os9conv_option(const char *arg, unsigned allowed)
{
	static const char *const names[] = { "elf-save=", "elf-dir=", "elf-cache=", "obj=" };
	size_t i;
	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		if (ci_prefix(arg + 1, names[i]))
			break;
	if (i == sizeof names / sizeof names[0])
		return 0;
	const char *value = arg + 1 + strlen(names[i]);
	switch (i) {
	case 0: os9conv_set_save(value); break;
	case 1: os9conv.elfdir = value; break;
	case 2: os9conv.cache = value; break;
	case 3: os9conv_set_obj(value, allowed, "-obj="); break;
	}
	return 1;
}

/* ----- Converting ----- */

/* The cache's directory for absolute directory DIR (allocated). */
static char *cache_dir(const char *dir)
{
	const char *root = os9conv.cache;
	char *home = NULL;
	if (root == NULL) {
		const char *xdg = getenv("XDG_CACHE_HOME");
		if (xdg != NULL && *xdg != '\0')
			root = xdg;
		else {
			const char *h = getenv("HOME");
			if (h == NULL)
				h = getenv("USERPROFILE");
			if (h == NULL)
				h = ".";
			root = home = concat(h, "/.cache", NULL);
		}
	}
	/* The directory's path below it, without a drive's colon. */
	char *below = xstrdup(dir);
	for (char *p = below; *p != '\0'; p++)
		if (*p == ':')
			*p = '_';
	const char *start = below;
	while (host_is_sep(*start))
		start++;
	char *path = concat(root, "/", os9conv.elfdir, "/", start, NULL);
	free(below);
	free(home);
	return path;
}

/* Where the ELF form of ROF object or library INPUT goes (allocated). */
static char *converted_path(const char *input, int library)
{
	char *abs = host_abspath(input);
	char *dir = path_dir(abs);
	char *name = path_stem(abs, 1);
	char *file = library ? concat(os9conv.save == SAVE_BESIDE ? "lib" : "", name, ".a", NULL)
		: concat(name, ".o", NULL);
	char *target = NULL;

	if (os9conv.save == SAVE_BESIDE && host_writable_dir(dir)) {
		target = concat(dir, "/", file, NULL);
		/* A ROF object some makefile named .o would be its own target. */
		if (ci_eq(target, abs)) {
			free(target);
			target = NULL;
		}
	}
	else if (os9conv.save == SAVE_SUBDIR) {
		char *sub = concat(dir, "/", os9conv.elfdir, NULL);
		if ((host_mtime(sub) != (time_t)-1 || (host_writable_dir(dir) && host_mkdirs(sub) == 0)) &&
			host_writable_dir(sub))
			target = concat(sub, "/", file, NULL);
		free(sub);
	}
	if (target == NULL) {
		char *cache = cache_dir(dir);
		if (os9conv.save == SAVE_SUBDIR || (os9conv.save == SAVE_BESIDE && !host_writable_dir(dir)))
			os9_note("%s isn't writable; converting %s into %s", dir, lbasename(input), cache);
		if (host_mkdirs(cache) != 0)
			os9_error("cannot create %s", cache);
		if (library && os9conv.save == SAVE_BESIDE) {
			free(file);
			file = concat(name, ".a", NULL);
		}
		target = concat(cache, "/", file, NULL);
		free(cache);
	}
	free(abs);
	free(dir);
	free(name);
	free(file);
	return target;
}

char *os9conv_as_elf(const char *input, int library)
{
	if (host_mtime(input) == (time_t)-1)
		os9_error("cannot find %s", input);
	if (file_is_elf(input) || file_is_ar(input))
		return xstrdup(input);

	char *target = converted_path(input, library);
	time_t made = host_mtime(target);
	if (made != (time_t)-1 && made >= host_mtime(input))
		return target;

	/* Convert into a temporary file beside the target, then rename it, so
	 * that parallel runs never see half a file. */
	char *temp = xmalloc(strlen(target) + 32);
	sprintf(temp, "%s.%ld.tmp", target, host_pid());
	struct args argv = { 0 };
	args_add(&argv, ROF2ELF);
	args_add(&argv, "-o");
	args_add(&argv, temp);
	args_add(&argv, input);
	int status = os9_run(&argv, NULL, NULL);
	args_free(&argv);
	if (status == 0 && library && !os9_dry_run && !file_is_ar(temp)) {
		/* one ROF given as a library (l68 -l=stamp.r: a library of one
		 * member, as Microware's l68 takes it): rof2elf's object put
		 * into an archive of its own */
		char *obj = xmalloc(strlen(temp) + 3);
		sprintf(obj, "%s.o", temp);
		if (host_replace(temp, obj) != 0) {
			status = 1;
		} else {
			args_add(&argv, AR);
			args_add(&argv, "rc");
			args_add(&argv, temp);
			args_add(&argv, obj);
			status = os9_run(&argv, NULL, NULL);
			args_free(&argv);
			remove(obj);
		}
		free(obj);
	}
	if (status == 0 && library) {
		args_add(&argv, RANLIB);
		args_add(&argv, temp);
		status = os9_run(&argv, NULL, NULL);
		args_free(&argv);
	}
	if (!os9_dry_run && (status != 0 || host_replace(temp, target) != 0)) {
		remove(temp);
		os9_error("cannot convert %s", input);
	}
	free(temp);
	return target;
}
