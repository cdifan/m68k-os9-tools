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

#define AR	os9_gnu("ar")
#define NM	os9_gnu("nm")
#define READELF	os9_gnu("readelf")
#define ROF2ELF	os9_tool("rof2elf")
#define ELF2ROF	os9_tool("elf2rof")

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
		"                      ucc (libgen's format)");
	usage_line("-gnu-prefix=<p>", "the GNU tools: a directory of them under their short names,\n"
		"                      or the prefix of their names (default m68k-elfos9)");
	usage_line("-elf-save=<s>", "where files converted to ELF go: beside (default), subdir, cache");
	usage_line("-elf-dir=<name>", "subdirectory, and directory in cache (default m68k-os9-elf)");
	usage_line("-elf-cache=<dir>", "cache directory (default $XDG_CACHE_HOME or ~/.cache)");
	printf("Files: ELF and ROF objects, and libraries of any kind (ar, concatenated ROFs,\n");
	printf("libgen's), taken member by member.\n");
	printf("Environment:\n");
	usage_line("M68K_OS9_GNU_PREFIX", "as -gnu-prefix=");
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
	if (os9_tools_option(arg))
		return;
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

/* Member I as a ROF file: a ROF input as it is, an ELF member through
 * elf2rof, named for its psect. */
static const char *member_rof(int i)
{
	if (members[i].rof != NULL)
		return members[i].rof;
	char *made = os9_temp(".r");
	struct args argv = { 0 };
	args_add(&argv, ELF2ROF);
	args_add(&argv, "-n");
	args_add(&argv, members[i].psect);
	args_add(&argv, "-o");
	args_add(&argv, made);
	args_add(&argv, members[i].elf);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("converting %s to ROF failed", members[i].elf);
	args_free(&argv);
	return made;
}

/* Writes the members as concatenated ROFs to OUT: ROF inputs as they are,
 * ELF members through elf2rof. */
static void write_rof(const char *out)
{
	char *temp = temp_beside(out);
	FILE *fp = fopen(temp, "wb");
	if (fp == NULL)
		error("cannot write %s", temp);
	for (int i = 0; i < nmembers; i++)
		append_file(fp, member_rof(i));
	if (fclose(fp) != 0)
		error("cannot write %s", temp);
	if (host_replace(temp, out) != 0)
		error("cannot write %s", out);
}

/* ----- libgen's format (-obj=ucc) ----- */

/* A ROF psect, read for -obj=ucc. */
struct rref { unsigned type; unsigned long offset; };
struct rext { char *name; struct rref *refs; int nrefs; };
struct rdef { char *name; unsigned type; unsigned long value; };
struct rpsect {
	unsigned series;
	unsigned long statics, idata, code, stack, rstatics, ridata, debug;
	char *name;
	struct rdef *defs;
	int ndefs;
	const unsigned char *body;	/* code, idata, ridata, debug */
	struct rext *exts;
	int nexts;
	struct rref *locals;
	int nlocals;
};

struct rin { const unsigned char *p, *end; const char *file; };

static unsigned long rget(struct rin *in, int n)
{
	if (in->end - in->p < n)
		error("%s: truncated ROF", in->file);
	unsigned long v = 0;
	for (int i = 0; i < n; i++)
		v = v << 8 | *in->p++;
	return v;
}

static char *rname(struct rin *in)
{
	const unsigned char *z = memchr(in->p, 0, in->end - in->p);
	if (z == NULL)
		error("%s: truncated ROF", in->file);
	char *s = xstrdup((const char *)in->p);
	in->p = z + 1;
	return s;
}

static struct rref *rrefs(struct rin *in, int wide, int *count)
{
	*count = (int)rget(in, wide ? 4 : 2);
	struct rref *r = xmalloc((*count ? *count : 1) * sizeof *r);
	for (int i = 0; i < *count; i++) {
		r[i].type = rget(in, 2);
		r[i].offset = rget(in, 4);
	}
	return r;
}

/* Reads the ROFs of file PATH (one, or several concatenated). */
static void read_rofs(const char *path, struct rpsect **ps, int *nps)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		error("cannot read %s", path);
	unsigned char *data = NULL;
	size_t size = 0, cap = 0, n;
	unsigned char buf[8192];
	while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
		if (size + n > cap)
			data = xrealloc(data, cap = (size + n) * 2);
		memcpy(data + size, buf, n);
		size += n;
	}
	fclose(fp);
	struct rin in = { data, data + size, path };
	while (in.p < in.end) {
		if (rget(&in, 4) != 0xDEADFACE)
			error("%s: not a ROF", path);
		*ps = xrealloc(*ps, (*nps + 1) * sizeof **ps);
		struct rpsect *r = &(*ps)[(*nps)++];
		memset(r, 0, sizeof *r);
		rget(&in, 2);		/* type/language */
		rget(&in, 2);		/* attributes/revision */
		if (rget(&in, 2) != 0)
			error("%s: ROF has assembly errors", path);
		r->series = rget(&in, 2);
		if (r->series != 9 && r->series != 0xF9)
			error("%s: unsupported ROF series 0x%04X", path, r->series);
		int wide = r->series == 0xF9;
		in.p += 6;		/* date */
		rget(&in, 2);		/* edition */
		r->statics = rget(&in, 4);
		r->idata = rget(&in, 4);
		r->code = rget(&in, 4);
		r->stack = rget(&in, 4);
		rget(&in, 4);		/* entry offset, */
		rget(&in, 4);		/* trap offset: libgen doesn't keep them */
		r->rstatics = rget(&in, 4);
		r->ridata = rget(&in, 4);
		r->debug = rget(&in, 4);
		r->name = rname(&in);
		r->ndefs = (int)rget(&in, wide ? 4 : 2);
		r->defs = xmalloc((r->ndefs ? r->ndefs : 1) * sizeof *r->defs);
		for (int i = 0; i < r->ndefs; i++) {
			r->defs[i].name = rname(&in);
			r->defs[i].type = rget(&in, 2);
			r->defs[i].value = rget(&in, 4);
		}
		unsigned long body = r->code + r->idata + r->ridata + r->debug;
		if ((unsigned long)(in.end - in.p) < body)
			error("%s: truncated ROF", path);
		r->body = in.p;
		in.p += body;
		r->nexts = (int)rget(&in, wide ? 4 : 2);
		r->exts = xmalloc((r->nexts ? r->nexts : 1) * sizeof *r->exts);
		for (int i = 0; i < r->nexts; i++) {
			r->exts[i].name = rname(&in);
			r->exts[i].refs = rrefs(&in, wide, &r->exts[i].nrefs);
		}
		r->locals = rrefs(&in, wide, &r->nlocals);
		while (in.p < in.end && *in.p == 0)
			in.p++;
	}
	/* the data stays: the bodies point into it */
}

struct wbuf { unsigned char *s; size_t n, cap; };

static void wput(struct wbuf *b, unsigned long v, int bytes)
{
	if (b->n + bytes > b->cap)
		b->s = xrealloc(b->s, b->cap = (b->n + bytes) * 2 + 64);
	for (int i = bytes - 1; i >= 0; i--)
		b->s[b->n++] = (unsigned char)(v >> (8 * i));
}

static void wbytes(struct wbuf *b, const void *p, size_t n)
{
	if (b->n + n > b->cap)
		b->s = xrealloc(b->s, b->cap = (b->n + n) * 2 + 64);
	memcpy(b->s + b->n, p, n);
	b->n += n;
}

static unsigned long wstr(struct wbuf *strs, const char *s)
{
	unsigned long at = strs->n;
	wbytes(strs, s, strlen(s) + 1);
	return at;
}

static int cmp_def(const void *a, const void *b)
{
	return strcmp(((const struct rdef *)a)->name, ((const struct rdef *)b)->name);
}

static int cmp_ext(const void *a, const void *b)
{
	return strcmp(((const struct rext *)a)->name, ((const struct rext *)b)->name);
}

#define LG_BUCKETS 378
#define LG_NONE 0xFFFFFFFFul

/* Whether reference type T is a long word of initialized data pointing into
 * the code (CODE) or elsewhere: libgen counts them per psect (the module's
 * two reference tables). */
static int data_pointer(unsigned t)
{
	return (t & 0x0020) == 0 && (t & 0x0018) == 0x0018 && (t & 0x00C0) == 0;
}

/* Writes libgen's format (type 1; reverse engineering of libgen's output,
 * see m68k-os9-libgen.md) to OUT. */
static void write_ucc(const char *out)
{
	struct rpsect *ps = NULL;
	int nps = 0;
	for (int i = 0; i < nmembers; i++)
		read_rofs(member_rof(i), &ps, &nps);

	struct wbuf strs = { 0 }, defs = { 0 }, psects = { 0 }, irefs = { 0 }, erefs = { 0 }, blocks = { 0 };
	unsigned long head[LG_BUCKETS];
	for (int i = 0; i < LG_BUCKETS; i++)
		head[i] = LG_NONE;
	wstr(&strs, out);

	/* the definitions, psect by psect, each psect's sorted by name */
	int ndefs = 0;
	struct { char *name; unsigned long index; } *defined = NULL;
	unsigned long *dhead = xmalloc((nps ? nps : 1) * sizeof *dhead);
	unsigned long *nameoff = xmalloc((nps ? nps : 1) * sizeof *nameoff);
	unsigned long **extoff = xmalloc((nps ? nps : 1) * sizeof *extoff);
	for (int p = 0; p < nps; p++) {
		struct rpsect *r = &ps[p];
		qsort(r->defs, r->ndefs, sizeof *r->defs, cmp_def);
		qsort(r->exts, r->nexts, sizeof *r->exts, cmp_ext);
		nameoff[p] = wstr(&strs, r->name);
		dhead[p] = r->ndefs ? (unsigned long)ndefs : LG_NONE;
		for (int i = 0; i < r->ndefs; i++, ndefs++) {
			unsigned bucket = 0;
			for (const unsigned char *c = (const unsigned char *)r->defs[i].name; *c; c++)
				bucket += *c;
			bucket %= LG_BUCKETS - 1;
			wput(&defs, r->defs[i].type, 2);
			wput(&defs, r->defs[i].value, 4);
			wput(&defs, wstr(&strs, r->defs[i].name), 4);
			wput(&defs, head[bucket], 4);
			wput(&defs, i + 1 < r->ndefs ? (unsigned long)ndefs + 1 : LG_NONE, 4);
			wput(&defs, p, 4);
			head[bucket] = ndefs;
			defined = xrealloc(defined, (ndefs + 1) * sizeof *defined);
			defined[ndefs].name = r->defs[i].name;
			defined[ndefs].index = ndefs;
		}
		extoff[p] = xmalloc((r->nexts ? r->nexts : 1) * sizeof **extoff);
		for (int i = 0; i < r->nexts; i++)
			extoff[p][i] = wstr(&strs, r->exts[i].name);
	}

	/* where the per-psect blocks start: after the tables */
	unsigned long nirefs = 0, nerefs = 0;
	for (int p = 0; p < nps; p++)
		for (int i = 0; i < ps[p].nexts; i++) {
			int found = 0;
			for (int d = 0; d < ndefs && !found; d++)
				found = strcmp(defined[d].name, ps[p].exts[i].name) == 0;
			if (found)
				nirefs++;
			else
				nerefs++;
		}
	unsigned long base = 46 + LG_BUCKETS * 4 + defs.n + strs.n + (unsigned long)nps * 66 + nirefs * 16 + nerefs * 16;

	unsigned long ii = 0, ei = 0;
	for (int p = 0; p < nps; p++) {
		struct rpsect *r = &ps[p];
		unsigned long codeoff = base + blocks.n;
		unsigned long body = r->code + r->idata + r->ridata + r->debug;
		wbytes(&blocks, r->body, body);
		int wide = r->series == 0xF9;
		unsigned long ihead = LG_NONE, ehead = LG_NONE;
		unsigned long codeptrs = 0, dataptrs = 0;
		/* references to the library's definitions, then to other names */
		for (int pass = 0; pass < 2; pass++) {
			struct wbuf *tab = pass == 0 ? &irefs : &erefs;
			unsigned long *count = pass == 0 ? &ii : &ei;
			unsigned long *headp = pass == 0 ? &ihead : &ehead;
			long last = -1;
			for (int i = 0; i < r->nexts; i++) {
				long def = -1;
				for (int d = 0; d < ndefs && def < 0; d++)
					if (strcmp(defined[d].name, r->exts[i].name) == 0)
						def = d;
				if ((pass == 0) != (def >= 0))
					continue;
				if (last >= 0) {
					/* the previous entry's next */
					unsigned char *n = tab->s + last * 16 + 8;
					unsigned long v = *count;
					n[0] = v >> 24, n[1] = v >> 16, n[2] = v >> 8, n[3] = v;
				} else {
					*headp = *count;
				}
				wput(tab, r->exts[i].nrefs, 4);
				wput(tab, base + blocks.n, 4);
				wput(tab, LG_NONE, 4);
				wput(tab, pass == 0 ? (unsigned long)def : extoff[p][i], 4);
				for (int k = 0; k < r->exts[i].nrefs; k++) {
					wput(&blocks, r->exts[i].refs[k].type, 2);
					wput(&blocks, r->exts[i].refs[k].offset, 4);
					if (data_pointer(r->exts[i].refs[k].type))
						dataptrs++;
				}
				last = (long)*count;
				(*count)++;
			}
		}
		unsigned long locoff = base + blocks.n;
		wput(&blocks, r->nlocals, wide ? 4 : 2);
		for (int k = 0; k < r->nlocals; k++) {
			wput(&blocks, r->locals[k].type, 2);
			wput(&blocks, r->locals[k].offset, 4);
			if (data_pointer(r->locals[k].type)) {
				if ((r->locals[k].type & 7) == 4)
					codeptrs++;
				else
					dataptrs++;
			}
		}
		wput(&blocks, 0, 4), wput(&blocks, 0, 4), wput(&blocks, 0, 4), wput(&blocks, 0, 4);

		wput(&psects, r->series, 2);
		wput(&psects, r->statics, 4);
		wput(&psects, r->idata, 4);
		wput(&psects, r->rstatics, 4);
		wput(&psects, r->ridata, 4);
		wput(&psects, r->code, 4);
		wput(&psects, r->debug, 4);
		wput(&psects, r->stack, 4);
		wput(&psects, 0, 4);
		wput(&psects, codeoff, 4);
		wput(&psects, locoff, 4);
		wput(&psects, dataptrs, 4);
		wput(&psects, codeptrs, 4);
		wput(&psects, ihead, 4);
		wput(&psects, ehead, 4);
		wput(&psects, nameoff[p], 4);
		wput(&psects, dhead[p], 4);
	}

	/* the header: id, format 1 for the 68000 family, date, edition */
	struct wbuf lib = { 0 };
	wput(&lib, 0x2D00D5BC, 4);
	wput(&lib, 0xFF01, 2);
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	wput(&lib, t->tm_year, 1), wput(&lib, t->tm_mon + 1, 1), wput(&lib, t->tm_mday, 1);
	wput(&lib, t->tm_hour, 1), wput(&lib, t->tm_min, 1), wput(&lib, t->tm_sec, 1);
	wput(&lib, st.edition != NULL ? strtoul(st.edition, NULL, 0) : 1, 2);
	wput(&lib, 0, 4);
	wput(&lib, LG_BUCKETS * 4, 4);
	wput(&lib, defs.n, 4);
	wput(&lib, strs.n, 4);
	wput(&lib, psects.n, 4);
	wput(&lib, irefs.n, 4);
	wput(&lib, erefs.n, 4);
	wput(&lib, 0, 1), wput(&lib, st.mts ? 2 : 0, 1), wput(&lib, 0, 2);
	for (int i = 0; i < LG_BUCKETS; i++)
		wput(&lib, head[i], 4);
	wbytes(&lib, defs.s, defs.n);
	wbytes(&lib, strs.s, strs.n);
	wbytes(&lib, psects.s, psects.n);
	wbytes(&lib, irefs.s, irefs.n);
	wbytes(&lib, erefs.s, erefs.n);
	if (lib.n != base)
		error("internal error: library layout");
	wbytes(&lib, blocks.s, blocks.n);

	char *temp = temp_beside(out);
	FILE *fp = fopen(temp, "wb");
	if (fp == NULL || fwrite(lib.s, 1, lib.n, fp) != lib.n || fclose(fp) != 0)
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
		/* (not in Ultra C 1.x's older header, $2D00D5BB) */
		if (h[3] == 0xBC)
			fprintf(listing, "Threads:         %s\n", h[43] == 2 ? "safe" : "none");
	}
	if (fp != NULL)
		fclose(fp);
	fputc('\n', listing);
}

int main(int argc, char **argv)
{
	os9_who = "m68k-os9-libgen";
	static const char *const own_names[] = { "libgen", NULL };
	os9_tools_init(argv[0], own_names);
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
	default: write_ucc(out); break;
	}
	os9_cleanup();
	return 0;
}
