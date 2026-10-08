/* m68k-os9-libgen: Ultra C's librarian libgen, with GNU tools.
 *
 * A drop-in for makefiles that call libgen: the same options, making an ar
 * archive (-obj=elf, the default), concatenated ROFs (-obj=rof) or, later,
 * libgen's own format (-obj=ucc), from ELF objects, ROF objects and
 * libraries of any kind; and libgen's listings. See m68k-os9-libgen.md.
 *
 * MIT License; see elf2mod.c.
 */

#include "hostsys.h"
#include "os9conv.h"
#include "os9link.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libiberty.h>

#define AR	"m68k-elfos9-ar"
#define NM	"m68k-elfos9-nm"
#define READELF	"m68k-elfos9-readelf"
#define ROF2ELF	"rof2elf"
#define ELF2ROF	"elf2rof"

#define error os9_error

#define OBJ_KINDS ((1u << OBJ_ELF) | (1u << OBJ_ROF) | (1u << OBJ_UCC))

/* ----- Options ----- */

enum kind { FLAG, VALUE, OPTVALUE };

enum id { O_B, O_C, O_E, O_F, O_L, O_LE, O_LI, O_LL, O_LN, O_LU, O_MTS, O_O, O_P, O_Z };

struct option {
	const char *name;	/* lowercase, without the '-'; matched in any case */
	enum kind kind;
	const char *arg;
	const char *help;	/* what libgen does, in our words */
	const char *does;	/* what this program does, when that differs */
	enum id id;
};

/* In the order of libgen -?. */
static const struct option options[] = {
	{ "b", VALUE, "=<n>", "Output buffer size, in KB", "ignored", O_B },
	{ "c", FLAG, "", "Create a library", "in the -obj= format", O_C },
	{ "e", VALUE, "=<n>", "Library edition", "only with -obj=ucc", O_E },
	{ "f", VALUE, "=<path>", "Write the listings to <path>", NULL, O_F },
	{ "l", FLAG, "", "List the library's psects", NULL, O_L },
	{ "le", FLAG, "", "List each psect's sizes, definitions and references", "not yet", O_LE },
	{ "li", FLAG, "", "List the library's identification", NULL, O_LI },
	{ "ll", FLAG, "", "List each psect's local references", "not yet", O_LL },
	{ "ln", FLAG, "", "List symbols as nm does", NULL, O_LN },
	{ "lu", FLAG, "", "List the library's psects, one per line", NULL, O_LU },
	{ "mts", FLAG, "", "Mark the library thread-safe", "only with -obj=ucc", O_MTS },
	{ "o", VALUE, "=<path>", "Output file (default output.l)", NULL, O_O },
	{ "p", VALUE, "=<psect>", "Only list this psect", "not supported", O_P },
	{ "z", OPTVALUE, "[=<path>]", "File names and options from standard input or <path>", NULL, O_Z },
};
#define NOPTIONS (sizeof options / sizeof options[0])

static struct {
	int create;		/* -c */
	const char *edition;	/* -e= */
	const char *listfile;	/* -f= */
	enum id listing;	/* the last listing option (libgen prints that one only); 0 none */
	int listed;
	int mts;		/* -mts */
	const char *out;	/* -o= */
	struct args files;
} st;

/* ----- Usage ----- */

static void usage_line(const char *left, const char *text)
{
	enum { COLUMN = 18 };
	if (strlen(left) < COLUMN)
		printf("   %-*s ", COLUMN, left);
	else
		printf("   %s\n   %-*s ", left, COLUMN, "");
	printf("%s\n", text);
}

static void usage(void)
{
	printf("%s [opts] <files> [opts]\n", os9_who);
	printf("Options of Ultra C's libgen, with GNU tools:\n");
	for (size_t i = 0; i < NOPTIONS; i++) {
		const struct option *o = &options[i];
		char left[64];
		snprintf(left, sizeof left, "-%s%s", o->name, o->arg);
		char *text = concat(o->help, o->does != NULL ? " [" : "", o->does != NULL ? o->does : "",
			o->does != NULL ? "]" : "", NULL);
		usage_line(left, text);
		free(text);
	}
	printf("Options of this program:\n");
	usage_line("-obj=<kind>", "what -c writes: elf (an ar archive, default), rof (concatenated ROFs),\n"
		"                      ucc (libgen's format; not yet)");
	usage_line("-elf-save=<s>", "where files converted to ELF go: beside (default), subdir, cache");
	usage_line("-elf-dir=<name>", "subdirectory, and directory in cache (default m68k-os9-elf)");
	usage_line("-elf-cache=<dir>", "cache directory (default $XDG_CACHE_HOME or ~/.cache)");
	printf("Files: ELF and ROF objects, and libraries of any kind (ar, concatenated ROFs,\n");
	printf("libgen's), taken member by member.\n");
	printf("Environment:\n");
	usage_line("M68K_OS9_ELF", "-elf-.../-obj= settings as save=...,dir=...,cache=...,obj=...");
	usage_line("CLIB", "where sys.l is, for its equates when converting ROFs");
}

/* ----- Parsing ----- */

static void parse(int argc, char **argv);

/* -z[=PATH]: words, file names and options (as libgen takes them). */
static void read_z(const char *path)
{
	FILE *fp = path != NULL ? fopen(path, "r") : stdin;
	if (fp == NULL)
		error("cannot read %s", path);
	struct args words = { 0 };
	args_add(&words, os9_who);
	char word[1024];
	while (fscanf(fp, "%1023s", word) == 1)
		args_add(&words, word);
	if (fp != stdin)
		fclose(fp);
	/* The words stay: the options keep pointers into them. */
	parse(words.n, words.v);
}

static void apply(const struct option *o, const char *value)
{
	switch (o->id) {
	case O_B: break;
	case O_C: st.create = 1; break;
	case O_E: st.edition = value; break;
	case O_F: st.listfile = value; break;
	case O_L: case O_LE: case O_LI: case O_LL: case O_LN: case O_LU:
		st.listing = o->id;
		st.listed = 1;
		break;
	case O_MTS: st.mts = 1; break;
	case O_O: st.out = value; break;
	case O_P: error("-p=%s: listing a single psect isn't supported", value); break;
	case O_Z: read_z(value); break;
	}
}

static void option(const char *arg)
{
	const char *text = arg + 1;
	const struct option *best = NULL;
	const char *value = NULL;
	size_t bestlen = 0;

	if (strcmp(text, "?") == 0) {
		usage();
		os9_cleanup();
		exit(0);
	}
	if (os9conv_option(arg, OBJ_KINDS))
		return;
	for (size_t i = 0; i < NOPTIONS; i++) {
		const struct option *o = &options[i];
		size_t len = strlen(o->name);
		if (len <= bestlen || !ci_prefix(text, o->name))
			continue;
		const char *rest = text + len, *candidate = NULL;
		if (o->kind == FLAG) {
			if (*rest != '\0')
				continue;
		} else if (o->kind == VALUE) {
			if (*rest == '=')
				rest++;
			if (*rest == '\0')
				continue;
			candidate = rest;
		} else {
			candidate = *rest == '=' ? rest + 1 : *rest != '\0' ? rest : NULL;
			if (candidate != NULL && *candidate == '\0')
				candidate = NULL;
		}
		best = o;
		bestlen = len;
		value = candidate;
	}
	if (best == NULL)
		error("unknown option %s (%s -? for the usage)", arg, os9_who);
	apply(best, value);
}

static void parse(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0')
			option(argv[i]);
		else
			args_add(&st.files, argv[i]);
	}
}

/* ----- Members ----- */

/* One psect: an ELF object (a file of its own, maybe extracted from an
 * archive), its psect name and where it came from. */
struct member {
	char *elf;		/* the ELF object */
	char *psect;		/* the psect's name */
	char *file;		/* the input it came from */
	char *rof;		/* the ROF bytes' file, for -obj=rof: the input if it is a
				 * single ROF, else null (made with elf2rof) */
};

static struct member *members;
static int nmembers;

/* Runs ARGV quietly (not shown even with -v-like settings); OUT gets its
 * output. Returns its status. */
static int run_quiet(struct args *argv, const char *out)
{
	int show = os9_show;
	os9_show = 0;
	int status = os9_run(argv, out, NULL);
	os9_show = show;
	return status;
}

/* The lines of command ARGV's output (allocated, null-terminated). */
static char **lines_of(struct args *argv)
{
	char *out = os9_temp(".out");
	if (run_quiet(argv, out) != 0)
		error("%s failed", argv->v[0]);
	FILE *fp = fopen(out, "r");
	if (fp == NULL)
		error("cannot read %s", out);
	struct args lines = { 0 };
	char line[2048];
	while (fgets(line, sizeof line, fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		args_add(&lines, line);
	}
	fclose(fp);
	if (lines.v == NULL)
		args_add(&lines, ""), lines.n = 0, lines.v[0] = NULL;
	return lines.v;
}

/* The psect name of ELF object PATH: its file symbol (rof2elf writes the
 * psect's name there), else DEFAULT. */
static char *psect_name(const char *path, const char *fallback)
{
	struct args argv = { 0 };
	args_add(&argv, READELF);
	args_add(&argv, "-s");
	args_add(&argv, path);
	char **lines = lines_of(&argv);
	args_free(&argv);
	for (char **l = lines; *l != NULL; l++) {
		char type[32], name[512];
		unsigned long n, value, size;
		char bind[32], vis[32], ndx[32];
		if (sscanf(*l, "%lu: %lx %lu %31s %31s %31s %31s %511s", &n, &value, &size, type, bind, vis, ndx, name) == 8
			&& strcmp(type, "FILE") == 0)
			return xstrdup(name);
	}
	return xstrdup(fallback);
}

static void add_member(const char *elf, const char *file, const char *rof)
{
	members = xrealloc(members, (nmembers + 1) * sizeof *members);
	struct member *m = &members[nmembers++];
	m->elf = xstrdup(elf);
	char *stem = path_stem(elf, 0);
	m->psect = psect_name(elf, stem);
	free(stem);
	m->file = xstrdup(file);
	m->rof = rof != NULL ? xstrdup(rof) : NULL;
}

/* Adds the members of input FILE, in order. */
static void add_input(const char *file)
{
	int library = file_is_ar(file) || file_is_libgen(file) || ci_eq(path_suffix(file), ".l");
	char *elf = os9conv_as_elf(file, library);
	if (!file_is_ar(elf)) {
		int rof = !file_is_elf(file);
		add_member(elf, file, rof ? file : NULL);
		free(elf);
		return;
	}
	/* An archive: its members, extracted into a directory of their own. */
	char *dir = os9_temp(".d");
	if (host_mkdirs(dir) != 0)
		error("cannot create %s", dir);
	char *abs = host_abspath(elf);
	struct args argv = { 0 };
	args_add(&argv, AR);
	args_add(&argv, "t");
	args_add(&argv, abs);
	char **names = lines_of(&argv);
	args_free(&argv);
	/* Members of the same name are extracted one at a time (ar xN). */
	for (int i = 0; names[i] != NULL; i++) {
		int count = 1;
		for (int j = 0; j < i; j++)
			count += strcmp(names[j], names[i]) == 0;
		char num[16];
		snprintf(num, sizeof num, "%d", i);
		char *mdir = concat(dir, "/", num, NULL);
		if (host_mkdirs(mdir) != 0)
			error("cannot create %s", mdir);
		args_add(&argv, AR);
		args_addf(&argv, "--output=%s", mdir);
		args_add(&argv, "xN");
		args_addf(&argv, "%d", count);
		args_add(&argv, abs);
		args_add(&argv, names[i]);
		if (run_quiet(&argv, NULL) != 0)
			error("cannot extract %s from %s", names[i], file);
		args_free(&argv);
		char *path = concat(mdir, "/", names[i], NULL);
		add_member(path, file, NULL);
		free(path);
		free(mdir);
	}
	free(abs);
	free(elf);
}

/* ----- Checks ----- */

struct sym {
	char type;		/* nm's */
	unsigned long value;
	char *name;
};

/* The symbols of member M as nm lists them, in its order (-p). */
static struct sym *member_syms(const struct member *m, int *count)
{
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, "-p");
	args_add(&argv, m->elf);
	char **lines = lines_of(&argv);
	args_free(&argv);
	struct sym *syms = NULL;
	int n = 0;
	for (char **l = lines; *l != NULL; l++) {
		char type, name[512];
		unsigned long value = 0;
		if (sscanf(*l, "%lx %c %511s", &value, &type, name) != 3) {
			value = 0;
			if (sscanf(*l, " %c %511s", &type, name) != 2)
				continue;
		}
		syms = xrealloc(syms, (n + 1) * sizeof *syms);
		syms[n].type = type;
		syms[n].value = value;
		syms[n].name = xstrdup(name);
		n++;
	}
	*count = n;
	return syms;
}

/* A global symbol defined twice is an error, as with libgen. */
static void check_duplicates(void)
{
	int failed = 0;
	struct { char *name; int member; } *defs = NULL;
	int ndefs = 0;
	for (int i = 0; i < nmembers; i++) {
		int n;
		struct sym *s = member_syms(&members[i], &n);
		for (int k = 0; k < n; k++) {
			if (!isupper((unsigned char)s[k].type) || s[k].type == 'U')
				continue;
			for (int j = 0; j < ndefs; j++)
				if (strcmp(defs[j].name, s[k].name) == 0) {
					const struct member *a = &members[i], *b = &members[defs[j].member];
					fprintf(stderr, "%s: symbol %s, defined by psect %s, file %s, already defined by "
						"psect %s, file %s\n", os9_who, s[k].name, a->psect, a->file, b->psect, b->file);
					failed = 1;
				}
			defs = xrealloc(defs, (ndefs + 1) * sizeof *defs);
			defs[ndefs].name = s[k].name;
			defs[ndefs].member = i;
			ndefs++;
		}
	}
	if (failed)
		error("no library written");
}

/* ----- Writing ----- */

/* A temporary file beside OUT (allocated), so that renaming it to OUT stays
 * on one file system; removed by os9_cleanup if left over. */
static char *temp_beside(const char *out)
{
	char *temp = xmalloc(strlen(out) + 32);
	sprintf(temp, "%s.%ld.tmp", out, host_pid());
	return temp;
}

/* Copies file FROM to TO; returns 0, or -1. */
static int copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb"), *out = in != NULL ? fopen(to, "wb") : NULL;
	char buf[8192];
	size_t n;
	int status = in != NULL && out != NULL ? 0 : -1;
	while (status == 0 && (n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n)
			status = -1;
	if (in != NULL)
		fclose(in);
	if (out != NULL && fclose(out) != 0)
		status = -1;
	return status;
}

/* Writes the members as an ar archive with its index to OUT; member names
 * made unique (psect.o, psect_2.o, ...). */
static void write_ar(const char *out)
{
	char *dir = os9_temp(".ar");
	if (host_mkdirs(dir) != 0)
		error("cannot create %s", dir);
	char *temp = temp_beside(out);
	struct args argv = { 0 };
	args_add(&argv, AR);
	args_add(&argv, "rcs");
	args_add(&argv, temp);
	for (int i = 0; i < nmembers; i++) {
		char *base = path_stem(members[i].elf, 0);
		char *name = concat(base, ".o", NULL);
		for (int k = 2; ; k++) {
			char *path = concat(dir, "/", name, NULL);
			int used = host_mtime(path) != (time_t)-1;
			free(path);
			if (!used)
				break;
			free(name);
			char num[16];
			snprintf(num, sizeof num, "_%d.o", k);
			name = concat(base, num, NULL);
		}
		char *path = concat(dir, "/", name, NULL);
		if (copy_file(members[i].elf, path) != 0)
			error("cannot copy %s", members[i].elf);
		args_add(&argv, path);
		free(path);
		free(name);
		free(base);
	}
	if (os9_run(&argv, NULL, NULL) != 0)
		error("making %s failed", out);
	args_free(&argv);
	if (!os9_dry_run && host_replace(temp, out) != 0)
		error("cannot write %s", out);
}

/* Appends file PATH to FP. */
static void append_file(FILE *fp, const char *path)
{
	FILE *in = fopen(path, "rb");
	if (in == NULL)
		error("cannot read %s", path);
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, fp) != n)
			error("cannot write");
	fclose(in);
}

/* Writes the members as concatenated ROFs to OUT: ROF inputs as they are,
 * ELF members through elf2rof. */
static void write_rof(const char *out)
{
	char *temp = temp_beside(out);
	FILE *fp = fopen(temp, "wb");
	if (fp == NULL)
		error("cannot write %s", temp);
	for (int i = 0; i < nmembers; i++) {
		const char *rof = members[i].rof;
		char *made = NULL;
		if (rof == NULL) {
			made = os9_temp(".r");
			struct args argv = { 0 };
			args_add(&argv, ELF2ROF);
			args_add(&argv, "-o");
			args_add(&argv, made);
			args_add(&argv, members[i].elf);
			if (os9_run(&argv, NULL, NULL) != 0)
				error("converting %s to ROF failed", members[i].elf);
			args_free(&argv);
			rof = made;
		}
		append_file(fp, rof);
	}
	if (fclose(fp) != 0)
		error("cannot write %s", temp);
	if (host_replace(temp, out) != 0)
		error("cannot write %s", out);
}

/* ----- Listings ----- */

static FILE *listing;

/* -l: the psect names of input FILE, five to a line; -lu: one per line. */
static void list_names(const char *file, int columns)
{
	int shown = 0;
	for (int i = 0; i < nmembers; i++) {
		if (strcmp(members[i].file, file) != 0)
			continue;
		if (columns) {
			fprintf(listing, "%-15s ", members[i].psect);
			if (++shown % 5 == 0)
				fputc('\n', listing);
		} else {
			fprintf(listing, "%s\n", members[i].psect);
		}
	}
	if (columns && shown % 5 != 0)
		fputc('\n', listing);
}

/* -ln: lib:psect:value type name; definitions, then references. */
static void list_nm(const char *file)
{
	const char *lib = lbasename(file);
	for (int i = 0; i < nmembers; i++) {
		if (strcmp(members[i].file, file) != 0)
			continue;
		int n;
		struct sym *s = member_syms(&members[i], &n);
		for (int pass = 0; pass < 2; pass++)
			for (int k = 0; k < n; k++) {
				char t = s[k].type;
				if (!isupper((unsigned char)t) || (pass == 0) == (t == 'U'))
					continue;
				char shown = t == 'T' ? 'T' : t == 'U' ? 'U' : t == 'A' ? 'A' : 'D';
				if (t == 'U')
					fprintf(listing, "%s:%s:         U %s\n", lib, members[i].psect, s[k].name);
				else
					fprintf(listing, "%s:%s:%08lx %c %s\n", lib, members[i].psect, s[k].value, shown, s[k].name);
			}
	}
}

/* -li: what library FILE has: a libgen library its header's target,
 * format, edition, date and threads; ar archives and concatenated ROFs only
 * the name. */
static void list_ident(const char *file)
{
	fprintf(listing, "Library name:    %s\n", lbasename(file));
	unsigned char h[46];
	FILE *fp = file_is_libgen(file) ? fopen(file, "rb") : NULL;
	if (fp != NULL && fread(h, 1, sizeof h, fp) == sizeof h) {
		static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
			"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		fprintf(listing, "Target:          %s\n", h[5] == 1 ? "68K" : "?");
		fprintf(listing, "Format:          1.%u\n", h[5]);
		fprintf(listing, "Edition:         %u\n", h[12] << 8 | h[13]);
		fprintf(listing, "Date created:    %s %2u, %u %02u:%02u:%02u\n", h[7] >= 1 && h[7] <= 12 ? months[h[7] - 1] : "?",
			h[8], 1900 + h[6], h[9], h[10], h[11]);
		fprintf(listing, "Threads:         %s\n", h[43] == 2 ? "safe" : "none");
	}
	if (fp != NULL)
		fclose(fp);
	fputc('\n', listing);
}

int main(int argc, char **argv)
{
	os9_who = "m68k-os9-libgen";
	os9conv_environment(OBJ_KINDS);
	if (argc < 2) {
		usage();
		return 0;
	}
	parse(argc, argv);
	int listings = st.listed;
	if (!st.create && !listings) {
		fprintf(stderr, "%s: to create a library, use the -c option\n", os9_who);
		usage();
		return 0;
	}
	if (st.listing == O_LE || st.listing == O_LL)
		error("-le and -ll aren't supported yet");
	if (st.files.n == 0)
		error("no files (%s -? for the usage)", os9_who);

	for (int i = 0; i < st.files.n; i++)
		add_input(st.files.v[i]);

	if (listings) {
		/* With -c too, a listing lists the inputs and writes no library,
		 * as libgen does. */
		listing = stdout;
		if (st.listfile != NULL && (listing = fopen(st.listfile, "w")) == NULL)
			error("cannot write %s", st.listfile);
		for (int i = 0; i < st.files.n; i++) {
			const char *file = st.files.v[i];
			if (st.listing == O_LI)
				list_ident(file);
			else if (st.listing == O_L)
				list_names(file, 1);
			else if (st.listing == O_LU)
				list_names(file, 0);
			else if (st.listing == O_LN)
				list_nm(file);
		}
		if (listing != stdout)
			fclose(listing);
		os9_cleanup();
		return 0;
	}

	check_duplicates();
	const char *out = st.out != NULL ? st.out : "output.l";
	switch (os9conv.obj) {
	case OBJ_ELF: write_ar(out); break;
	case OBJ_ROF: write_rof(out); break;
	default: error("-obj=ucc (libgen's own format) isn't written yet");
	}
	os9_cleanup();
	return 0;
}
