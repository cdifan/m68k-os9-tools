/* os9link.c: argument vectors, running commands, and linking a program into
 * an OS-9 module with elf2mod; see os9link.h.
 *
 * MIT License; see elf2mod.c.
 */

#include "os9link.h"
#include "hostsys.h"
#include "r68x_note.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libiberty.h>

int os9_show, os9_dry_run;

void args_add(struct args *a, const char *arg)
{
	if (a->n + 2 > a->size) {
		a->size = a->size ? 2 * a->size : 16;
		a->v = xrealloc(a->v, a->size * sizeof *a->v);
	}
	a->v[a->n++] = xstrdup(arg);
	a->v[a->n] = NULL;
}

void args_addf(struct args *a, const char *format, ...)
{
	va_list ap;
	char *arg;

	va_start(ap, format);
	if (vasprintf(&arg, format, ap) < 0)
		arg = NULL;
	va_end(ap);
	if (arg == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	args_add(a, arg);
	free(arg);
}

void args_append(struct args *a, const struct args *more)
{
	for (int i = 0; i < more->n; i++)
		args_add(a, more->v[i]);
}

void args_free(struct args *a)
{
	for (int i = 0; i < a->n; i++)
		free(a->v[i]);
	free(a->v);
	a->v = NULL;
	a->n = a->size = 0;
}

/* Shows ARGV as C 3.2's xcc -bp does: the command line in quotes. */
static void show(const struct args *argv)
{
	putchar('"');
	for (int i = 0; i < argv->n; i++)
		printf(i ? " %s" : "%s", argv->v[i]);
	puts("\"");
	fflush(stdout);
}

int os9_run(const struct args *argv, const char *out, const char *err)
{
	if (os9_show || os9_dry_run)
		show(argv);
	if (os9_dry_run)
		return 0;
	return host_run((const char *const *)argv->v, out, err);
}

static struct args temps;
static char *dir;

void os9_set_tempdir(const char *d)
{
	free(dir);
	dir = host_abspath(d);
}

char *os9_temp(const char *suffix)
{
	static int count;

	if (dir == NULL)
		dir = host_tempdir();
	char *path = xmalloc(strlen(dir) + strlen(suffix) + 64);
	sprintf(path, "%s/m68k-os9-%ld-%d%s", dir, host_pid(), ++count, suffix);
	args_add(&temps, path);
	return path;
}

char *os9_temp_dir(void)
{
	char *path = os9_temp(".d");
	if (host_mkdirs(path) != 0)
		return NULL;
	return path;
}

void os9_temp_add(const char *path)
{
	args_add(&temps, path);
}

void os9_cleanup(void)
{
	/* the latest first: a directory's files before the directory */
	for (int i = temps.n - 1; i >= 0; i--)
		if (remove(temps.v[i]) != 0)
			host_rmdir(temps.v[i]);
	args_free(&temps);
}

/* ----- Finding the tools ----- */

#define GNU_PREFIX	"m68k-elfos9-"
#define FRONT_PREFIX	"m68k-os9-"

static struct {
	char *dir;		/* this program's directory, or null */
	char *self;		/* the name it runs as, without .exe */
	char *prefix;		/* of its name: "m68k-os9-", "" for xcc */
	char *gnu;		/* -gnu-prefix= or M68K_OS9_GNU_PREFIX, or null */
	struct args names, paths;	/* found: "kind:name", its program */
} tools;

void os9_tools_init(const char *argv0, const char *const names[])
{
	tools.dir = host_self_dir(argv0);
	/* the name it runs as, without a directory and .exe */
	char *base = xstrdup(lbasename(argv0 != NULL ? argv0 : ""));
	size_t n = strlen(base);
	if (n > 4 && strcasecmp(base + n - 4, ".exe") == 0)
		base[n -= 4] = '\0';
	tools.prefix = xstrdup(FRONT_PREFIX);
	for (int i = 0; names != NULL && names[i] != NULL; i++) {
		size_t k = strlen(names[i]);
		if (n >= k && strcmp(base + n - k, names[i]) == 0) {
			free(tools.prefix);
			tools.prefix = xstrndup(base, n - k);
			break;
		}
	}
	tools.self = base;
	const char *env = getenv("M68K_OS9_GNU_PREFIX");
	if (env != NULL && *env != '\0' && tools.gnu == NULL)
		tools.gnu = xstrdup(env);
}

void os9_set_gnu_prefix(const char *value)
{
	free(tools.gnu);
	tools.gnu = *value != '\0' ? xstrdup(value) : NULL;
	/* for the programs this one runs too (xcc runs r68 with r68's own
	 * options only) */
	host_setenv("M68K_OS9_GNU_PREFIX", tools.gnu);
}

int os9_tools_option(const char *arg)
{
	static const char opt[] = "-gnu-prefix=";
	if (strncasecmp(arg, opt, sizeof opt - 1) != 0)
		return 0;
	os9_set_gnu_prefix(arg + sizeof opt - 1);
	return 1;
}

/* KIND:NAME found before, or null. */
static const char *found(const char *key)
{
	for (int i = 0; i < tools.names.n; i++)
		if (strcmp(tools.names.v[i], key) == 0)
			return tools.paths.v[i];
	return NULL;
}

static const char *keep(const char *key, char *path)
{
	args_add(&tools.names, key);
	args_add(&tools.paths, path);
	free(path);
	return tools.paths.v[tools.paths.n - 1];
}

/* NAME in this program's directory (allocated), or null. */
static char *in_own_dir(const char *name)
{
	return tools.dir != NULL ? host_program_in(tools.dir, name) : NULL;
}

const char *os9_gnu(const char *tool)
{
	char *key = concat("gnu:", tool, NULL);
	const char *path = found(key);
	if (path != NULL) {
		free(key);
		return path;
	}
	char *p = NULL;
	if (tools.gnu != NULL) {
		size_t n = strlen(tools.gnu);
		if (host_is_dir(tools.gnu))
			p = concat(tools.gnu, host_is_sep(tools.gnu[n - 1]) ? "" : "/", tool, NULL);
		else
			p = concat(tools.gnu, tools.gnu[n - 1] == '-' ? "" : "-", tool, NULL);
	} else {
		char *full = concat(GNU_PREFIX, tool, NULL);
		p = in_own_dir(full);
		/* the short name only in a directory of short names, where it can't
		 * be the host's own gcc or nm; never this program (m68k-os9-gcc
		 * as gcc) */
		if (p == NULL && tools.prefix != NULL && *tools.prefix == '\0' &&
			strcasecmp(tool, tools.self) != 0)
			p = in_own_dir(tool);
		if (p == NULL)
			p = xstrdup(full);
		free(full);
	}
	path = keep(key, p);
	free(key);
	return path;
}

const char *os9_tool(const char *name)
{
	char *key = concat("tool:", name, NULL);
	const char *path = found(key);
	if (path == NULL) {
		char *p = in_own_dir(name);
		path = keep(key, p != NULL ? p : xstrdup(name));
	}
	free(key);
	return path;
}

const char *os9_front(const char *name)
{
	char *key = concat("front:", name, NULL);
	const char *path = found(key);
	if (path == NULL) {
		char *full = concat(FRONT_PREFIX, name, NULL);
		char *p = in_own_dir(full);
		if (p == NULL)
			p = host_program_on_path(full);
		if (p == NULL && tools.prefix != NULL && strcmp(tools.prefix, FRONT_PREFIX) != 0) {
			char *own = concat(tools.prefix, name, NULL);
			p = in_own_dir(own);
			free(own);
		}
		path = keep(key, p != NULL ? p : xstrdup(full));
		free(full);
	}
	free(key);
	return path;
}

/* Copies file PATH to standard error. */
static void cat_stderr(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return;
	char line[1024];
	while (fgets(line, sizeof line, fp) != NULL)
		fputs(line, stderr);
	fclose(fp);
}

/* Whether the linker's messages in PATH are all about truncated relocations
 * (code too large for 16-bit calls), and there are some. */
static int only_truncated(const char *path)
{
	static const char *const harmless[] = {
		"relocation truncated to fit", "additional relocation overflows omitted",
		": in function", "DWARF error", "ld returned 1 exit status",
	};
	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return 0;
	char line[1024];
	int truncated = 0, other = 0;
	while (fgets(line, sizeof line, fp) != NULL) {
		size_t i;
		for (i = 0; i < sizeof harmless / sizeof harmless[0]; i++)
			if (strstr(line, harmless[i]) != NULL)
				break;
		if (i == 0)
			truncated = 1;
		else if (i == sizeof harmless / sizeof harmless[0] && line[strspn(line, " \t\r\n")] != '\0')
			other = 1;
	}
	fclose(fp);
	return truncated && !other;
}

/* Links with the jump table size TABLE (in bytes; -1 for a link that must
 * succeed as it is). ERR gets the linker's messages. */
static int link_elf(struct os9link *l, long table, const char *err)
{
	struct args argv = { 0 };
	args_add(&argv, l->gcc);
	args_append(&argv, &l->link);
	args_add(&argv, "-o");
	args_add(&argv, l->elf);
	if (table >= 0) {
		args_add(&argv, "-Wl,--noinhibit-exec");
		args_addf(&argv, "-Wl,--defsym,__jmptbl_size=%ld", table);
	}
	int status = os9_run(&argv, NULL, err);
	args_free(&argv);
	return status;
}

/* Runs elf2mod on L->elf; OUT, if not null, gets its output. */
static int make_module(struct os9link *l, const char *out)
{
	struct args argv = { 0 };
	args_add(&argv, l->elf2mod);
	args_add(&argv, "-n");
	args_add(&argv, l->name);
	args_append(&argv, &l->mod);
	args_add(&argv, l->elf);
	args_add(&argv, l->module);
	int status = os9_run(&argv, out, NULL);
	args_free(&argv);
	return status;
}

/* The jump table size, in bytes, that elf2mod asks for in its output PATH:
 * "N far calls need a jump table of M entries (B bytes)"; -1 if none. */
static long table_size(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	char line[1024];
	long bytes = -1;
	while (bytes < 0 && fgets(line, sizeof line, fp) != NULL) {
		const char *p = strstr(line, "jump table of ");
		long entries;
		if (p != NULL && sscanf(p, "jump table of %ld entries (%ld bytes)", &entries, &bytes) != 2)
			bytes = -1;
	}
	fclose(fp);
	return bytes;
}

/* Whether file PATH contains S. */
static int file_contains(const char *path, const char *s)
{
	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return 0;
	char line[2048];
	int found = 0;
	while (!found && fgets(line, sizeof line, fp) != NULL)
		found = strstr(line, s) != NULL;
	fclose(fp);
	return found;
}

/* The definitions of the r68x_ symbols (r68x_note.h) the objects refer to,
 * those named in LOG (the failed link's messages): from a link written
 * anyway (--noinhibit-exec), its .r68x section (the notes of the members
 * linked) into a linker script that defines them, which joins L->link.
 * Returns the number defined, or -1 (reported). */
static int r68x_definitions(struct os9link *l, const char *log)
{
	struct args argv = { 0 };
	args_add(&argv, l->gcc);
	args_append(&argv, &l->link);
	args_add(&argv, "-o");
	args_add(&argv, l->elf);
	args_add(&argv, "-Wl,--noinhibit-exec");
	os9_run(&argv, NULL, os9_temp(".log"));
	args_free(&argv);

	const char *objcopy = os9_gnu("objcopy");
	char *notes = os9_temp(".r68x");
	args_add(&argv, objcopy);
	args_addf(&argv, "--dump-section=" R68X_SECTION "=%s", notes);
	args_add(&argv, l->elf);
	args_add(&argv, os9_temp(".elf"));
	int status = os9_run(&argv, NULL, os9_temp(".log"));
	args_free(&argv);
	FILE *fp = status == 0 ? fopen(notes, "r") : NULL;
	if (fp == NULL)
		return 0;

	char *script = os9_temp(".ld");
	FILE *ld = fopen(script, "w");
	if (ld == NULL) {
		fclose(fp);
		fprintf(stderr, "%s: cannot write %s\n", l->who, script);
		return -1;
	}
	struct args names = { 0 }, texts = { 0 }, undef = { 0 };
	char line[4096];
	int count = 0, bad = 0;
	while (fgets(line, sizeof line, fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		char *eq = strchr(line, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		const char *name = line, *text = eq + 1;
		/* only those referred to: an object also notes the
		 * expressions of lines its conditionals left out */
		if (!file_contains(log, name))
			continue;
		struct r68x x;
		char check[32];
		r68x_name(text, check);
		if (!r68x_parse(text, &x) || strcmp(check, name) != 0) {
			fprintf(stderr, "%s: bad link-time expression %s=%s\n", l->who, name, text);
			bad = 1;
			continue;
		}
		int seen = 0;
		for (int i = 0; i < names.n && !seen; i++)
			if (strcmp(names.v[i], name) == 0) {
				seen = 1;
				if (strcmp(texts.v[i], text) != 0) {
					/* two expressions with the same hash */
					fprintf(stderr, "%s: %s stands for both %s and %s\n", l->who, name, texts.v[i], text);
					bad = 1;
				}
			}
		if (!seen) {
			/* -u: the objects refer to the terms only through the
			 * note, which takes no library member in */
			for (int i = 0; i < x.n; i++)
				args_addf(&undef, "-Wl,--undefined=%s", x.t[i].sym);
			char *e = r68x_ld_expression(&x);
			fprintf(ld, "%s = %s;\n", name, e);
			free(e);
			args_add(&names, name);
			args_add(&texts, text);
			count++;
		}
		r68x_free(&x);
	}
	fclose(fp);
	fclose(ld);
	args_free(&names);
	args_free(&texts);
	if (!bad && count) {
		args_append(&l->link, &undef);
		args_add(&l->link, "-Xlinker");
		args_add(&l->link, script);
	}
	args_free(&undef);
	return bad ? -1 : count;
}

/* PC-relative references to absolute symbols (bsr EQU, lea EQU(pc): an
 * equate, or a macro's name defined as NAME:), which ld reports as
 * truncated (the code is at 0x400000): reported as l68 reports them
 * ("relative reference to symbol 'X' in the absolute (equ) area", both
 * Microware linkers, measured 2026-10-10), not as far calls for a jump
 * table. 1 if there are any. */
static int absolute_relative(const char *path, const char *who)
{
	FILE *fp = fopen(path, "r");
	if (fp == NULL)
		return 0;
	char line[1024];
	int found = 0;
	while (fgets(line, sizeof line, fp) != NULL) {
		char *t = strstr(line, ": relocation truncated to fit: R_68K_PC");
		char *a = strstr(line, " defined in *ABS* section");
		char *q = strstr(line, " against symbol `");
		if (t == NULL || a == NULL || q == NULL)
			continue;
		char *s = q + strlen(" against symbol `"), *e = strchr(s, '\'');
		if (e == NULL)
			continue;
		fprintf(stderr, "%s: relative reference to symbol '%.*s' in the absolute (equ) area, at %.*s\n",
			who, (int)(e - s), s, (int)(t - line), line);
		found = 1;
	}
	fclose(fp);
	return found;
}

int os9_link(struct os9link *l)
{
	char *log = os9_temp(".log");
	int status = link_elf(l, -1, log);
	if (os9_dry_run)
		return make_module(l, NULL);
	if (status != 0 && file_contains(log, R68X_PREFIX)) {
		/* link-time expressions: defined, then linked again */
		int n = r68x_definitions(l, log);
		if (n < 0)
			return 1;
		if (n > 0)
			status = link_elf(l, -1, log);
	}
	if (status == 0) {
		cat_stderr(log);
		return make_module(l, NULL);
	}
	if (!only_truncated(log)) {
		cat_stderr(log);
		return status;
	}
	if (absolute_relative(log, l->who))
		return 1;
	if (!file_contains(log, "R_68K_PC16") && !file_contains(log, "R_68K_PLT16")) {
		/* Only absolute references truncated: from the code to code
		 * labels (dc.w Init), linked at 0x400000; elf2mod puts the
		 * labels' module offsets into them, as l68 does. No far calls,
		 * so no jump table. */
		if (link_elf(l, 0, os9_temp(".log")) != 0) {
			cat_stderr(log);
			return status;
		}
		return make_module(l, NULL);
	}
	if (l->no_jmptbl) {
		fprintf(stderr, "%s: the code is too large for 16-bit calls and needs a jump table "
			"(l68's -a; xcc gives it unless -j)\n", l->who);
		return 1;
	}

	/* Link anyway, and let elf2mod say how large a table it needs. */
	char *out = os9_temp(".out");
	link_elf(l, 0, os9_temp(".log"));
	make_module(l, out);
	long table = table_size(out);
	if (table < 0) {
		cat_stderr(log);
		return status;
	}
	if (link_elf(l, table + 6 * l->jmptbl_spare, os9_temp(".log")) != 0) {
		cat_stderr(log);
		return status;
	}
	return make_module(l, NULL);
}
