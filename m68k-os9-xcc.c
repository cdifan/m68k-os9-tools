/* m68k-os9-xcc: Microware C 3.2's xcc/cc options, run with GCC.
 *
 * A drop-in for makefiles written for Microware C 3.2's xcc (and Ultra C's
 * xcc -mode=compat): the same options, carried out by m68k-elfos9-gcc,
 * rof2elf, GNU ld and elf2mod, with the user's own copies of Microware's
 * cstart.r and libraries (CLIB) and headers (CDEF), converted on demand.
 * See m68k-os9-xcc.md. m68k-os9-ucc is the same program for Ultra C's own
 * options (-mode=ucc), with Ultra C's MWOS tree, libraries and calling
 * convention (-mos9call=ucc).
 *
 * MIT License; see elf2mod.c.
 */

#include "hostsys.h"
#include "os9conv.h"
#include "os9link.h"
#include "asmsyntax.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libiberty.h>

#define GCC	os9_gnu("gcc")
#define ELF2ROF	os9_tool("elf2rof")
#define GASTOR68 os9_tool("gastor68")
#define R68	os9_front("r68")
#define ROF2ELF	os9_tool("rof2elf")


/* ----- Options ----- */

enum kind {
	FLAG,		/* -x */
	VALUE,		/* -e=5, also -e5 */
	JOIN,		/* -dNAME, also -d=NAME */
	OPTVALUE,	/* -r, -r=DIR */
};

enum id {
	O_A, O_BG, O_BP, O_C, O_CS, O_D, O_E, O_F, O_FD, O_G, O_H, O_I, O_J, O_K,
	O_L, O_LO, O_M, O_N, O_NL, O_NV, O_O, O_Q, O_R, O_S, O_T, O_TQ, O_TO, O_TP,
	O_U, O_V, O_W, O_X, O_Z,
};

#define COMPAT	1	/* C 3.2's options (Ultra C's -mode=compat) */
#define UCOMPAT	2	/* only in Ultra C's -mode=compat */

struct option {
	const char *name;	/* lowercase, without the '-' */
	enum kind kind;
	const char *arg;	/* the value, in the usage */
	const char *help;	/* what the option does, in our words */
	const char *does;	/* what this program does, when that differs */
	int id;			/* enum id, or enum uid in Ultra C's table */
	int modes;
};

/* In the order of C 3.2's usage; the usage is made from this table. */
static const struct option options[] = {
	{ "a", FLAG, "", "Stop after compiling; write assembler source", "r68 syntax (gastor68); -obj=gas: GNU as syntax", O_A, COMPAT },
	{ "bg", FLAG, "", "Make a sticky (adhesive) module", "not supported", O_BG, COMPAT },
	{ "bp", FLAG, "", "Print each command before running it", NULL, O_BP, COMPAT },
	{ "c", FLAG, "", "Comments in assembler output", "ignored", O_C, COMPAT },
	{ "cs", VALUE, "=<name>", "Link with this root psect instead of cstart.r", NULL, O_CS, COMPAT },
	{ "d", JOIN, "<name>", "Define a preprocessor macro", NULL, O_D, COMPAT },
	{ "e", VALUE, "=<n>", "Module edition", NULL, O_E, COMPAT },
	{ "f", VALUE, "=<path>", "Output file (in execution directory)", "path as given", O_F, COMPAT },
	{ "fd", VALUE, "=<path>", "Output file (in data directory)", "path as given", O_FD, COMPAT },
	{ "g", FLAG, "", "Generate source-level debug information", "elf2mod -g writes .stb, no .dbg", O_G, COMPAT },
	{ "h", FLAG, "", "Print commands without running them", NULL, O_H, UCOMPAT },
	{ "i", FLAG, "", "Link with shared C I/O module (cio)", "links cio.l before C library", O_I, COMPAT },
	{ "j", FLAG, "", "No jump table for far calls", "fails if program needs one", O_J, COMPAT },
	{ "k", VALUE, "=<n>[W|L][CW|CL][F]", "Processor: 0 for 68000, 2 for 68020; W/L for word/long data offsets,\n"
		"CW/CL for word/long code offsets; F for 68881", "L and CL: unsupported", O_K, COMPAT },
	{ "l", VALUE, "=<name>", "Also search this library", NULL, O_L, COMPAT },
	{ "lo", VALUE, "=<opts>", "Pass options on to the linker", "not supported", O_LO, COMPAT },
	{ "m", VALUE, "=<n>[K]", "Extra stack for module, in KB", "in K with or without K, as l68's -M=", O_M, COMPAT },
	{ "n", VALUE, "=<name>", "Module name (default: unsuffixed output file name)", NULL, O_N, COMPAT },
	{ "nl", FLAG, "", "Don't link default libraries", NULL, O_NL, COMPAT },
	{ "nv", FLAG, "", "No vsect directives (OS-9000 only)", "ignored", O_NV, COMPAT },
	{ "o", OPTVALUE, "[=<n>]", "Optimization level, 0 to 2 (0 without <n>)", "0: -O0, 1 (the default): -Os, 2: -O2", O_O, COMPAT },
	{ "q", FLAG, "", "No progress messages", NULL, O_Q, COMPAT },
	{ "r", OPTVALUE, "[=<dir>]", "Stop at object files (.r), in <dir> if given", "ELF in .r files; see -obj", O_R, COMPAT },
	{ "s", FLAG, "", "Leave out stack checks", NULL, O_S, COMPAT },
	{ "t", VALUE, "=<dir>", "Put temporary files in <dir>", NULL, O_T, COMPAT },
	{ "t?", FLAG, "", "List target systems and processors", NULL, O_TQ, COMPAT },
	{ "to", VALUE, "=<name>", "Operating system to compile for", "OSK only", O_TO, COMPAT },
	{ "tp", VALUE, "=<n>[..]", "Target processor, with options", "68k, 020 (with 68881), cpu32", O_TP, COMPAT },
	{ "u", JOIN, "<name>", "Undefine a predefined macro", NULL, O_U, COMPAT },
	{ "v", VALUE, "=<dir>", "Also search <dir> for #include files", "before CDEF", O_V, COMPAT },
	{ "w", VALUE, "=<dir>", "Directory of default libraries", "replaces CLIB", O_W, COMPAT },
	{ "x", FLAG, "", "Floating point through math trap module", "-mos9math", O_X, COMPAT },
	{ "z", OPTVALUE, "[=<file>]", "Read options and file names from <file>, or standard input", NULL, O_Z, COMPAT },
};
#define NOPTIONS (sizeof options / sizeof options[0])

/* Ultra C's own options (-mode=ucc, m68k-os9-ucc), in the order of its
   usage; what each does was measured with xcc -h -b (2026-10-17). */
enum uid {
	U_A, U_B, U_BC, U_C, U_CO, U_CQ, U_CS, U_CW, U_CX, U_D, U_E, U_EDGX, U_F, U_FD, U_G,
	U_H, U_I, U_J, U_K, U_L, U_M, U_MT, U_MW, U_N, U_O, U_P, U_CPP, U_R, U_S, U_SL,
	U_TQ, U_T, U_TD, U_TO, U_TP, U_U, U_V, U_W, U_X, U_Y, U_Z, U_PHASE, U_OL,
};

static const struct option ucc_options[] = {
	{ "fe", VALUE, "=<opt>", "Pass an option to the front end", "not supported", U_PHASE, 0 },
	{ "il", VALUE, "=<opt>", "Pass an option to the I-code linker", "not supported", U_PHASE, 0 },
	{ "io", VALUE, "=<opt>", "Pass an option to the I-code optimizer", "not supported", U_PHASE, 0 },
	{ "be", VALUE, "=<opt>", "Pass an option to the back end", "not supported", U_PHASE, 0 },
	{ "ao", VALUE, "=<opt>", "Pass an option to the assembly optimizer", "not supported", U_PHASE, 0 },
	{ "as", VALUE, "=<opt>", "Pass an option to the assembler", "not supported", U_PHASE, 0 },
	{ "pl", VALUE, "=<opt>", "Pass an option to the C++ prelinker", "not supported", U_PHASE, 0 },
	{ "ol", VALUE, "=<opt>", "Pass an option to the linker", "l68 -<opt>", U_OL, 0 },
	{ "a", OPTVALUE, "[=warn|err]", "ANSI conformant source mode", "-std=c89 -pedantic[-errors]", U_A, 0 },
	{ "b", FLAG, "", "Show the phases' command lines", NULL, U_B, 0 },
	{ "bc", FLAG, "", "K&R compatible source mode", "-std=gnu89; cstart.r and K&R libraries", U_BC, 0 },
	{ "c", FLAG, "", "Constant pointers in the code area", "ignored", U_C, 0 },
	{ "co", FLAG, "", "C code as comments in assembly code", "-fverbose-asm", U_CO, 0 },
	{ "cq", FLAG, "", "Allow C++ comments in C code", "GNU C allows them", U_CQ, 0 },
	{ "cs", VALUE, "=<file>", "Alternate root psect for the link", NULL, U_CS, 0 },
	{ "cw", FLAG, "", "Warnings from the compiler phases", "-Wall", U_CW, 0 },
	{ "cx", OPTVALUE, "[=<file>]", "Extended cross reference file", "not supported", U_CX, 0 },
	{ "d", JOIN, "<name>[=<value>]", "Define a name for the preprocessor", NULL, U_D, 0 },
	{ "e", VALUE, "=<phase>[=<dir>]", "End after a phase, output in <dir>",
		"as: objects; be, ao: assembler (GNU as syntax); others: not supported", U_E, 0 },
	{ "edgx", FLAG, "", "Edison's extended ANSI C++ mode", "not supported", U_EDGX, 0 },
	{ "f", VALUE, "=<path>", "Output pathname (execution directory)", "path as given", U_F, 0 },
	{ "fd", VALUE, "=<path>", "Output pathname (data directory)", "path as given", U_FD, 0 },
	{ "g", FLAG, "", "Source level debugging information", "-g, and l68 -g (.stb)", U_G, 0 },
	{ "h", FLAG, "", "Don't execute the compiler phases", NULL, U_H, 0 },
	{ "i", FLAG, "", "Link to use the C shared library module", "csl.l and os_csl.l", U_I, 0 },
	{ "j", FLAG, "", "Default libraries in the I-code link", "not supported", U_J, 0 },
	{ "k", FLAG, "", "No default libraries (nor acstart.r) in the link", NULL, U_K, 0 },
	{ "l", VALUE, "=<name>", "Search this library too", NULL, U_L, 0 },
	{ "m", VALUE, "=<phase>=<n>[k]", "Stack space for a phase", "ignored", U_M, 0 },
	{ "mt", OPTVALUE, "[=<op>,...]", "Multi-threaded code", "none only", U_MT, 0 },
	{ "mw", VALUE, "=<dir>", "Location of the MWOS directory structure", NULL, U_MW, 0 },
	{ "n", FLAG, "", "Layout data area", "ignored", U_N, 0 },
	{ "o", OPTVALUE, "[=<n>]", "Optimization level 0-7 (0 without <n>)", "0: -O0, 1-3: -O1, 4-7: -O2", U_O, 0 },
	{ "p", OPTVALUE, "", "Preprocess (to standard output)", "-E", U_P, 0 },
	{ "qb", FLAG, "", "C++: cfront 2.1 compatibility", "not supported", U_CPP, 0 },
	{ "qc", FLAG, "", "C++: cfront 3.0 compatibility", "not supported", U_CPP, 0 },
	{ "qi", FLAG, "", "C++: implicit #include instantiation", "not supported", U_CPP, 0 },
	{ "qnx", FLAG, "", "C++: no exception handling", "not supported", U_CPP, 0 },
	{ "qp", FLAG, "", "C++: every .c in C++ mode", "not supported", U_CPP, 0 },
	{ "qt", VALUE, "=<mode>", "C++: instantiation mode", "not supported", U_CPP, 0 },
	{ "r", FLAG, "", "Omit stack checking code", "no -mos9stkchk", U_R, 0 },
	{ "s", VALUE, "=<n>", "Weight of space", "-Os when above time's", U_S, 0 },
	{ "sl", VALUE, "=<dir>", "Additional directory for libraries", NULL, U_SL, 0 },
	{ "t?", FLAG, "", "Target OS and CPU options", NULL, U_TQ, 0 },
	{ "t", VALUE, "=<n>", "Weight of time", NULL, U_T, 0 },
	{ "td", VALUE, "=<dir>", "Where the temporary files go", NULL, U_TD, 0 },
	{ "to", VALUE, "=<name>", "Operating system to compile for", "OSK only", U_TO, 0 },
	{ "tp", VALUE, "=<n>[..]", "Processor to compile for, with options", "68k, 68020, cpu32", U_TP, 0 },
	{ "u", JOIN, "<name>", "Remove a predefined macro", NULL, U_U, 0 },
	{ "v", VALUE, "=<dir>", "Directory for #include and use files", NULL, U_V, 0 },
	{ "w", VALUE, "=<dir>", "Path to the default libraries", NULL, U_W, 0 },
	{ "x", VALUE, "=<phases>", "Phases to skip (il, io, ao)", "ignored", U_X, 0 },
	{ "y", VALUE, "=<name>", "Additional I-code library", "not supported", U_Y, 0 },
	{ "z", OPTVALUE, "[=<file>]", "Read options and files from <file>, or standard input", NULL, U_Z, 0 },
};
#define NUCC_OPTIONS (sizeof ucc_options / sizeof ucc_options[0])

/* Ultra C mode (-mode=ucc or c89, or the name m68k-os9-ucc). */
static int ucc_mode;

/* ----- State ----- */

static struct {
	/* Compiling. */
	struct args flags;	/* -D/-U/-I, in order */
	int opt;		/* -O level */
	int debug, nostack, math, comments;
	int cpu;		/* 0, 2, 32 for the CPU32 */
	int fpu;		/* the 68881 */
	struct args vdirs;	/* -v */

	/* What to do. */
	int asm_only, rel_only;
	int asm_syntax;		/* -obj=r68 'r', -obj=gas 'g' (with -a; else 0: -asm=) */
	int asm_files;		/* -asm=r68 'r', -asm=gas 'g', auto 0 */
	const char *reldir;

	/* Linking. */
	const char *root;	/* -cs */
	const char *out;	/* -f, -fd */
	const char *name;	/* -n */
	const char *edition;	/* -e */
	long stack;		/* -m, in bytes */
	int cio, nojmptbl, nolibs;

	/* Ultra C mode. */
	const char *mwos;	/* -mw, else MWOS */
	struct args sldirs;	/* -sl */
	int ansi;		/* -a: 1 warn, 2 err */
	int kandr;		/* -bc */
	int cq, cw, verbose_asm, cslib, fdout, preprocess;
	char endphase;		/* -e: 'a' as (objects), 's' be/ao (assembler) */
	const char *enddir;
	int space, time;	/* -s, -t */
	struct args ol;		/* -ol */
	const char *libdir;	/* -w */
	struct args libs;	/* -l */

	/* This program's own options that l68 takes too, passed on to it
	 * (-gnu-prefix=, -elf-...=, -obj=). */
	struct args passed;

	/* Files, in order. */
	struct args files;

} st = { .opt = 1 };

#define OBJ_KINDS ((1u << OBJ_ELF) | (1u << OBJ_ROF) | (1u << OBJ_UCC))
#define error os9_error
#define note os9_note

/* ----- Usage ----- */

/* One entry of the usage: LEFT in a column, then TEXT; when LEFT doesn't
 * fit, TEXT goes on the next line. A line break in TEXT continues under
 * the text column. */
static void usage_line(const char *left, const char *text)
{
	enum { COLUMN = 18 };
	if (strlen(left) < COLUMN)
		printf("   %-*s ", COLUMN, left);
	else
		printf("   %s\n   %-*s ", left, COLUMN, "");
	for (const char *nl; (nl = strchr(text, '\n')) != NULL; text = nl + 1)
		printf("%.*s\n   %-*s ", (int)(nl - text), text, COLUMN, "");
	printf("%s\n", text);
}

static void usage(void)
{
	printf("%s [opts] [files] [opts]\n", os9_who);
	if (ucc_mode)
		printf("Options of Microware Ultra C's xcc (-mode=ucc), run with GCC:\n");
	else
		printf("Options of Microware C 3.2's xcc/cc (Ultra C's -mode=compat), run with GCC:\n");
	const struct option *table = ucc_mode ? ucc_options : options;
	size_t n = ucc_mode ? NUCC_OPTIONS : NOPTIONS;
	for (size_t i = 0; i < n; i++) {
		const struct option *o = &table[i];
		char left[64];
		snprintf(left, sizeof left, "-%s%s", o->name, o->arg);
		for (char *p = left + 1; *p != '\0' && *p != '=' && *p != '<' && *p != '['; p++)
			*p = toupper((unsigned char)*p);
		/* What the option does here, when that differs, in square brackets. */
		char *text = concat(o->help, o->modes == UCOMPAT ? " (Ultra C)" : "",
			o->does != NULL ? " [" : "", o->does != NULL ? o->does : "",
			o->does != NULL ? "]" : "", NULL);
		usage_line(left, text);
		free(text);
	}
	printf("Options of this program:\n");
	usage_line("-mode=<mode>", ucc_mode ? "ucc or c89 (Ultra C, the default here), or compat (C 3.2)"
		: "compat (C 3.2, the default here), or ucc or c89 (Ultra C, as m68k-os9-ucc)");
	usage_line("-gnu-prefix=<p>", "the GNU tools: a directory of them under their short names,\n"
		"or the prefix of their names (default m68k-elfos9)");
	usage_line("-elf-save=<s>", "where files converted to ELF go: cache (default), beside, subdir");
	usage_line("-elf-dir=<name>", "subdirectory, and directory in cache (default m68k-os9-elf)");
	usage_line("-elf-cache=<dir>", "cache directory (default $XDG_CACHE_HOME or ~/.cache)");
	usage_line("-obj=<kind>", "what -r writes into .r files: elf (default), rof or ucc (ROF, for Microware's l68);\n"
		"with -a the syntax: r68 (default) or gas");
	printf("Files, by suffix, in any case:\n");
	usage_line(".c", "C source");
	usage_line(".r, .o", "object, ROF or ELF (ROF is converted; see -elf-save=)");
	usage_line(".l", "library: ROFs or Ultra C's libgen format (converted), or an ar archive");
	usage_line(".a", "r68 assembler source, assembled by m68k-os9-r68");
	printf("Environment:\n");
	if (ucc_mode) {
		usage_line("MWOS", "Ultra C's MWOS directory: SRC/DEFS, OS9/SRC/DEFS, OS9/<cpu>/LIB");
	} else {
		usage_line("CDEF", "Microware's header directory");
		usage_line("CLIB", "Microware's library directory: cstart.r, clib.l ...");
	}
	usage_line("M68K_OS9_GNU_PREFIX", "as -gnu-prefix=");
	usage_line("M68K_OS9_ELF", "-elf-.../-obj= settings as save=...,dir=...,cache=...,obj=...");
	usage_line("GRPUSER", "module owner, for elf2mod: GROUP.USER, decimal (default 0.0)");
}

static void targets(void)
{
	printf("Target OS:\n   OSK            OS-9/68K\n");
	if (ucc_mode) {
		printf("Target processors (-tp=):\n");
		printf("   68k            68000/68010/68070\n");
		printf("   68020          68020/68030\n");
		printf("   cpu32          CPU32\n");
		return;
	}
	printf("Target processors (-tp=, -k=):\n");
	printf("   68k            68000/68010/68070 (-k=0)\n");
	printf("   020            68020/68030, with the 68881 (-k=2f; -k=2 without)\n");
	printf("   cpu32          CPU32\n");
}

/* ----- Parsing ----- */

static void parse(int argc, char **argv);

/* -k=<n>[W|L][CW|CL][F] */
static void set_k(const char *value)
{
	const char *p = value;
	if (*p == '0')
		st.cpu = 0;
	else if (*p == '2')
		st.cpu = 2;
	else
		error("-k=%s: the target must be 0 or 2", value);
	p++;
	st.fpu = 0;
	while (*p != '\0') {
		char c = tolower((unsigned char)*p);
		if (c == 'c') {
			char d = tolower((unsigned char)p[1]);
			if (d == 'l')
				error("-k=%s: long code offsets aren't supported", value);
			if (d != 'w')
				error("-k=%s: bad code offset size", value);
			p += 2;
		} else if (c == 'l') {
			error("-k=%s: long data offsets aren't supported", value);
		} else if (c == 'w') {
			p++;
		} else if (c == 'f') {
			if (st.cpu != 2)
				error("-k=%s: the 68881 needs the 68020", value);
			st.fpu = 1;
			p++;
		} else {
			error("-k=%s: unknown option %c", value, *p);
		}
	}
}

/* -tp=68k, 020, cpu32 (C 3.2's -tp=020 brings the 68881). */
static void set_tp(const char *value)
{
	if (ci_prefix(value, "68k")) {
		st.cpu = 0;
		st.fpu = 0;
		value += 3;
	} else if (ci_prefix(value, "020")) {
		st.cpu = 2;
		st.fpu = 1;
		value += 3;
	} else if (ci_prefix(value, "cpu32")) {
		st.cpu = 32;
		st.fpu = 0;
		value += 5;
	} else {
		error("-tp=%s: unknown processor (68k, 020 or cpu32)", value);
	}
	for (; *value != '\0'; value++) {
		char c = tolower((unsigned char)*value);
		if (c == 'f' && st.cpu == 2)
			st.fpu = 1;
		else if (c == 'l')
			error("-tp: long offsets aren't supported");
		else if (c != 'w' && c != 'c' && c != 'd')
			error("-tp: unknown option %c", *value);
	}
}

/* -z[=FILE]: options and files from FILE, or standard input. */
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

/* Sets option O with VALUE (null without one). */
static void apply(const struct option *o, const char *value)
{
	switch (o->id) {
	case O_A: st.asm_only = 1; break;
	case O_BG: error("-bg (adhesive modules) isn't supported"); break;
	case O_BP: os9_show = 1; break;
	case O_C: st.comments = 1; break;
	case O_CS: st.root = value; break;
	case O_D: args_addf(&st.flags, "-D%s", value); break;
	case O_E: st.edition = value; break;
	case O_F: case O_FD: st.out = value; break;
	case O_G: st.debug = 1; break;
	case O_H: os9_dry_run = 1; break;
	case O_I: st.cio = 1; break;
	case O_J: st.nojmptbl = 1; break;
	case O_K: set_k(value); break;
	case O_L: args_add(&st.libs, value); break;
	case O_LO: error("-lo=%s: l68 options aren't supported", value); break;
	case O_M: {
		char *end;
		long n = strtol(value, &end, 0);
		if (end == value || (*end != '\0' && !ci_eq(end, "k")) || n < 0)
			error("-m=%s: expected a size in K", value);
		/* In K, with or without the K, as l68's -M= takes it. */
		st.stack = n * 1024;
		break;
	}
	case O_N: st.name = value; break;
	case O_NL: st.nolibs = 1; break;
	case O_NV: break;
	case O_O:
		/* As C 3.2's xcc (xcc -bp): -o alone and -o=0 no optimizer,
		 * 1 (the default) c68 and o68, 2 c68 -c (more in registers,
		 * not smaller) and o68 */
		if (value == NULL)
			st.opt = 0;
		else if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0 || strcmp(value, "2") == 0)
			st.opt = *value - '0';
		else
			error("-o=%s: the level must be 0, 1 or 2", value);
		break;
	case O_Q: os9_quiet = 1; break;
	case O_R: st.rel_only = 1; st.reldir = value; break;
	case O_S: st.nostack = 1; break;
	case O_T: os9_set_tempdir(value); break;
	case O_TQ: targets(); os9_cleanup(); exit(0);
	case O_TO:
		if (!ci_eq(value, "osk"))
			error("-to=%s: only OSK (OS-9/68K) is supported", value);
		break;
	case O_TP: set_tp(value); break;
	case O_U: args_addf(&st.flags, "-U%s", value); break;
	case O_V: args_add(&st.vdirs, value); break;
	case O_W: st.libdir = value; os9conv.libdir = value; break;
	case O_X: st.math = 1; break;
	case O_Z: read_z(value); break;
	}
}

/* -tp= in Ultra C mode: 68k (68000), 68020, cpu32. */
static void set_tp_ucc(const char *value)
{
	if (ci_prefix(value, "68k") || ci_prefix(value, "68000"))
		st.cpu = 0;
	else if (ci_prefix(value, "68020"))
		st.cpu = 2;
	else if (ci_prefix(value, "cpu32"))
		st.cpu = 32;
	else
		error("-tp=%s: unknown processor (68k, 68020 or cpu32)", value);
	st.fpu = 0;
}

/* -e=<phase>[=<dir>]: as (objects), be or ao (assembler). */
static void set_endphase(const char *value)
{
	const char *eq = strchr(value, '=');
	size_t n = eq != NULL ? (size_t)(eq - value) : strlen(value);
	if (n == 2 && ci_prefix(value, "as"))
		st.endphase = 'a';
	else if (n == 2 && (ci_prefix(value, "be") || ci_prefix(value, "ao")))
		st.endphase = 's';
	else
		error("-e=%s: only the phases as, be and ao are supported (no I-code)", value);
	st.enddir = eq != NULL ? eq + 1 : NULL;
}

/* A weight (-s=, -t=), 1 to 9 as Ultra C takes it. */
static int weight(const char *value, const char *option)
{
	char *end;
	long n = strtol(value, &end, 10);
	if (end == value || *end != '\0' || n < 0)
		error("%s=%s: expected a number", option, value);
	return (int)n;
}

/* Sets Ultra C's option O with VALUE (null without one). */
static void apply_ucc(const struct option *o, const char *value)
{
	switch (o->id) {
	case U_A:
		if (value == NULL || ci_eq(value, "warn"))
			st.ansi = 1;
		else if (ci_eq(value, "err"))
			st.ansi = 2;
		else
			error("-a=%s: expected warn or err", value);
		break;
	case U_B: os9_show = 1; break;
	case U_BC: st.kandr = 1; break;
	case U_C: case U_N: case U_M: case U_X: break;
	case U_CO: st.verbose_asm = 1; break;
	case U_CQ: st.cq = 1; break;
	case U_CS: st.root = value; break;
	case U_CW: st.cw = 1; break;
	case U_D: args_addf(&st.flags, "-D%s", value); break;
	case U_E: set_endphase(value); break;
	case U_F: st.out = value; st.fdout = 0; break;
	case U_FD: st.out = value; st.fdout = 1; break;
	case U_G: st.debug = 1; break;
	case U_H: os9_dry_run = 1; break;
	case U_I: st.cslib = 1; break;
	case U_K: st.nolibs = 1; break;
	case U_L: args_add(&st.libs, value); break;
	case U_MT:
		if (value == NULL || !ci_eq(value, "none"))
			error("-mt%s%s: multi-threaded code isn't supported", value != NULL ? "=" : "",
				value != NULL ? value : "");
		break;
	case U_MW: st.mwos = value; break;
	case U_O:
		if (value == NULL)
			st.opt = 0;
		else if (strlen(value) == 1 && *value >= '0' && *value <= '7')
			st.opt = *value - '0';
		else
			error("-o=%s: the level must be 0 to 7", value);
		break;
	case U_P:
		if (value != NULL)
			error("-p%s: only -p alone (to standard output) is supported", value);
		st.preprocess = 1;
		break;
	case U_R: st.nostack = 1; break;
	case U_S: st.space = weight(value, "-s"); break;
	case U_SL: args_add(&st.sldirs, value); break;
	case U_TQ: targets(); os9_cleanup(); exit(0);
	case U_T: st.time = weight(value, "-t"); break;
	case U_TD: os9_set_tempdir(value); break;
	case U_TO:
		if (!ci_eq(value, "osk"))
			error("-to=%s: only OSK (OS-9/68K) is supported", value);
		break;
	case U_TP: set_tp_ucc(value); break;
	case U_U: args_addf(&st.flags, "-U%s", value); break;
	case U_V: args_add(&st.vdirs, value); break;
	case U_W: st.libdir = value; break;
	case U_Z: read_z(value); break;
	case U_OL: args_addf(&st.ol, "-%s", value); break;
	case U_PHASE:
		error("-%s: options for Ultra C's phases aren't supported (only -ol=)", o->name);
		break;
	case U_CX: case U_EDGX: case U_J: case U_Y: case U_CPP:
		error("-%s: not supported (cross references, I-code and C++)", o->name);
		break;
	}
}

/* This program's own options; returns whether ARG was one. */
static int own_option(const char *arg)
{
	if (ci_prefix(arg + 1, "mode=")) {
		/* taken before the parsing (main) */
		const char *value = arg + 1 + strlen("mode=");
		if (!ci_eq(value, "ucc") && !ci_eq(value, "c89") && !ci_eq(value, "compat"))
			error("unknown -mode=%s (compat, c89 or ucc)", value);
		return 1;
	}
	/* -obj=r68|gas: the assembly syntax of -a (xcc's own, not passed on) */
	if (ci_eq(arg + 1, "obj=r68") || ci_eq(arg + 1, "obj=gas")) {
		st.asm_syntax = ci_eq(arg + 1, "obj=gas") ? 'g' : 'r';
		return 1;
	}
	/* -asm=auto|r68|gas: the syntax of .a files, read (auto: detected) and
	 * written by -a (auto: r68) */
	if (ci_prefix(arg + 1, "asm=")) {
		const char *value = arg + 1 + strlen("asm=");
		if (ci_eq(value, "auto"))
			st.asm_files = 0;
		else if (ci_eq(value, "r68") || ci_eq(value, "gas"))
			st.asm_files = ci_eq(value, "gas") ? 'g' : 'r';
		else
			error("unknown -asm=%s (auto, r68 or gas)", value);
		return 1;
	}
	if (os9_tools_option(arg) || os9conv_option(arg, OBJ_KINDS)) {
		args_add(&st.passed, arg);
		return 1;
	}
	return 0;
}

/* Matches option ARG (with its '-'); the longest name that fits wins. */
static void option(const char *arg)
{
	const char *text = arg + 1;
	const struct option *best = NULL;
	const char *value = NULL, *candidate;
	size_t bestlen = 0;

	if (strcmp(text, "?") == 0) {
		usage();
		os9_cleanup();
		exit(0);
	}
	if (own_option(arg))
		return;

	const struct option *table = ucc_mode ? ucc_options : options;
	size_t n = ucc_mode ? NUCC_OPTIONS : NOPTIONS;
	for (size_t i = 0; i < n; i++) {
		const struct option *o = &table[i];
		size_t len = strlen(o->name);
		if (len <= bestlen || !ci_prefix(text, o->name))
			continue;
		const char *rest = text + len;
		switch (o->kind) {
		case FLAG:
			if (*rest != '\0')
				continue;
			candidate = NULL;
			break;
		case VALUE:
		case JOIN:
			if (*rest == '=')
				rest++;
			if (*rest == '\0')
				continue;
			candidate = rest;
			break;
		case OPTVALUE:
		default:
			candidate = *rest == '=' ? rest + 1 : *rest != '\0' ? rest : NULL;
			if (candidate != NULL && *candidate == '\0')
				candidate = NULL;
			break;
		}
		best = o;
		bestlen = len;
		value = candidate;
	}
	if (best == NULL)
		error("unknown option %s (%s -? for the usage)", arg, os9_who);
	if (ucc_mode)
		apply_ucc(best, value);
	else
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

/* ----- Paths and converted files ----- */

#define suffix path_suffix
#define stem path_stem

/* ----- Microware's libraries ----- */

/* The library directory: -w, else CLIB. */
static const char *libdir(void)
{
	const char *dir = st.libdir != NULL ? st.libdir : getenv("CLIB");
	if (dir == NULL || *dir == '\0')
		error("CLIB isn't set: it names the directory of Microware's cstart.r and libraries");
	return dir;
}

/* NAME (e.g. "clib.l") in the library directory, regardless of case
 * (allocated). */
static char *libfile(const char *name)
{
	char *path = host_find_nocase(libdir(), name);
	if (path == NULL)
		error("cannot find %s in %s", name, libdir());
	return path;
}

/* The default libraries, as C 3.2's xcc picks them. */
static void default_libraries(struct args *libs)
{
	const char *clib, *math;
	if (st.cpu == 2 && st.math)
		clib = "clib020.l", math = NULL;
	else if (st.cpu == 2 && st.fpu)
		clib = "clib020h.l", math = "math881.l";
	else if (st.cpu == 2)
		clib = "clib020n.l", math = "math.l";
	else if (st.math)
		clib = "clib.l", math = NULL;
	else
		clib = "clibn.l", math = "math.l";

	if (st.cio)
		args_add(libs, libfile("cio.l"));
	args_add(libs, libfile(clib));
	if (math != NULL)
		args_add(libs, libfile(math));
	args_add(libs, libfile("sys.l"));
}

/* ----- Ultra C's files ----- */

/* The MWOS tree: -mw, else MWOS. */
static const char *mwos(void)
{
	const char *dir = st.mwos != NULL ? st.mwos : getenv("MWOS");
	if (dir == NULL || *dir == '\0')
		error("MWOS isn't set: it names Ultra C's MWOS directory (or give -mw=)");
	return dir;
}

/* The directories Ultra C's xcc searches for libraries (xcc -b's "Library
 * file paths"): -w's, else those of the CPU (the 68020's and the CPU32's
 * before the 68000's), then -sl's. */
static void ucc_libdirs(struct args *dirs)
{
	if (st.libdir != NULL) {
		args_add(dirs, st.libdir);
	} else {
		if (st.cpu == 2)
			args_addf(dirs, "%s/OS9/68020/LIB", mwos());
		if (st.cpu == 2 || st.cpu == 32)
			args_addf(dirs, "%s/OS9/CPU32/LIB", mwos());
		args_addf(dirs, "%s/OS9/68000/LIB", mwos());
	}
	args_append(dirs, &st.sldirs);
}

/* NAME found in the library directories, regardless of case (allocated);
 * as given when it has a directory or isn't found, as Ultra C's xcc passes
 * it then. */
static char *ucc_libfile(const char *name)
{
	if (strcmp(lbasename(name), name) != 0)
		return xstrdup(name);
	struct args dirs = { 0 };
	ucc_libdirs(&dirs);
	char *path = NULL;
	for (int i = 0; i < dirs.n && path == NULL; i++)
		path = host_find_nocase(dirs.v[i], name);
	args_free(&dirs);
	return path != NULL ? path : xstrdup(name);
}

/* Ultra C's default libraries: K&R mode's (-bc), the shared library
 * module's (-i), or the ANSI ones. */
static void ucc_default_libraries(struct args *libs)
{
	static const char *const kandr[] = { "sys_clib.l", "clib.l", "os_lib.l", "math.l", "sys.l", NULL };
	static const char *const shared[] = { "csl.l", "os_csl.l", "sys.l", NULL };
	static const char *const ansi[] = { "clib.l", "os_lib.l", "sys.l", NULL };
	const char *const *names = st.kandr ? kandr : st.cslib ? shared : ansi;
	for (int i = 0; names[i] != NULL; i++) {
		char *path = ucc_libfile(names[i]);
		args_add(libs, path);
		free(path);
	}
}

/* Ultra C's predefined names, as its xcc gives them to the front end
 * (-D) and to r68 (-a=). */
static void ucc_predefined(struct args *a, const char *option)
{
	args_addf(a, "%s_UCC", option);
	args_addf(a, "%s_MAJOR_REV=2", option);
	args_addf(a, "%s_MINOR_REV=5", option);
	args_addf(a, "%s_SPACE_FACTOR=%d", option, st.space);
	args_addf(a, "%s_TIME_FACTOR=%d", option, st.time);
	args_addf(a, "%s_OSK", option);
	if (st.kandr)
		args_addf(a, "%sOSK", option);
	args_addf(a, "%s%s", option, st.cpu == 2 ? "_MPF68020" : st.cpu == 32 ? "_MPFCPU32" : "_MPF68000");
	args_addf(a, "%s_MPF68K", option);
	if (st.kandr)
		args_addf(a, "%smc68000", option);
	args_addf(a, "%s_FPF881", option);
	args_addf(a, "%s_BIG_END", option);
}

/* Ultra C's header directories: -v's, then the MWOS tree's. */
static void ucc_incdirs(struct args *dirs)
{
	args_append(dirs, &st.vdirs);
	args_addf(dirs, "%s/SRC/DEFS", mwos());
	args_addf(dirs, "%s/OS9/SRC/DEFS", mwos());
}

/* A directory with a stdarg.h that is GCC's: searched before Ultra C's
 * header directories, whose stdarg.h makes va_start a builtin of Ultra C's
 * compiler (__inline_va_start__); GCC's va_list under -mos9call=ucc is a
 * plain pointer as Ultra C's, which its vprintf and the like take. Null
 * when GCC doesn't say where its headers are. */
static const char *ucc_stdarg_dir(void)
{
	static char *dir;
	static int done;
	if (done)
		return dir;
	done = 1;
	char *out = os9_temp(".inc");
	struct args argv = { 0 };
	args_add(&argv, GCC);
	args_add(&argv, "-print-file-name=include");
	int dry = os9_dry_run, show = os9_show;
	os9_dry_run = os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	os9_dry_run = dry, os9_show = show;
	args_free(&argv);
	char inc[4096] = "";
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	if (fp != NULL) {
		if (fgets(inc, sizeof inc, fp) == NULL)
			inc[0] = '\0';
		fclose(fp);
	}
	inc[strcspn(inc, "\r\n")] = '\0';
	if (inc[0] == '\0' || (dir = os9_temp_dir()) == NULL)
		return NULL;
	char *shim = concat(dir, "/stdarg.h", NULL);
	os9_temp_add(shim);
	fp = fopen(shim, "w");
	if (fp == NULL)
		error("cannot write %s", shim);
	/* Ultra C's ansi_c.h defines _VA_LIST (unsigned char *, for the
	 * prototypes of vprintf and the like), which GCC's stdarg.h takes as
	 * va_list defined already (SCO's), and defines empty itself */
	fprintf(fp, "/* GCC's, not Ultra C's (m68k-os9-ucc); Ultra C's _VA_LIST is va_list */\n"
		"#undef _VA_LIST\n#include \"%s/stdarg.h\"\n#undef _VA_LIST\n#define _VA_LIST va_list\n", inc);
	fclose(fp);
	free(shim);
	return dir;
}

/* GCC's options for Ultra C's code generation: its convention (variadic
 * calls' unnamed arguments on the stack), '\n' a CR as in C 3.2. */
static void ucc_compile_flags(struct args *a)
{
	args_add(a, "-mpcrel");
	args_add(a, "-ma6rel");
	/* K&R mode (-bc): the library's printf and scanf take the first
	 * argument after the format from d1, as C 3.2's (-mos9call); ANSI
	 * mode calls them, and fopen and freopen, by other names (measured
	 * 2026-10-17: Ultra C's compiler renames the calls), whose variadic
	 * ones take all unnamed arguments from the stack (-mos9call=ucc) */
	if (st.kandr) {
		args_add(a, "-mos9call");
	} else {
		args_add(a, "-mos9call=ucc");
		args_add(a, "-Dprintf=__ansi_printf");
		args_add(a, "-Dscanf=__ansi_scanf");
		args_add(a, "-Dfopen=__ansi_fopen");
		args_add(a, "-Dfreopen=__ansi_freopen");
	}
	args_add(a, "-mbuiltin=os9call");
	/* '\n' is a CR in every mode, as in Ultra C's compiler (measured: its
	 * default, -bc and -a modes); -a also makes '\r' a line feed, which
	 * -mos9newline doesn't (it stays a CR) */
	args_add(a, "-mos9newline");
	if (st.ansi)
		args_add(a, st.cq ? "-std=gnu89" : "-std=c89");
	else
		args_add(a, "-std=gnu89");
	if (st.ansi)
		args_add(a, st.ansi == 2 ? "-pedantic-errors" : "-pedantic");
	args_add(a, st.cw ? "-Wall" : "-w");
	if (!st.nostack)
		args_add(a, "-mos9stkchk");
	if (st.cpu == 2)
		args_add(a, "-m68020");
	else if (st.cpu == 32)
		args_add(a, "-mcpu=cpu32");
	else
		args_add(a, "-m68000");
	args_add(a, "-msoft-float");
	if (st.space > st.time)
		args_add(a, "-Os");
	else
		args_addf(a, "-O%d", st.opt == 0 ? 0 : st.opt <= 3 ? 1 : 2);
	if (st.debug)
		args_add(a, "-g");
	if (st.verbose_asm && st.endphase == 's')
		args_add(a, "-fverbose-asm");
	ucc_predefined(a, "-D");
	args_append(a, &st.flags);
	const char *shim = ucc_stdarg_dir();
	if (shim != NULL)
		args_addf(a, "-I%s", shim);
	struct args dirs = { 0 };
	ucc_incdirs(&dirs);
	for (int i = 0; i < dirs.n; i++)
		args_addf(a, "-I%s", dirs.v[i]);
	args_free(&dirs);
}

/* ----- Compiling ----- */

/* GCC's options for C 3.2's code generation. */
static void compile_flags(struct args *a)
{
	if (ucc_mode) {
		ucc_compile_flags(a);
		return;
	}
	args_add(a, "-mpcrel");
	args_add(a, "-ma6rel");
	args_add(a, "-mos9call");
	args_add(a, "-mbuiltin=os9call");
	args_add(a, "-mos9newline");
	args_add(a, "-std=gnu89");
	args_add(a, "-w");
	/* C 3.2 assumes nothing from pointer types, and old code reads one
	 * type through a pointer to another; GCC's -O2 and -Os otherwise do
	 * (measured on 399 newlib files: 16-20 compiled differently, the code
	 * 0.1% bigger without) */
	args_add(a, "-fno-strict-aliasing");
	if (!st.nostack)
		args_add(a, "-mos9stkchk");
	if (st.cpu == 2)
		args_add(a, "-m68020");
	else if (st.cpu == 32)
		args_add(a, "-mcpu=cpu32");
	else
		args_add(a, "-m68000");
	if (st.fpu)
		args_add(a, "-m68881");
	else
		args_add(a, "-msoft-float");
	if (st.math && !st.fpu)
		args_add(a, "-mos9math");
	/* level 1, C 3.2's compact default (o68, a peephole pass): GCC's
	 * -Os, the smallest code; 2, which C 3.2 makes no smaller: -O2 */
	if (st.opt == 1)
		args_add(a, "-Os");
	else
		args_addf(a, "-O%d", st.opt);
	if (st.debug)
		args_add(a, "-g");
	if (st.comments && st.asm_only)
		args_add(a, "-fverbose-asm");
	args_append(a, &st.flags);
	for (int i = 0; i < st.vdirs.n; i++)
		args_addf(a, "-I%s", st.vdirs.v[i]);
	const char *cdef = getenv("CDEF");
	if (cdef != NULL && *cdef != '\0')
		args_addf(a, "-I%s", cdef);
}

/* Compiles C file SOURCE: to OUTPUT, as assembler (-a), an object, or a
 * ROF (-obj=rof). */
static void compile(const char *source, const char *output, int assembler, int rof)
{
	struct args argv = { 0 };
	args_add(&argv, GCC);
	compile_flags(&argv);
	args_add(&argv, assembler ? "-S" : "-c");
	args_add(&argv, "-o");
	/* -a: GCC's gas syntax, then r68's through gastor68 (as Microware's
	 * xcc -a writes r68 source), unless -obj=gas, or -asm=gas without -obj= */
	int syntax = st.asm_syntax != 0 ? st.asm_syntax : st.asm_files;
	int r68 = assembler && syntax != 'g';
	char *object = rof ? os9_temp(".o") : r68 ? os9_temp(".s") : (char *)output;
	args_add(&argv, object);
	args_add(&argv, source);
	if (!os9_quiet && !os9_show)
		printf("'%s'\n", source);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("compiling %s failed", source);
	args_free(&argv);

	if (r68) {
		args_add(&argv, GASTOR68);
		args_add(&argv, ucc_mode ? "-mode=ucc" : "-mode=c32");
		if (st.cpu != 0)
			args_add(&argv, "-m2");	/* as r68 is run for it */
		args_add(&argv, "-o");
		args_add(&argv, output);
		args_add(&argv, object);
		if (os9_run(&argv, NULL, NULL) != 0)
			error("translating %s to r68 syntax failed", source);
		args_free(&argv);
	}

	if (rof) {
		char *name = stem(source, 0);
		args_add(&argv, ELF2ROF);
		args_add(&argv, "-n");
		args_addf(&argv, "%s_c", name);
		args_add(&argv, "-o");
		args_add(&argv, output);
		args_add(&argv, object);
		free(name);
		if (os9_run(&argv, NULL, NULL) != 0)
			error("converting %s to ROF failed", source);
		args_free(&argv);
	}
}

/* Assembles gas source SOURCE (an xcc -a -obj=gas file) into OUTPUT, an ELF
 * object or (ROF) a ROF, with GCC's assembler for the CPU of -k/-tp. */
static void assemble_gas(const char *source, const char *output, int rof)
{
	struct args argv = { 0 };
	/* gas takes LF line ends only (a CR is junk to it); CR LF and CR (OS-9's)
	 * as m68k-os9-r68 takes them: a copy with LF */
	const char *input = source;
	FILE *in = fopen(source, "rb");
	if (in != NULL) {
		int c, cr = 0;
		while ((c = getc(in)) != EOF && !cr)
			cr = c == '\r';
		if (cr) {
			char *copy = os9_temp(".s");
			FILE *out = fopen(copy, "wb");
			if (out == NULL)
				error("cannot write %s", copy);
			rewind(in);
			int prev = 0;
			while ((c = getc(in)) != EOF) {
				if (c == '\n' && prev == '\r') {
					prev = c;
					continue;	/* CR LF: the LF already written for the CR */
				}
				putc(c == '\r' ? '\n' : c, out);
				prev = c;
			}
			if (fclose(out) != 0)
				error("cannot write %s", copy);
			input = copy;
		}
		fclose(in);
	}
	args_add(&argv, GCC);
	args_add(&argv, st.cpu == 2 ? "-m68020" : st.cpu == 32 ? "-mcpu=cpu32" : "-m68000");
	if (st.fpu)
		args_add(&argv, "-m68881");
	args_add(&argv, "-c");
	args_add(&argv, "-x");
	args_add(&argv, "assembler");
	args_add(&argv, input);
	args_add(&argv, "-x");
	args_add(&argv, "none");
	args_add(&argv, "-o");
	char *object = rof ? os9_temp(".o") : (char *)output;
	args_add(&argv, object);
	if (!os9_quiet && !os9_show)
		printf("'%s'\n", source);
	if (os9_run(&argv, NULL, NULL) != 0)
		error("assembling %s failed", source);
	args_free(&argv);
	if (rof) {
		char *name = stem(source, 0);
		args_add(&argv, ELF2ROF);
		args_add(&argv, "-n");
		args_addf(&argv, "%s_a", name);
		args_add(&argv, "-o");
		args_add(&argv, output);
		args_add(&argv, object);
		free(name);
		if (os9_run(&argv, NULL, NULL) != 0)
			error("converting %s to ROF failed", source);
		args_free(&argv);
	}
}

/* Assembles r68 source SOURCE into OUTPUT, an ELF object or (ROF) a ROF,
 * with r68 for the CPU of -k/-tp (r68's 2 is the 68020 and the CPU32): our
 * m68k-os9-r68, or Microware's r68 where that is the one found (see "Finding
 * the tools" in m68k-os9-xcc.md). So only r68's own options; the object
 * format goes to ours through M68K_OS9_ELF, and a ROF from Microware's where
 * an ELF object is wanted is converted. */
static void assemble(const char *source, const char *output, int rof)
{
	struct args argv = { 0 };
	args_add(&argv, R68);
	if (ucc_mode) {
		/* as Ultra C's xcc runs r68: its predefined names, -bt, the CPU,
		 * the header directories for use files */
		ucc_predefined(&argv, "-a=");
		args_add(&argv, "-bt");
		args_add(&argv, st.cpu != 0 ? "-m2" : "-m0");
		struct args dirs = { 0 };
		ucc_incdirs(&dirs);
		for (int i = 0; i < dirs.n; i++)
			args_addf(&argv, "-u=%s", dirs.v[i]);
		args_free(&dirs);
	} else if (st.cpu != 0) {
		args_add(&argv, "-m2");
	}
	args_add(&argv, source);
	args_addf(&argv, "-o=%s", output);
	if (os9_quiet)
		args_add(&argv, "-q");
	if (!os9_quiet && !os9_show)
		printf("'%s'\n", source);
	const char *env = getenv("M68K_OS9_ELF");
	char *saved = env != NULL ? xstrdup(env) : NULL;
	char *obj = concat(saved != NULL && *saved != '\0' ? saved : "", saved != NULL && *saved != '\0' ? "," : "",
		rof ? "obj=rof" : "obj=elf", NULL);
	host_setenv("M68K_OS9_ELF", obj);
	int status = os9_run(&argv, NULL, NULL);
	host_setenv("M68K_OS9_ELF", saved);
	free(obj);
	free(saved);
	if (status != 0)
		error("assembling %s failed", source);
	args_free(&argv);
	if (!rof && !os9_dry_run && !file_is_elf(output)) {
		/* a ROF, from Microware's r68: into ELF in its place (the ROF
		 * moved beside it: a rename into the temporary directory could
		 * cross file systems) */
		char *rof_copy = concat(output, ".rof", NULL);
		if (host_replace(output, rof_copy) != 0)
			error("cannot rename %s", output);
		args_add(&argv, ROF2ELF);
		args_add(&argv, "-o");
		args_add(&argv, output);
		args_add(&argv, rof_copy);
		int converted = os9_run(&argv, NULL, NULL);
		remove(rof_copy);
		free(rof_copy);
		if (converted != 0)
			error("converting %s's ROF to ELF failed", source);
		args_free(&argv);
	}
}

/* ----- Linking ----- */

/* Links OBJECTS into module OUTPUT as C 3.2's xcc does: by running l68 with
 * cstart.r (or -cs=), the objects, the libraries (-l=, then the default
 * ones) and the module's settings; our m68k-os9-l68 normally (see "Finding
 * the tools" in m68k-os9-xcc.md), with C 3.2's order of library members
 * (-dialect=c32), the jump table allowed unless -j (l68's -a, which C 3.2's
 * xcc always gives otherwise). */
static void link_module(struct args *objects, const char *output)
{
	struct args libs = { 0 }, argv = { 0 };
	for (int i = 0; i < st.libs.n; i++)
		args_add(&libs, st.libs.v[i]);
	if (!st.nolibs)
		default_libraries(&libs);

	args_add(&argv, os9_front("l68"));
	if (st.root != NULL) {
		args_add(&argv, st.root);
	} else {
		char *root = libfile("cstart.r");
		args_add(&argv, root);
		free(root);
	}
	args_append(&argv, objects);
	for (int i = 0; i < libs.n; i++)
		args_addf(&argv, "-l=%s", libs.v[i]);
	args_addf(&argv, "-o=%s", output);
	if (st.name != NULL)
		args_addf(&argv, "-n=%s", st.name);
	if (st.edition != NULL)
		args_addf(&argv, "-e=%s", st.edition);
	if (st.stack > 0)
		args_addf(&argv, "-M=%ld", st.stack / 1024);
	if (st.debug)
		args_add(&argv, "-g");
	if (!st.nojmptbl)
		args_add(&argv, "-a");
	args_add(&argv, "-dialect=c32");
	args_append(&argv, &st.passed);

	if (!os9_quiet && !os9_show)
		printf("linking %s\n", output);
	if (os9_run(&argv, NULL, NULL) != 0) {
		/* l68 has said why */
		os9_cleanup();
		exit(1);
	}
	args_free(&libs);
	args_free(&argv);
}

/* Links OBJECTS (and the libraries given as files, LIBFILES) into module
 * OUTPUT as Ultra C's xcc does (measured with xcc -h -b): l68 -t=os9_68k
 * with acstart.r (cstart.r with -bc, -cs=, none with -k), -o= (-O= with
 * -fd=), the file attributes, the alignment of the CPU (-x= and -b=: 2, 16
 * for the 68020, 4 for the CPU32), -g, -c for ANSI mode (-a), the -l=
 * libraries then the default ones, -ol='s options, then the objects. No
 * -a: a jump table only with -ol=a, as with Ultra C's xcc. Ultra C's order
 * of library members is our l68's default. */
static void ucc_link_module(struct args *objects, const char *output)
{
	struct args libs = { 0 }, argv = { 0 };
	for (int i = 0; i < st.libs.n; i++)
		args_add(&libs, st.libs.v[i]);
	if (!st.nolibs)
		ucc_default_libraries(&libs);

	args_add(&argv, os9_front("l68"));
	args_add(&argv, "-t=os9_68k");
	if (st.root != NULL) {
		args_add(&argv, st.root);
	} else if (!st.nolibs) {
		char *root = ucc_libfile(st.kandr ? "cstart.r" : "acstart.r");
		args_add(&argv, root);
		free(root);
	}
	args_addf(&argv, st.fdout ? "-O=%s" : "-o=%s", output);
	if (st.ansi)
		args_add(&argv, "-c");
	args_add(&argv, "-f=orowoe");
	int align = st.cpu == 2 ? 16 : st.cpu == 32 ? 4 : 2;
	args_addf(&argv, "-x=%d", align);
	args_addf(&argv, "-b=%d", align);
	if (st.debug)
		args_add(&argv, "-g");
	for (int i = 0; i < libs.n; i++)
		args_addf(&argv, "-l=%s", libs.v[i]);
	args_append(&argv, &st.ol);
	args_append(&argv, &st.passed);
	args_append(&argv, objects);

	if (!os9_quiet && !os9_show)
		printf("linking %s\n", output);
	if (os9_run(&argv, NULL, NULL) != 0) {
		/* l68 has said why */
		os9_cleanup();
		exit(1);
	}
	args_free(&libs);
	args_free(&argv);
}

/* ----- Main ----- */

int main(int argc, char **argv)
{
	/* The mode: Ultra C's under a name with ucc, else C 3.2's; -mode=
	 * anywhere on the line (the last one) chooses, as the option table
	 * the line is parsed with depends on it. */
	const char *base = lbasename(argv[0]);
	ucc_mode = strstr(base, "ucc") != NULL;
	for (int i = 1; i < argc; i++)
		if (argv[i][0] == '-' && ci_prefix(argv[i] + 1, "mode="))
			ucc_mode = !ci_eq(argv[i] + 1 + strlen("mode="), "compat");
	os9_who = ucc_mode ? "m68k-os9-ucc" : "m68k-os9-xcc";
	if (ucc_mode) {
		st.opt = 7;
		st.space = st.time = 1;
	}
	/* xcc and ucc before cc, which ends both */
	static const char *const own_names[] = { "xcc", "ucc", "cc", NULL };
	os9_tools_init(argv[0], own_names);

	os9conv_environment(OBJ_KINDS);
	if (argc < 2) {
		usage();
		return 0;
	}
	parse(argc, argv);
	if (st.files.n == 0)
		error("no files (%s -? for the usage)", os9_who);
	if (ucc_mode && st.endphase != 0) {
		/* -e=as: objects, -e=be/ao: assembler, in -e='s directory */
		st.rel_only = st.endphase == 'a';
		st.asm_only = st.endphase == 's';
		st.reldir = st.enddir;
	}
	if (st.asm_syntax != 0 && !st.asm_only)
		error("-obj=%s goes with -a (the assembler output's syntax)", st.asm_syntax == 'g' ? "gas" : "r68");

	struct args objects = { 0 };
	const char *first = NULL;
	for (int i = 0; i < st.files.n; i++) {
		const char *file = st.files.v[i];
		const char *sfx = suffix(file);
		if (ci_eq(sfx, ".c") && st.preprocess) {
			/* -p (Ultra C): the preprocessed source */
			struct args pp = { 0 };
			args_add(&pp, GCC);
			compile_flags(&pp);
			args_add(&pp, "-E");
			args_add(&pp, file);
			if (os9_run(&pp, NULL, NULL) != 0)
				error("preprocessing %s failed", file);
			args_free(&pp);
		} else if (ci_eq(sfx, ".c")) {
			char *name = stem(file, 0);
			if (first == NULL)
				first = xstrdup(name);
			if (st.asm_only) {
				char *out = ucc_mode && st.reldir != NULL ? concat(st.reldir, "/", name, ".a", NULL)
					: concat(name, ".a", NULL);
				compile(file, out, 1, 0);
				free(out);
			} else if (st.rel_only) {
				const char *dir = st.reldir != NULL ? st.reldir : ".";
				char *out = concat(dir, "/", name, ".r", NULL);
				compile(file, out, 0, os9conv.obj != OBJ_ELF);
				free(out);
			} else {
				char *out = os9_temp(".o");
				compile(file, out, 0, 0);
				args_add(&objects, out);
			}
			free(name);
		} else if (ci_eq(sfx, ".r") || ci_eq(sfx, ".o")) {
			/* ROF or ELF, whatever the suffix: some makefiles name
			 * ROF files .o. */
			if (first == NULL)
				first = stem(file, 0);
			/* as given: l68 converts a ROF */
			if (!st.asm_only && !st.rel_only)
				args_add(&objects, file);
		} else if (ci_eq(sfx, ".l") && ucc_mode) {
			/* given to l68 as a file, as Ultra C's xcc does: linked
			 * whole */
			if (!st.asm_only && !st.rel_only)
				args_add(&objects, file);
		} else if (ci_eq(sfx, ".l")) {
			args_add(&st.libs, file);
		} else if (ci_eq(sfx, ".a")) {
			/* assembler source: r68's through m68k-os9-r68, gas's (an
			 * xcc -a -obj=gas file) through GCC's assembler; which one
			 * from -asm=, else detected (asmsyntax.c; undecided: r68).
			 * -a has nothing to do for it. */
			char *name = stem(file, 0);
			if (first == NULL)
				first = xstrdup(name);
			int gas = st.asm_files == 'g' || (st.asm_files == 0 && asm_syntax_of_file(file) == ASM_GAS);
			if (st.rel_only) {
				const char *dir = st.reldir != NULL ? st.reldir : ".";
				char *out = concat(dir, "/", name, ".r", NULL);
				(gas ? assemble_gas : assemble)(file, out, os9conv.obj != OBJ_ELF);
				free(out);
			} else if (!st.asm_only) {
				char *out = os9_temp(".o");
				(gas ? assemble_gas : assemble)(file, out, 0);
				args_add(&objects, out);
			}
			free(name);
		} else {
			error("%s: no recognized suffix", file);
		}
	}

	if (ucc_mode && !st.asm_only && !st.rel_only && !st.preprocess) {
		/* Ultra C's xcc names the module after its one input file, else
		 * "output" */
		if (st.out == NULL && first == NULL)
			error("nothing to link");
		ucc_link_module(&objects, st.out != NULL ? st.out : st.files.n == 1 ? first : "output");
	} else if (!st.asm_only && !st.rel_only && !st.preprocess) {
		if (first == NULL && st.out == NULL)
			error("nothing to link");
		link_module(&objects, st.out != NULL ? st.out : first);
	}

	args_free(&objects);
	os9_cleanup();
	return 0;
}
