/* m68k-os9-r68: Microware's assembler r68, with GNU tools.
 *
 * A drop-in for makefiles that call r68: the same options (Ultra C's, a
 * superset of C 3.2's), translating the source with r68togas and assembling
 * it with GNU as in its MRI mode; the object is ELF (-obj=elf, the default),
 * a ROF (-obj=rof, -obj=ucc, through elf2rof) or, with -obj=gas, the
 * translated source itself. See m68k-os9-r68.md.
 *
 * MIT License; see elf2mod.c.
 */

#include "hostsys.h"
#include "os9conv.h"
#include "os9link.h"
#include "r68x_note.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libiberty.h>

#define AS	"m68k-elfos9-as"
#define READELF	"m68k-elfos9-readelf"
#define R68TOGAS "r68togas"
#define ELF2ROF	"elf2rof"
#define NM	"m68k-elfos9-nm"
#define OBJCOPY	"m68k-elfos9-objcopy"
#define OBJDUMP	"m68k-elfos9-objdump"

#define error os9_error

#define OBJ_KINDS ((1u << OBJ_ELF) | (1u << OBJ_ROF) | (1u << OBJ_UCC))

/* ----- Options ----- */

enum kind { FLAG, VALUE, OPTVALUE, NUMBER };

enum id {
	O_A, O_B, O_BT, O_C, O_D, O_E, O_F, O_G, O_L, O_J, O_M, O_N, O_O, O_P, O_Q, O_R, O_S, O_U, O_V,
	O_X, O_Y, O_Z,
};

struct option {
	const char *name;	/* lowercase, without the '-'; matched in any case */
	enum kind kind;
	const char *arg;
	const char *help;	/* what r68 does, in our words */
	const char *does;	/* what this program does, when that differs */
	enum id id;
};

/* In the order of Ultra C's r68 -?. */
static const struct option options[] = {
	{ "a", VALUE, "=<sym>[=<val>]", "Define a symbol (1 without a value)", NULL, O_A },
	{ "b", FLAG, "", "Optimize branch sizes", "as always does", O_B },
	{ "bt", FLAG, "", "Optimize branch sizes, long to externals", "as sizes them", O_BT },
	{ "c", FLAG, "", "List false conditionals too", NULL, O_C },
	{ "d", NUMBER, "<n>", "Lines per listing page", "ignored", O_D },
	{ "e", FLAG, "", "Don't print errors", "ignored: as prints them", O_E },
	{ "f", FLAG, "", "Form feeds for page ejects", "ignored", O_F },
	{ "g", FLAG, "", "List all code bytes", NULL, O_G },
	{ "l", FLAG, "", "Listing to standard output", NULL, O_L },
	{ "j", FLAG, "", "Long branches through a jump table (-m0)", "elf2mod's jump table does it", O_J },
	{ "m", NUMBER, "<n>", "CPU: 0 68000, 1 68010, 2 68020/CPU32, 3 68030, 4 68040, 6 68060", NULL, O_M },
	{ "n", FLAG, "", "No line numbers in the listing", "ignored", O_N },
	{ "o", OPTVALUE, "[=<path>]", "Object file (output.r without a path)", NULL, O_O },
	{ "p", NUMBER, "<n>", "Align orgs to <n> (2, 4, 8, 16)", "ignored", O_P },
	{ "q", FLAG, "", "Quiet: no warnings or messages", NULL, O_Q },
	{ "r", FLAG, "", "Registers must be written with %", "not supported", O_R },
	{ "s", FLAG, "", "Symbol table in the listing", NULL, O_S },
	{ "u", VALUE, "[=]<dir>", "Search <dir> for use files", NULL, O_U },
	{ "v", FLAG, "", "Print the version", NULL, O_V },
	{ "x", FLAG, "", "Macro expansions in the listing", NULL, O_X },
	{ "y", FLAG, "", "All branches long", "not supported", O_Y },
	{ "z", OPTVALUE, "[=<path>]", "File names and options from standard input or <path>", NULL, O_Z },
};
#define NOPTIONS (sizeof options / sizeof options[0])

static struct {
	struct args defs;	/* -a=, as r68togas -a */
	struct args dirs;	/* -u=, as r68togas -u */
	int cpu;		/* -m */
	int cpu_given;		/* -m given: r68 pads differently without it */
	int branch_sizing;	/* -b, -bt */
	int listing, symbols, conditionals, all_bytes, macros;
	const char *out;	/* -o= ("output.r" for a bare -o); null: none */
	struct args files;
	struct args notes;	/* options ignored, for one note */
	const char *dialect;	/* -dialect=: r68togas -d; null: its default */
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
	printf("%s [opts] <file> [opts]\n", os9_who);
	printf("Options of Microware's r68, with GNU tools (any case):\n");
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
	usage_line("-sysl=<lib>", "the library whose equates are folded in (default CLIB's sys.l;\n"
		"                      none: none), as rof2elf -e folds them into ROFs");
	usage_line("-dialect=<d>", "whose encodings and padding where Microware's assemblers\n"
		"                      differ: ucc (Ultra C's r68, the default) or c32 (C 3.2's R68)");
	usage_line("-obj=<kind>", "what -o writes: elf (default), rof or ucc (a ROF), gas (the\n"
		"                      translated source, for m68k-os9-as; standard output\n"
		"                      without -o)");
	printf("Environment:\n");
	usage_line("M68K_OS9_ELF", "obj=elf, rof or ucc as -obj= (gas only on the command line)");
	usage_line("MWOS", "use <file> also looks in $MWOS/OS9/SRC/DEFS");
}

/* -sysl=: the library whose equates are folded in ("none": none; default
 * the sys.l of CLIB). */
static const char *sysl;

/* ----- Parsing ----- */

static void parse(int argc, char **argv);

/* -z[=PATH]: words, file names and options. */
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

static void note_ignored(const struct option *o, const char *value)
{
	args_addf(&st.notes, "-%s%s", o->name, value != NULL ? value : "");
}

/* --x: r68 turns flag x off again (measured 2026-10-10: -bt --bt, --b, -q --q,
 * -y --y; the last one given wins). Makefiles pass -bt and --bt (xcc
 * -as=-bt), which leaves branch sizing off. */
static int flag_off;

static void apply(const struct option *o, const char *value)
{
	int on = !flag_off;
	switch (o->id) {
	case O_A: args_add(&st.defs, "-a"); args_add(&st.defs, value); break;
	case O_B: case O_BT: st.branch_sizing = on; break;
	case O_C: st.conditionals = on; break;
	case O_D: case O_E: case O_F: case O_N: case O_P: if (on) note_ignored(o, value); break;
	case O_G: st.all_bytes = on; break;
	case O_L: st.listing = on; break;
	case O_J: if (on) note_ignored(o, value); break;
	case O_M:
		if (strlen(value) != 1 || strchr("012346", value[0]) == NULL)
			error("-m%s: the CPU is 0, 1, 2, 3, 4 or 6", value);
		st.cpu = value[0] - '0';
		st.cpu_given = 1;
		break;
	case O_O: st.out = value != NULL ? value : "output.r"; break;
	case O_Q: os9_quiet = on; break;
	case O_R: if (on) error("-r (registers written with %%) isn't supported"); break;
	case O_S: st.symbols = on; break;
	case O_U: args_add(&st.dirs, "-u"); args_add(&st.dirs, value); break;
	case O_V:
		printf("%s: Microware r68's options; r68togas and GNU as (%s) assemble\n", os9_who, AS);
		break;
	case O_X: st.macros = on; break;
	case O_Y: if (on) error("-y (all branches long) isn't supported"); break;
	case O_Z: read_z(value); break;
	}
}

static void option(const char *arg)
{
	/* --x: the flag x off (see flag_off); options with values the same */
	const char *text = arg[1] == '-' && arg[2] != '\0' ? arg + 2 : arg + 1;
	flag_off = text == arg + 2;
	const struct option *best = NULL;
	const char *value = NULL;
	size_t bestlen = 0;

	if (strcmp(text, "?") == 0) {
		usage();
		os9_cleanup();
		exit(0);
	}
	if (ci_prefix(text, "sysl=")) {
		sysl = text + 5;
		return;
	}
	if (ci_prefix(text, "dialect=")) {
		if (!ci_eq(text + 8, "c32") && !ci_eq(text + 8, "ucc"))
			error("-dialect= takes c32 or ucc");
		st.dialect = ci_eq(text + 8, "c32") ? "c32" : "ucc";
		return;
	}
	if (ci_prefix(text, "obj=") && ci_eq(text + 4, "r68"))
		error("-obj=r68: the input is r68 already");
	if (os9conv_option(arg, OBJ_KINDS | (1u << OBJ_GAS)))
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
		} else if (o->kind == VALUE || o->kind == NUMBER) {
			/* -u=dir and -udir; -m2, -m=2 */
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
	if (best == NULL) {
		/* Single-letter flags together, as r68 takes them (-qb). */
		/* the flags at its start; the rest named in the note (Ultra
		 * C's r68 takes -qXYZ123) */
		const char *c;
		for (c = text; *c; c++) {
			const struct option *o = NULL;
			for (size_t i = 0; i < NOPTIONS; i++)
				if (options[i].kind == FLAG && options[i].name[1] == '\0' &&
				    options[i].name[0] == tolower((unsigned char)*c))
					o = &options[i];
			if (o == NULL)
				break;
			apply(o, NULL);
		}
		if (c == text)
			error("unknown option %s (%s -? for the usage)", arg, os9_who);
		if (*c)
			args_addf(&st.notes, "%s (from %s)", c, arg);
		return;
	}
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

/* ----- Assembling ----- */

/* The psect name of ELF object PATH: its file symbol (r68togas's epilogue
 * writes the psect's name there), else FALLBACK (allocated). */
static char *psect_name(const char *path, const char *fallback)
{
	struct args argv = { 0 };
	args_add(&argv, READELF);
	args_add(&argv, "-s");
	args_add(&argv, path);
	char *out = os9_temp(".out");
	int show = os9_show;
	os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	os9_show = show;
	args_free(&argv);
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	char line[2048];
	while (fp != NULL && fgets(line, sizeof line, fp) != NULL) {
		char type[32], name[512], bind[32], vis[32], ndx[32];
		unsigned long n, value, size;
		if (sscanf(line, "%lu: %lx %lu %31s %31s %31s %31s %511s", &n, &value, &size, type, bind, vis, ndx, name) == 8
			&& strcmp(type, "FILE") == 0) {
			fclose(fp);
			return xstrdup(name);
		}
	}
	if (fp != NULL)
		fclose(fp);
	return xstrdup(fallback);
}

/* Symbols named like a GNU as register (val, tc, ...) are NAME__r68 in
 * r68togas's output, since as can't have a symbol of that name; the
 * object's global and undefined ones get their names back. */
static void register_names(const char *object)
{
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, "-g");
	args_add(&argv, object);
	char *out = os9_temp(".out");
	int show = os9_show;
	os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	os9_show = show;
	args_free(&argv);
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	if (fp == NULL)
		error("%s %s failed", NM, object);
	struct args renames = { 0 };
	char line[1024];
	while (fgets(line, sizeof line, fp) != NULL) {
		char *name = strrchr(line, ' ');
		name = name != NULL ? name + 1 : line;
		name[strcspn(name, "\r\n")] = '\0';
		size_t n = strlen(name);
		if (n > 5 && strcmp(name + n - 5, "__r68") == 0 && strncmp(name, "r68__", 5) != 0) {
			/* and a dot in the name was __dot__ (see r68togas's put_symbol) */
			char *orig = xstrdup(name), *p;
			orig[n - 5] = '\0';
			while ((p = strstr(orig, "__dot__")) != NULL) {
				*p = '.';
				memmove(p + 1, p + 7, strlen(p + 7) + 1);
			}
			args_add(&renames, "--redefine-sym");
			args_addf(&renames, "%s=%s", name, orig);
			free(orig);
		}
	}
	fclose(fp);
	if (renames.n == 0)
		return;
	args_add(&argv, OBJCOPY);
	args_append(&argv, &renames);
	args_add(&argv, object);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("renaming symbols in %s failed", object);
	args_free(&argv);
	args_free(&renames);
}

/* The equates of sys.l (or -sysl=) as NAME VALUE lines in a temporary
 * file, for r68togas -e; null if there is no library. r68 leaves them
 * external and l68 takes them from sys.l at link time; as can't add two
 * externals (F_ONE+F_TWO) or test one in a conditional, so they are
 * folded in here, as rof2elf -e folds them into a ROF's code. */
static char *equates(void)
{
	const char *lib = sysl != NULL ? (ci_eq(sysl, "none") ? NULL : sysl) : os9conv_sys_l();
	if (lib == NULL)
		return NULL;
	char *elf = os9conv_as_elf(lib, 1);
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, elf);
	char *out = os9_temp(".out");
	int show = os9_show;
	os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	os9_show = show;
	args_free(&argv);
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	if (fp == NULL)
		error("%s %s failed", NM, elf);
	char *file = os9_temp(".equ");
	FILE *eq = fopen(file, "w");
	if (eq == NULL)
		error("cannot write %s", file);
	char line[1024], value[64], type[8], name[512];
	while (fgets(line, sizeof line, fp) != NULL)
		if (sscanf(line, "%63s %7s %511s", value, type, name) == 3 && strcmp(type, "A") == 0)
			fprintf(eq, "%s $%s\n", name, value);
	fclose(fp);
	fclose(eq);
	free(elf);
	return file;
}

/* Whether the assembler sizes a branch to a global label of the same
 * file (the binutils change for m68k-elfos9; without it as keeps a
 * relocation, and r68togas -b mustn't use jbra there): a probe. */
static int relaxes_globals(void)
{
	char *src = os9_temp(".s"), *obj = os9_temp(".o"), *out = os9_temp(".out");
	FILE *fp = fopen(src, "w");
	if (fp == NULL)
		return 0;
	fputs(" .globl x\nx: nop\n jbra x\n", fp);
	fclose(fp);
	struct args argv = { 0 };
	args_add(&argv, AS);
	args_add(&argv, "-o");
	args_add(&argv, obj);
	args_add(&argv, src);
	int show = os9_show;
	os9_show = 0;
	int ok = os9_run(&argv, NULL, NULL) == 0;
	args_free(&argv);
	if (ok) {
		args_add(&argv, "m68k-elfos9-size");
		args_add(&argv, obj);
		ok = os9_run(&argv, out, NULL) == 0;
		args_free(&argv);
	}
	os9_show = show;
	int text = 0;
	FILE *o = ok ? fopen(out, "r") : NULL;
	char line[256];
	while (o != NULL && fgets(line, sizeof line, o) != NULL)
		if (isdigit((unsigned char)line[strspn(line, " \t")]))
			text = atoi(line);
	if (o != NULL)
		fclose(o);
	/* nop and bra.s: 4 bytes; with a relocation, bra.w: 6 or jmp: 8 */
	return text == 4;
}

/* The lines of command ARGV's standard output, in a list (empty if it
 * fails: the caller's next step reports it). */
static struct args output_lines(struct args *argv)
{
	struct args lines = { 0 };
	char *out = os9_temp(".out");
	int show = os9_show;
	os9_show = 0;
	/* its messages discarded (readelf -p warns of a section not there) */
	int status = os9_run(argv, out, os9_temp(".err"));
	os9_show = show;
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	if (fp == NULL)
		return lines;
	char line[1024];
	while (fgets(line, sizeof line, fp) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		args_add(&lines, line);
	}
	fclose(fp);
	return lines;
}

/* Undefined symbols that no relocation uses are removed: the Microware
 * assemblers write an external reference only where one is used, and a
 * link takes in a library member for any undefined symbol (an equate of an
 * external, Size2 equ Size1, or the twin of a label a conditional left
 * out, lab__r68pc equ lab, leave one in as's object). */
static void unreferenced_externals(const char *object)
{
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, "-u");
	args_add(&argv, object);
	struct args undefined = output_lines(&argv);
	args_free(&argv);
	if (undefined.n == 0)
		return;
	args_add(&argv, OBJDUMP);
	args_add(&argv, "-r");
	args_add(&argv, object);
	struct args relocs = output_lines(&argv);
	args_free(&argv);
	/* the terms of link-time expressions: used through .r68x's notes, kept
	 * (as rof2elf keeps them) */
	args_add(&argv, READELF);
	args_add(&argv, "-p");
	args_add(&argv, R68X_SECTION);
	args_add(&argv, object);
	struct args notes = output_lines(&argv);
	args_free(&argv);
	struct args strip = { 0 };
	for (int i = 0; i < undefined.n; i++) {
		/* "         U name" */
		const char *name = strrchr(undefined.v[i], ' ');
		name = name != NULL ? name + 1 : undefined.v[i];
		if (*name == '\0')
			continue;
		size_t n = strlen(name);
		int used = 0;
		for (int j = 0; j < relocs.n && !used; j++) {
			/* "OFFSET TYPE VALUE", VALUE the symbol and an addend */
			const char *v = strrchr(relocs.v[j], ' ');
			if (v == NULL || strncmp(relocs.v[j], "RELOCATION", 10) == 0)
				continue;
			v++;
			used = strncmp(v, name, n) == 0 && (v[n] == '\0' || v[n] == '+' || v[n] == '-');
		}
		for (int j = 0; j < notes.n && !used; j++) {
			/* "  [  0]  r68x_xxxxxxxx=-lab_b+lab_a\n..." */
			for (const char *p = strstr(notes.v[j], name); p != NULL && !used; p = strstr(p + 1, name)) {
				char before = p == notes.v[j] ? ' ' : p[-1], after = p[n];
				used = (before == '=' || before == '+' || before == '-' || before == '*' || before == ' ') &&
					(after == '\0' || after == '+' || after == '-' || after == '\\' || after == ' ');
			}
		}
		if (!used)
			args_addf(&strip, "--strip-symbol=%s", name);
	}
	if (strip.n > 0) {
		args_add(&argv, OBJCOPY);
		args_append(&argv, &strip);
		args_add(&argv, object);
		if (os9_run(&argv, NULL, NULL) != 0)
			error("removing unreferenced symbols from %s failed", object);
		args_free(&argv);
	}
	args_free(&strip);
	args_free(&undefined);
	args_free(&relocs);
	args_free(&notes);
}

/* The sections' alignment as the Microware assemblers pad a psect's
 * (r68togas pads them, see its pad_alignment): 2 for C 3.2's R68
 * (-dialect=c32) and Ultra C's r68 -m0, so that the linker places the next
 * psect's after it, as l68 does; as gives them 4, the other dialect's. */
static void section_alignment(const char *object)
{
	int c32 = st.dialect != NULL && strcmp(st.dialect, "c32") == 0;
	if (!c32 && !(st.cpu_given && st.cpu == 0))
		return;
	static const char *const sections[] = { ".text", ".data", ".bss", ".remote.data", ".remote.bss", NULL };
	struct args argv = { 0 };
	args_add(&argv, OBJCOPY);
	for (const char *const *s = sections; *s; s++) {
		args_add(&argv, "--set-section-alignment");
		args_addf(&argv, "%s=2", *s);
	}
	args_add(&argv, object);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("setting the sections' alignment in %s failed", object);
	args_free(&argv);
}

/* GNU as's options for r68's -m<n>: the CPU, with the FPU (r68 takes its
 * instructions for any CPU). */
static void cpu_options(struct args *argv)
{
	static const char *const cpus[] = { "-m68000", "-m68010", "-m68020", "-m68030", "-m68040", NULL, "-m68060" };
	args_add(argv, cpus[st.cpu]);
	if (st.cpu < 4)
		args_add(argv, "-m68881");
}

/* GNU as's listing option for r68's listing options: -a with c (leave out
 * false conditionals; r68 lists them with -c), m (macro expansions, -x), s
 * (the symbols, -s); -l alone lists the source and code. */
static void listing_options(struct args *argv)
{
	if (!st.listing && !st.symbols)
		return;
	char opt[16] = "-a";
	if (st.listing)
		strcat(opt, "hl");
	if (!st.conditionals)
		strcat(opt, "c");
	if (st.macros)
		strcat(opt, "m");
	if (st.symbols)
		strcat(opt, "s");
	args_add(argv, opt);
	if (st.all_bytes)
		args_add(argv, "--listing-cont-lines=100000");
}

static void assemble(const char *source)
{
	const char *out = st.out;
	int gas = os9conv.obj == OBJ_GAS;
	if (out != NULL) {
		char *a = host_abspath(out), *b = host_abspath(source);
		int same = strcmp(a, b) == 0;
		free(a);
		free(b);
		if (same)
			error("%s: won't write over the source", out);
	}

	/* 1. The translation: to the output for -obj=gas (standard output
	 * without -o), else to a temporary file. */
	struct args argv = { 0 };
	args_add(&argv, R68TOGAS);
	args_append(&argv, &st.defs);
	if (st.cpu_given)
		args_addf(&argv, "-m%d", st.cpu);
	if (st.dialect != NULL) {
		args_add(&argv, "-d");
		args_add(&argv, st.dialect);
	}
	if (st.branch_sizing) {
		args_add(&argv, "-b");
		if (relaxes_globals())
			args_add(&argv, "-B");
	}
	args_append(&argv, &st.dirs);
	char *eqf = equates();
	if (eqf != NULL) {
		args_add(&argv, "-e");
		args_add(&argv, eqf);
	}
	char *translated = NULL;
	if (!gas || out != NULL) {
		translated = gas ? (char *)out : os9_temp(".s");
		args_add(&argv, "-o");
		args_add(&argv, translated);
	}
	args_add(&argv, source);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("translating %s failed", source);
	args_free(&argv);
	if (gas)
		return;

	/* 2. GNU as: the object, ELF; a temporary one for a ROF, or for
	 * only checking (no -o). */
	int rof = os9conv.obj == OBJ_ROF || os9conv.obj == OBJ_UCC;
	char *object = out != NULL && !rof ? (char *)out : os9_temp(".o");
	args_add(&argv, AS);
	cpu_options(&argv);
	if (os9_quiet)
		args_add(&argv, "-W");
	listing_options(&argv);
	args_add(&argv, "-o");
	args_add(&argv, object);
	args_add(&argv, translated);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("assembling %s failed", source);
	args_free(&argv);

	register_names(object);
	unreferenced_externals(object);
	section_alignment(object);

	/* 3. A ROF, named for the psect. */
	if (out != NULL && rof) {
		char *stem = path_stem(source, 0);
		char *name = psect_name(object, stem);
		args_add(&argv, ELF2ROF);
		args_add(&argv, "-n");
		args_add(&argv, name);
		args_add(&argv, "-o");
		args_add(&argv, out);
		args_add(&argv, object);
		if (os9_run(&argv, NULL, NULL) != 0)
			error("converting %s to ROF failed", source);
		args_free(&argv);
		free(name);
		free(stem);
	}
}

int main(int argc, char **argv)
{
	os9_who = "m68k-os9-r68";
	os9conv_environment(OBJ_KINDS);
	if (argc < 2) {
		usage();
		return 0;
	}
	parse(argc, argv);
	if (st.files.n == 0)
		error("no source file (%s -? for the usage)", os9_who);
	if (st.files.n > 1)
		error("one source file at a time, as r68 (%s and %s)", st.files.v[0], st.files.v[1]);
	if (st.notes.n > 0 && !os9_quiet) {
		fprintf(stderr, "%s: ignored:", os9_who);
		for (int i = 0; i < st.notes.n; i++)
			fprintf(stderr, " %s", st.notes.v[i]);
		fprintf(stderr, " (see %s -?)\n", os9_who);
	}
	assemble(st.files.v[0]);
	os9_cleanup();
	return 0;
}
