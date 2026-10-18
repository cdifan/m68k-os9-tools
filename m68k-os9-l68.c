/* m68k-os9-l68: Microware's linker l68 (C 3.2's and Ultra C's options),
 * run with GNU ld and elf2mod.
 *
 * A drop-in for makefiles that call l68: the same options and inputs (ROF
 * objects and libraries, converted with rof2elf on demand, or ELF), linked
 * by GNU ld (through m68k-elfos9-gcc, as m68k-os9-xcc links) and made into
 * an OS-9 module by elf2mod, with the module type's header layout as l68
 * writes it. See m68k-os9-l68.md.
 *
 * MIT License; see elf2mod.c.
 */

#include "hostsys.h"
#include "os9conv.h"
#include "os9link.h"
#include "r68x_note.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libiberty.h>

#define GCC	os9_gnu("gcc")
#define NM	os9_gnu("nm")
#define OBJCOPY	os9_gnu("objcopy")
#define ELF2MOD	os9_tool("elf2mod")

#define error os9_error
#define note os9_note

/* The objects -obj= may name: l68 writes modules, so only the conversion
 * settings of M68K_OS9_ELF matter here; its obj= is accepted and unused. */
#define OBJ_KINDS ((1u << OBJ_ELF) | (1u << OBJ_ROF) | (1u << OBJ_UCC))

/* ----- Options ----- */

enum kind {
	FLAG,		/* -a */
	VALUE,		/* -e=5 */
	OPTVALUE,	/* -r, -r=400 */
	JOIN,		/* -mtq */
};

enum id {
	O_A, O_B, O_C, O_E, O_F, O_G, O_GU, O_I, O_J, O_L, O_M, O_MM, O_MT, O_N, O_NN,
	O_OO, O_O, O_P, O_R, O_RR, O_S, O_SS, O_T, O_V, O_W, O_X, O_Z, O_DIALECT,
	O_NAMEPAD, O_IREFSPAD, O_JMPTBLPAD, O_STBNAMEPAD, O_DBGVERSION,
};

#define C32	1	/* in C 3.2's l68 */
#define UCC	2	/* in Ultra C's l68 */
#define OURS	4	/* ours */

struct option {
	const char *name;	/* case-sensitive, without the '-' */
	enum kind kind;
	const char *arg;	/* the value, in the usage */
	const char *help;	/* what l68 does, in our words */
	const char *does;	/* what this program does, when that differs */
	enum id id;
	int linkers;
};

/* In the order of Ultra C's l68 -?, with C 3.2's -E= at -e=. */
static const struct option options[] = {
	{ "a", FLAG, "", "Far bsr and lea through the jump table", NULL, O_A, C32 | UCC },
	{ "b", VALUE, "=<n>", "Align code and data to <n> (2, 4, 8, 16)", NULL, O_B, UCC },
	{ "c", FLAG, "", "ANSI: only the underscored linker symbols", NULL, O_C, UCC },
	{ "e", VALUE, "=<n>", "Module edition", NULL, O_E, C32 | UCC },
	{ "E", VALUE, "=<n>", "Module edition (as -e=)", NULL, O_E, C32 },
	{ "f", VALUE, "=<perms>", "File permissions (pr pw pe gr gw ge or ow oe)", "ignored", O_F, UCC },
	{ "g", FLAG, "", "Symbol module for the debugger", "C 3.2's format, Ultra C's with -t=", O_G, C32 | UCC },
	{ "gu", VALUE, "=<g>.<u>", "Module owner", NULL, O_GU, UCC },
	{ "i", FLAG, "", "Initialized static storage in a system module", NULL, O_I, UCC },
	{ "j", FLAG, "", "Print the jump table map", "the entries elf2mod made", O_J, C32 | UCC },
	{ "l", VALUE, "=<path>", "Library, searched after all files", "ROF, libgen's, or ar", O_L, C32 | UCC },
	{ "m", OPTVALUE, "[=<path>]", "Module map, to standard output or <path>", "GNU ld's map", O_M, C32 | UCC },
	{ "M", VALUE, "=<n>[K]", "More stack, in KB", NULL, O_MM, C32 | UCC },
	{ "mt", JOIN, "<x>", "Thread-incompatible code: e, w or q", "accepted, no effect", O_MT, UCC },
	{ "n", VALUE, "=<name>", "Module name", "default: output file, final .mod dropped", O_N, C32 | UCC },
	{ "O", VALUE, "=<path>", "Output file (in the data directory)", "path as given", O_OO, C32 | UCC },
	{ "o", VALUE, "=<path>", "Output file (in the execution directory)", "path as given", O_O, C32 | UCC },
	{ "p", VALUE, "=<hex>", "Module header permissions", NULL, O_P, C32 | UCC },
	{ "r", OPTVALUE, "[=<base>]", "Raw binary, code at <base> (hex, default 0)", NULL, O_R, C32 | UCC },
	{ "R", VALUE, "=<n>", "Module revision", NULL, O_RR, UCC },
	{ "s", OPTVALUE, "[=<path>]", "Module map with symbols", "GNU ld's map", O_S, C32 | UCC },
	{ "S", FLAG, "", "Sticky module", NULL, O_SS, C32 | UCC },
	{ "t", VALUE, "=<target>", "Target", "os9_68k only; Ultra C's symbol module format", O_T, UCC },
	{ "v", FLAG, "", "Verbose", "shows the commands run", O_V, UCC },
	{ "w", FLAG, "", "Map symbols in alphabetical order (with -s)", NULL, O_W, C32 | UCC },
	{ "x", VALUE, "=<n>", "Align the execution offset to <n> (2, 4, 8, 16)", NULL, O_X, UCC },
	{ "z", OPTVALUE, "[=<path>]", "Files and options from standard input or <path>", NULL, O_Z, C32 | UCC },
	{ "N", OPTVALUE, "[=<name>]", "Module name after the code (bare: the default name)", NULL, O_NN, OURS },
	{ "dialect", VALUE, "=<d>", "Library members in the order of ucc (Ultra C's l68, default) or c32 (C 3.2's: the archive's)", NULL, O_DIALECT, OURS },
	{ "namepad", VALUE, "=<hex>", "The byte after an odd-length module name (default 00)", NULL, O_NAMEPAD, OURS },
	{ "dbgversion", VALUE, "=<n>", "With -g, the .dbg's word after the magic (default: 3 Ultra C style, 0 C 3.2's)", NULL, O_DBGVERSION, OURS },
	{ "stbnamepad", VALUE, "=<hex>", "With -g, the byte after the symbol module's odd-length name (default 00)", NULL, O_STBNAMEPAD, OURS },
	{ "jmptblpad", VALUE, "=<n>", "With a jump table (-a), <n> unused entries (jmp 0) after the used ones", NULL, O_JMPTBLPAD, OURS },
	{ "irefspad", VALUE, "=<hex>", "Bytes after the initialized data's pointer table (M$IRefs), before the CRC", NULL, O_IREFSPAD, OURS },
};
#define NOPTIONS (sizeof options / sizeof options[0])

/* ----- State ----- */

static struct {
	int jmptbl;		/* -a */
	long align;		/* -b= */
	int ansi;		/* -c */
	const char *edition;	/* -e=, -E= */
	int debug;		/* -g */
	const char *owner;	/* -gu= */
	int idata;		/* -i */
	int jmap;		/* -j */
	struct args libs;	/* -l= */
	const char *map;	/* -m, -s: "-" for standard output */
	int map_symbols;	/* -s */
	long stack;		/* -M=, in bytes */
	const char *name;	/* -n=, -N= */
	int name_last;		/* -N */
	const char *out;	/* -o=, -O= */
	const char *perms;	/* -p= */
	int raw;		/* -r */
	const char *base;	/* -r=<base> */
	const char *revision;	/* -R= */
	int sticky;		/* -S */
	int ucc;		/* -t=: Ultra C's style */
	long xalign;		/* -x= */
	struct args files;	/* in order */
	int c32;		/* -dialect=c32: library members in archive order */
	const char *namepad;	/* -namepad= */
	const char *irefspad;	/* -irefspad= */
	long jmptblpad;		/* -jmptblpad= */
	const char *stbnamepad;	/* -stbnamepad= */
	const char *dbgversion;	/* -dbgversion= */
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
	printf("Options of Microware's l68 (C 3.2's and Ultra C's), linked with GNU ld and elf2mod:\n");
	for (size_t i = 0; i < NOPTIONS; i++) {
		const struct option *o = &options[i];
		if (o->linkers == OURS)
			continue;
		char left[64];
		snprintf(left, sizeof left, "-%s%s", o->name, o->arg);
		const char *who = o->linkers == UCC ? " (Ultra C)" : o->linkers == C32 ? " (C 3.2)" : "";
		char *text = concat(o->help, who, o->does != NULL ? " [" : "", o->does != NULL ? o->does : "",
			o->does != NULL ? "]" : "", NULL);
		usage_line(left, text);
		free(text);
	}
	printf("Options of this program:\n");
	for (size_t i = 0; i < NOPTIONS; i++) {
		const struct option *o = &options[i];
		if (o->linkers != OURS)
			continue;
		char left[64];
		snprintf(left, sizeof left, "-%s%s", o->name, o->arg);
		usage_line(left, o->help);
	}
	usage_line("-gnu-prefix=<p>", "the GNU tools: a directory of them under their short names,\n"
		"                      or the prefix of their names (default m68k-elfos9)");
	usage_line("-elf-save=<s>", "where files converted to ELF go: beside (default), subdir, cache");
	usage_line("-elf-dir=<name>", "subdirectory, and directory in cache (default m68k-os9-elf)");
	usage_line("-elf-cache=<dir>", "cache directory (default $XDG_CACHE_HOME or ~/.cache)");
	printf("Files: ROF objects and libraries (converted; see -elf-save=), ELF objects, ar archives;\n");
	printf("a library given as a file is linked whole, one given with -l= is searched.\n");
	printf("Environment:\n");
	usage_line("M68K_OS9_GNU_PREFIX", "as -gnu-prefix=");
	usage_line("M68K_OS9_ELF", "-elf-... settings as save=...,dir=...,cache=...");
	usage_line("CLIB", "where sys.l is, for its equates (unless -l= names a sys.l)");
	usage_line("GRPUSER", "module owner, as -gu=: GROUP.USER, decimal (default 0.0)");
}

/* ----- Parsing ----- */

static void parse(int argc, char **argv);

/* -z[=PATH]: one argument per line; blank lines and lines starting with
 * '*' are skipped (as l68 does). */
static void read_z(const char *path)
{
	FILE *fp = path != NULL ? fopen(path, "r") : stdin;
	if (fp == NULL)
		error("cannot read %s", path);
	struct args words = { 0 };
	args_add(&words, os9_who);
	char line[1024];
	while (fgets(line, sizeof line, fp) != NULL) {
		char *p = line + strspn(line, " \t");
		size_t n = strcspn(p, "\r\n");
		while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t'))
			n--;
		p[n] = '\0';
		if (*p == '\0' || *p == '*')
			continue;
		args_add(&words, p);
	}
	if (fp != stdin)
		fclose(fp);
	/* The words stay: the options keep pointers into them. */
	parse(words.n, words.v);
}

static long number(const char *option, const char *value, int base)
{
	char *end;
	long n = strtol(value, &end, base);
	if (end == value || *end != '\0' || n < 0)
		error("-%s=%s: expected a number", option, value);
	return n;
}

static long alignment(const char *option, const char *value)
{
	long n = number(option, value, 0);
	if (n != 2 && n != 4 && n != 8 && n != 16)
		error("-%s=%s: the alignment must be 2, 4, 8 or 16", option, value);
	return n;
}

/* The number of bytes in hex digits VALUE (a leading $ or 0x allowed), two
 * digits to a byte; -1 if they aren't. */
static long hex_bytes(const char *value)
{
	if (*value == '$')
		value++;
	else if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
		value += 2;
	size_t n = strlen(value);
	if (n == 0 || n % 2 != 0 || strspn(value, "0123456789abcdefABCDEF") != n)
		return -1;
	return (long)(n / 2);
}

static void apply(const struct option *o, const char *value)
{
	switch (o->id) {
	case O_A: st.jmptbl = 1; break;
	case O_B: st.align = alignment("b", value); break;
	case O_C: st.ansi = 1; break;
	case O_E: st.edition = value; break;
	case O_F: break;
	case O_G: st.debug = 1; break;
	case O_GU: st.owner = value; break;
	case O_I: st.idata = 1; break;
	case O_J: st.jmap = 1; break;
	case O_L: args_add(&st.libs, value); break;
	case O_M: st.map = value != NULL ? value : "-"; break;
	case O_MM: {
		char *end;
		long n = strtol(value, &end, 0);
		if (end == value || (*end != '\0' && strcmp(end, "K") != 0 && strcmp(end, "k") != 0) || n < 0)
			error("-M=%s: expected a size in KB", value);
		st.stack = n * 1024;
		break;
	}
	case O_MT:
		if (strcmp(value, "e") != 0 && strcmp(value, "w") != 0 && strcmp(value, "q") != 0)
			error("-mt%s: expected -mte, -mtw or -mtq", value);
		break;
	case O_N: st.name = value; break;
	case O_NN:
		st.name_last = 1;
		if (value != NULL)
			st.name = value;
		break;
	case O_OO: case O_O: st.out = value; break;
	case O_P: st.perms = value; break;
	case O_R: st.raw = 1; st.base = value; break;
	case O_RR: st.revision = value; break;
	case O_S: st.map = value != NULL ? value : "-"; st.map_symbols = 1; break;
	case O_SS: st.sticky = 1; break;
	case O_T:
		if (strcmp(value, "os9_68k") != 0)
			error("-t=%s: only os9_68k (OS-9 for the 68000 family) is supported", value);
		st.ucc = 1;
		break;
	case O_V: os9_show = 1; break;
	case O_W: break;
	case O_X: st.xalign = alignment("x", value); break;
	case O_Z: read_z(value); break;
	case O_DIALECT:
		if (strcmp(value, "c32") != 0 && strcmp(value, "ucc") != 0)
			error("-dialect=%s: expected ucc or c32", value);
		st.c32 = strcmp(value, "c32") == 0;
		break;
	case O_NAMEPAD:
		if (hex_bytes(value) != 1)
			error("-namepad=%s: expected one byte in hex (two digits)", value);
		st.namepad = value;
		break;
	case O_DBGVERSION: {
		char *end;
		long v = strtol(value, &end, 0);
		if (end == value || *end != '\0' || v < 0 || v > 0xFFFF)
			error("-dbgversion=%s: expected a word (0 to 65535)", value);
		st.dbgversion = value;
		break;
	}
	case O_STBNAMEPAD:
		if (hex_bytes(value) != 1)
			error("-stbnamepad=%s: expected one byte in hex (two digits)", value);
		st.stbnamepad = value;
		break;
	case O_IREFSPAD:
		if (hex_bytes(value) < 1)
			error("-irefspad=%s: expected bytes in hex (an even number of digits)", value);
		st.irefspad = value;
		break;
	case O_JMPTBLPAD: {
		char *end;
		st.jmptblpad = strtol(value, &end, 10);
		if (end == value || *end != '\0' || st.jmptblpad < 0)
			error("-jmptblpad=%s: expected a number of entries", value);
		break;
	}
	}
}

/* Matches option ARG (with its '-'), case-sensitive; the longest name that
 * fits wins. */
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
	if (os9_tools_option(arg))
		return;
	if (os9conv_option(arg, OBJ_KINDS))
		return;
	/* A bare -E is ignored, as by Ultra C's l68. */
	if (strcmp(text, "E") == 0)
		return;

	for (size_t i = 0; i < NOPTIONS; i++) {
		const struct option *o = &options[i];
		size_t len = strlen(o->name);
		if (len <= bestlen || strncmp(text, o->name, len) != 0)
			continue;
		const char *rest = text + len;
		switch (o->kind) {
		case FLAG:
			if (*rest != '\0')
				continue;
			candidate = NULL;
			break;
		case VALUE:
			if (*rest != '=' || rest[1] == '\0')
				continue;
			candidate = rest + 1;
			break;
		case JOIN:
			if (*rest == '\0')
				continue;
			candidate = rest;
			break;
		case OPTVALUE:
		default:
			if (*rest == '=' && rest[1] != '\0')
				candidate = rest + 1;
			else if (*rest == '\0')
				candidate = NULL;
			else
				continue;
			break;
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

/* ----- Inputs ----- */

/* Whether input PATH is a library rather than one object: an ar archive, a
 * libgen library, or ROFs named .l (concatenated). */
static int is_library(const char *path)
{
	return file_is_ar(path) || file_is_libgen(path) || ci_eq(path_suffix(path), ".l");
}

/* A root psect's module header values, from its __os9_* symbols. */
struct header {
	int found;
	unsigned long typelang, attrev, stack, entry;
};

/* The __os9_* values of ELF object PATH (an archive has none here). */
static struct header read_header(const char *path)
{
	struct header h = { 0 };
	char *out = os9_temp(".nm");
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, path);
	int dry = os9_dry_run, show = os9_show;
	os9_dry_run = os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	os9_dry_run = dry, os9_show = show;
	args_free(&argv);
	if (status != 0)
		return h;
	FILE *fp = fopen(out, "r");
	if (fp == NULL)
		return h;
	char line[512];
	while (fgets(line, sizeof line, fp) != NULL) {
		unsigned long value;
		char type, name[256];
		if (sscanf(line, "%lx %c %255s", &value, &type, name) != 3)
			continue;
		if (strcmp(name, "__os9_typelang") == 0)
			h.typelang = value, h.found = 1;
		else if (strcmp(name, "__os9_attrev") == 0)
			h.attrev = value;
		else if (strcmp(name, "__os9_stack") == 0)
			h.stack = value;
		else if (strcmp(name, "__os9_entry") == 0)
			h.entry = value;
	}
	fclose(fp);
	return h;
}

/* Library psects of equates only give way (measured 2026-10-09 with both
 * Microware linkers, m68k-os9-l68.md, "Duplicate symbols"): a library member
 * that defines nothing but equates is taken in as any other, but its
 * equates clash with no earlier definition: an object's, or an earlier
 * member's, value wins (sys.l's io member and a driver's own V_ equate).
 * Duplicates between objects, and with a member that has code or data, stay
 * errors. Here: a copy of the archive ARCHIVE with those members' global
 * symbols weak (names also defined, in another member, as code or data, are
 * left strong); ARCHIVE itself where there are none. */
static char *equate_library(const char *archive)
{
	char *out = os9_temp(".nm");
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, "-g");
	args_add(&argv, "--defined-only");
	args_add(&argv, archive);
	int dry = os9_dry_run, show = os9_show;
	os9_dry_run = os9_show = 0;
	int status = os9_run(&argv, out, NULL);
	args_free(&argv);
	FILE *fp = status == 0 ? fopen(out, "r") : NULL;
	if (fp == NULL) {
		os9_dry_run = dry, os9_show = show;
		return xstrdup(archive);
	}
	/* per member (nm's "name:" lines): its names, and whether all are equates */
	struct args equates = { 0 }, others = { 0 }, member = { 0 };
	int only_abs = 1;
	char line[512];
	for (int done = 0; !done;) {
		done = fgets(line, sizeof line, fp) == NULL;
		line[strcspn(line, "\r\n")] = '\0';
		size_t n = strlen(line);
		if (done || (n > 0 && line[n - 1] == ':') || n == 0) {
			/* a member ends: its names go to equates or others */
			for (int i = 0; i < member.n; i++)
				args_add(only_abs ? &equates : &others, member.v[i]);
			args_free(&member);
			member = (struct args){ 0 };
			only_abs = 1;
			continue;
		}
		unsigned long value;
		char type, name[256];
		if (sscanf(line, "%lx %c %255s", &value, &type, name) != 3)
			continue;
		args_add(&member, name);
		if (type != 'A')
			only_abs = 0;
	}
	fclose(fp);
	FILE *list = NULL;
	char *names = os9_temp(".sym");
	for (int i = 0; i < equates.n; i++) {
		int other = 0;
		for (int j = 0; j < others.n && !other; j++)
			other = strcmp(equates.v[i], others.v[j]) == 0;
		if (other)
			continue;
		if (list == NULL && (list = fopen(names, "w")) == NULL)
			error("cannot write %s", names);
		fprintf(list, "%s\n", equates.v[i]);
	}
	args_free(&equates);
	args_free(&others);
	if (list == NULL) {
		os9_dry_run = dry, os9_show = show;
		return xstrdup(archive);
	}
	fclose(list);
	char *copy = os9_temp(".a");
	args_add(&argv, OBJCOPY);
	args_addf(&argv, "--weaken-symbols=%s", names);
	args_add(&argv, archive);
	args_add(&argv, copy);
	os9_dry_run = dry, os9_show = show;
	if (os9_run(&argv, NULL, NULL) != 0)
		error("%s: making its equates give way failed", archive);
	args_free(&argv);
	return copy;
}

/* ----- Ultra C's member order (-dialect=ucc) ----- */

/* Ultra C's l68 takes the library members a link needs in another order than
 * the archive's, which C 3.2's l68 (and ld) keep (m68k-os9-l68.md, "Library
 * member order"; measured 2026-10-16 and 2026-10-17): the libraries one by
 * one, in their order; in each, the names still unresolved in order (the root
 * objects', then those of the members linked from earlier libraries), each
 * linking the member defining it; a member linked puts the members of the same
 * library defining its unresolved references (in name order, as a ROF lists
 * them) on a stack, those not linked or on it already; the top is linked next.
 * A name only an earlier library defines stays unresolved. Here the front end
 * chooses the members so and gives ld them as objects, in that order. */

struct member {
	char *path;		/* extracted, or the input itself */
	struct args defs;	/* its global definitions */
	struct args refs;	/* its references, in name order, link-time expressions' terms included */
	int state;		/* 0, 1 on the stack, 2 linked */
	int library;		/* its -l= library's number from 1 (0: a root object's) */
};

static struct member *members;
static int nmembers;

static struct member *new_member(const char *path, int library)
{
	members = xrealloc(members, (nmembers + 1) * sizeof *members);
	struct member *m = &members[nmembers++];
	memset(m, 0, sizeof *m);
	m->path = xstrdup(path);
	m->library = library;
	return m;
}

/* The members of ar archive PATH, each into a file of its own (the symbol
 * table and long names left out), as new members of LIBRARY. 0 if it isn't
 * readable. */
static int extract_members(const char *path, int library)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;
	char magic[8], h[60];
	if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, "!<arch>\n", 8) != 0) {
		fclose(fp);
		return 0;
	}
	while (fread(h, 1, 60, fp) == 60) {
		char sz[11];
		memcpy(sz, h + 48, 10);
		sz[10] = '\0';
		long size = strtol(sz, NULL, 10);
		if (size < 0)
			break;
		char *data = xmalloc((size_t)size + 1);
		if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
			free(data);
			break;
		}
		if (size % 2)
			fgetc(fp);
		if (h[0] == '/' && (h[1] == ' ' || h[1] == '/' || strncmp(h, "/SYM64/", 7) == 0)) {
			free(data);
			continue;
		}
		char *out = os9_temp(".o");
		FILE *o = fopen(out, "wb");
		if (o == NULL || fwrite(data, 1, (size_t)size, o) != (size_t)size || fclose(o) != 0)
			error("cannot write %s", out);
		free(data);
		new_member(out, library);
	}
	fclose(fp);
	return 1;
}

static int by_name(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* nm -g -p of PATH into a file (NULL if it fails). */
static char *nm_list(const char *path)
{
	char *out = os9_temp(".nm");
	struct args argv = { 0 };
	args_add(&argv, NM);
	args_add(&argv, "-g");
	args_add(&argv, "-p");
	args_add(&argv, path);
	int dry = os9_dry_run, show = os9_show;
	os9_dry_run = os9_show = 0;
	int status = os9_run(&argv, out, os9_temp(".log"));
	os9_dry_run = dry, os9_show = show;
	args_free(&argv);
	return status == 0 ? out : NULL;
}

/* A line of nm's list into M: a definition, or a reference (to a link-time
 * expression's symbol: into EXPRS). */
static void nm_line(struct member *m, const char *line, struct args *exprs)
{
	char a[64], b[64], name[256];
	int k = sscanf(line, "%63s %63s %255s", a, b, name);
	if (k == 2 && strcmp(a, "U") == 0) {
		if (strncmp(b, R68X_PREFIX, strlen(R68X_PREFIX)) == 0)
			args_add(exprs, b);
		else
			args_add(&m->refs, b);
	} else if (k == 3 && strcmp(b, "U") != 0) {
		args_add(&m->defs, name);
	}
}

/* M's definitions and references, from nm (LIST: an archive's list, its
 * lines from M's "member:" line on; else nm of M); the terms of the
 * link-time expressions it refers to (its .r68x notes) as references too, as
 * l68 has them in the ROF's expressions. */
static void read_symbols(struct member *m, FILE *list)
{
	struct args exprs = { 0 }, argv = { 0 };
	char line[512];
	if (list != NULL) {
		while (fgets(line, sizeof line, list) != NULL) {
			size_t n = strcspn(line, "\r\n");
			if (n == 0)
				break;
			if (line[n - 1] != ':')
				nm_line(m, line, &exprs);
		}
	} else {
		char *out = nm_list(m->path);
		FILE *fp = out != NULL ? fopen(out, "r") : NULL;
		while (fp != NULL && fgets(line, sizeof line, fp) != NULL)
			nm_line(m, line, &exprs);
		if (fp != NULL)
			fclose(fp);
	}
	int dry = os9_dry_run, show = os9_show, status;
	FILE *fp;
	os9_dry_run = os9_show = 0;
	if (exprs.n > 0) {
		char *notes = os9_temp(".r68x");
		args_add(&argv, OBJCOPY);
		args_addf(&argv, "--dump-section=" R68X_SECTION "=%s", notes);
		args_add(&argv, m->path);
		args_add(&argv, os9_temp(".o"));
		status = os9_run(&argv, NULL, os9_temp(".log"));
		args_free(&argv);
		fp = status == 0 ? fopen(notes, "r") : NULL;
		char text[4096];
		while (fp != NULL && fgets(text, sizeof text, fp) != NULL) {
			text[strcspn(text, "\r\n")] = '\0';
			char *eq = strchr(text, '=');
			if (eq == NULL)
				continue;
			*eq = '\0';
			int used = 0;
			for (int i = 0; i < exprs.n && !used; i++)
				used = strcmp(exprs.v[i], text) == 0;
			struct r68x x;
			if (!used || !r68x_parse(eq + 1, &x))
				continue;
			for (int i = 0; i < x.n; i++)
				args_add(&m->refs, x.t[i].sym);
			r68x_free(&x);
		}
		if (fp != NULL)
			fclose(fp);
	}
	os9_dry_run = dry, os9_show = show;
	args_free(&exprs);
	/* in name order, each once */
	if (m->refs.n == 0)
		return;
	qsort(m->refs.v, (size_t)m->refs.n, sizeof *m->refs.v, by_name);
	int k = 0;
	for (int i = 0; i < m->refs.n; i++) {
		if (k > 0 && strcmp(m->refs.v[k - 1], m->refs.v[i]) == 0)
			free(m->refs.v[i]);
		else
			m->refs.v[k++] = m->refs.v[i];
	}
	m->refs.n = k;
	m->refs.v[k] = NULL;
}

static int defined(const char *name)
{
	for (int i = 0; i < nmembers; i++)
		if (members[i].state == 2 || !members[i].library)
			for (int j = 0; j < members[i].defs.n; j++)
				if (strcmp(members[i].defs.v[j], name) == 0)
					return 1;
	return 0;
}

/* The first member of library LIBRARY defining NAME, or NULL. */
static struct member *definer(const char *name, int library)
{
	for (int i = 0; i < nmembers; i++)
		if (members[i].library == library)
			for (int j = 0; j < members[i].defs.n; j++)
				if (strcmp(members[i].defs.v[j], name) == 0)
					return &members[i];
	return NULL;
}

/* Links M and what it needs, as Ultra C's l68 (see above), adding their
 * paths to ORDER. */
static void link_member(struct member *m, struct args *order)
{
	struct member **stack = xmalloc((size_t)(nmembers + 1) * sizeof *stack);
	int top = 0;
	stack[top++] = m;
	m->state = 1;
	while (top > 0) {
		struct member *t = stack[--top];
		t->state = 2;
		args_add(order, t->path);
		for (int i = 0; i < t->refs.n; i++) {
			if (defined(t->refs.v[i]))
				continue;
			struct member *d = definer(t->refs.v[i], t->library);
			if (d != NULL && d->state == 0) {
				d->state = 1;
				stack[top++] = d;
			}
		}
	}
	free(stack);
}

/* The members of the -l= libraries LIBS (ELF archives) the root inputs ROOTS
 * (ELF objects and archives linked whole, in order) need, in Ultra C's
 * order, into ORDER. */
static void ucc_members(const struct args *roots, const struct args *libs, struct args *order)
{
	for (int i = 0; i < roots->n; i++) {
		if (file_is_ar(roots->v[i])) {
			int first = nmembers;
			extract_members(roots->v[i], 0);
			for (int j = first; j < nmembers; j++)
				read_symbols(&members[j], NULL);
		} else {
			read_symbols(new_member(roots->v[i], 0), NULL);
		}
	}
	int nroots = nmembers;
	for (int i = 0; i < libs->n; i++) {
		int first = nmembers;
		/* an object (a ROF of one psect, converted) is a library of one
		 * member */
		if (!file_is_ar(libs->v[i])) {
			read_symbols(new_member(libs->v[i], i + 1), NULL);
			continue;
		}
		if (!extract_members(libs->v[i], i + 1))
			error("%s: cannot read the archive", libs->v[i]);
		/* one nm of the archive, its "member:" blocks in the members'
		 * order; nm of each member where they don't match */
		char *out = nm_list(libs->v[i]);
		FILE *list = out != NULL ? fopen(out, "r") : NULL;
		int blocks = 0;
		char line[512];
		while (list != NULL && fgets(line, sizeof line, list) != NULL) {
			size_t n = strcspn(line, "\r\n");
			blocks += n > 0 && line[n - 1] == ':' && strchr(line, ' ') == NULL;
		}
		if (list != NULL && blocks == nmembers - first) {
			rewind(list);
			for (int j = first; j < nmembers; j++) {
				while (fgets(line, sizeof line, list) != NULL) {
					size_t n = strcspn(line, "\r\n");
					if (n > 0 && line[n - 1] == ':' && strchr(line, ' ') == NULL)
						break;
				}
				read_symbols(&members[j], list);
			}
		} else {
			for (int j = first; j < nmembers; j++)
				read_symbols(&members[j], NULL);
		}
		if (list != NULL)
			fclose(list);
	}
	/* the names to resolve, in order: the root objects', then those of the
	 * members linked, library by library */
	struct args names = { 0 };
	for (int i = 0; i < nroots; i++)
		for (int j = 0; j < members[i].refs.n; j++)
			args_add(&names, members[i].refs.v[j]);
	for (int lib = 1; lib <= libs->n; lib++) {
		int linked = order->n, n = names.n;
		for (int k = 0; k < n; k++) {
			if (defined(names.v[k]))
				continue;
			struct member *d = definer(names.v[k], lib);
			if (d != NULL && d->state == 0)
				link_member(d, order);
		}
		for (int k = linked; k < order->n; k++)
			for (int i = nroots; i < nmembers; i++)
				if (strcmp(members[i].path, order->v[k]) == 0)
					for (int j = 0; j < members[i].refs.n; j++)
						if (!defined(members[i].refs.v[j]))
							args_add(&names, members[i].refs.v[j]);
	}
	args_free(&names);
}

/* ----- Linking ----- */

/* The linker script, laid out as elf2mod expects (as m68k-os9-xcc's): code
 * at BASE, data a6-relative from DATA, elf2mod's jump table, and l68's
 * linker symbols (the plain ones only without -c). ALIGN: -b=. l68 biases
 * the data offsets of programs and trap libraries by 0x8000 (data from
 * -0x8000, a6 0x8000 into it, as cstart.r sets it up), not those of other
 * module types, whose static storage is addressed from its start. */
static char *write_script(const char *base, long align, int plain, const char *data, int raw)
{
	char *path = os9_temp(".ld");
	FILE *fp = fopen(path, "w");
	if (fp == NULL)
		error("cannot write %s", path);
	char sub[32] = "";
	if (align > 1)
		snprintf(sub, sizeof sub, " SUBALIGN(%ld)", align);
	fprintf(fp,
		"OUTPUT_FORMAT(\"elf32-m68k\")\n"
		"OUTPUT_ARCH(m68k)\n"
		"PROVIDE (__jmptbl_size = 0);\n"
		"SECTIONS {\n"
		"    . = %s;\n"
		"    .text :%s { *(.text .text.* .rodata .rodata.*) . = ALIGN(%ld); }\n"
		"    /* l68's symbols for the module's start, name and end: elf2mod sets\n"
		"       every reference to them, so these values are only placeholders. */\n"
		"    PROVIDE (_btext = ADDR(.text)); PROVIDE (_bname = ADDR(.text)); PROVIDE (_etext = ADDR(.text));\n",
		base, sub, align > 2 ? align : 2L);
	if (plain)
		fprintf(fp, "    PROVIDE (btext = ADDR(.text)); PROVIDE (bname = ADDR(.text)); PROVIDE (etext = ADDR(.text));\n");
	fprintf(fp,
		"    . = %s;\n"
		"    PROVIDE (_bdata = .);\n", data);
	if (plain)
		fprintf(fp, "    PROVIDE (bdata = .);\n");
	/* -b= pads each psect's code and data to its alignment, as l68 does. */
	long a = align > 2 ? align : 2;
	fprintf(fp,
		"    .bss (NOLOAD) :%s { *(.bss .bss.*) *(COMMON) . = ALIGN(%ld); }\n"
		"    .data :%s { *(.data .data.*) . = ALIGN(%ld); _jmptbl = .; . += __jmptbl_size; _ejmptbl = .; }\n"
		"    .remote.data :%s { *(.remote.data .remote.data.*) . = ALIGN(%ld); }\n"
		"    .remote.bss (NOLOAD) :%s { *(.remote.bss .remote.bss.*) . = ALIGN(%ld); }\n"
		"    PROVIDE (_enddata = .);\n",
		sub, a, sub, a, sub, a, sub, a);
	if (plain)
		fprintf(fp, "    PROVIDE (end = .);\n");
	if (raw) {
		/* l68 -r's own: the data size rounded up to $100, and the
		 * initialized data block's and reference tables' starts, after
		 * the code (placeholders: elf2mod sets the references) */
		fprintf(fp, "    PROVIDE (_dsize = ABSOLUTE (ALIGN (_enddata - _bdata, 0x100)));\n"
			"    PROVIDE (_bidata = ADDR(.text)); PROVIDE (_birefs = ADDR(.text));\n");
		if (plain)
			fprintf(fp, "    PROVIDE (dsize = _dsize); PROVIDE (edata = ADDR(.text));\n");
	}
	fprintf(fp, "}\n");
	if (fclose(fp) != 0)
		error("cannot write %s", path);
	return path;
}

/* Adds input PATH to the link; the driver would take a .r file for a Ratfor
 * source, so whatever isn't .o or .a goes to the linker directly. */
static void add_input(struct args *link, const char *path)
{
	const char *ext = strrchr(lbasename(path), '.');
	if (ext == NULL || (strcmp(ext, ".o") != 0 && strcmp(ext, ".a") != 0))
		args_add(link, "-Xlinker");
	args_add(link, path);
}

int main(int argc, char **argv)
{
	os9_who = "m68k-os9-l68";
	static const char *const own_names[] = { "l68", NULL };
	os9_tools_init(argv[0], own_names);
	os9conv_environment(OBJ_KINDS);
	if (argc < 2) {
		usage();
		return 0;
	}
	parse(argc, argv);
	if (st.files.n == 0)
		error("no files (%s -? for the usage)", os9_who);

	/* sys.l, for rof2elf's equates: one given with -l=, else CLIB's. */
	for (int i = 0; i < st.libs.n; i++)
		if (ci_eq(lbasename(st.libs.v[i]), "sys.l")) {
			char *abs = host_abspath(st.libs.v[i]);
			os9conv.libdir = path_dir(abs);
			free(abs);
		}

	/* The inputs as ELF, in order; the root psect among them. */
	struct args objects = { 0 };
	int *whole = xcalloc(st.files.n + 1, sizeof *whole);
	struct header root = { 0 };
	const char *root_file = NULL;
	for (int i = 0; i < st.files.n; i++) {
		const char *file = st.files.v[i];
		int library = is_library(file);
		char *elf = os9conv_as_elf(file, library);
		if (library) {
			char *e = equate_library(elf);
			free(elf);
			elf = e;
		}
		args_add(&objects, elf);
		whole[i] = library;
		if (!library) {
			struct header h = read_header(elf);
			if (h.found && root_file != NULL)
				error("root psect found in both %s and %s", root_file, file);
			if (h.found) {
				root = h;
				root_file = file;
			}
		}
		free(elf);
	}
	if (root_file == NULL)
		error("no root psect found");

	/* The module's layout, from the root psect's type and language, as l68
	 * lays it out (m68k-os9-l68.md, "What the linkers actually write"). */
	unsigned type = (root.typelang >> 8) & 0xFF, lang = root.typelang & 0xFF;
	int prog = type == 0x01 || type == 0x0B || (type == 0x0C && st.idata);
	const char *header = prog ? "0x48" : lang == 0 ? "0x30" : "0x3C";
	int name_last = st.name_last || type == 0x0B || !prog;
	if (st.stack > 0 && !prog && !st.raw)
		error("-M=: the module (type $%02X, language %u) has no stack field", type, lang);

	/* The output and the module's name. */
	const char *output = st.out != NULL ? st.out : "output";
	char *name = st.name != NULL ? xstrdup(st.name) : os9_module_name(output);

	struct os9link l = { .gcc = GCC, .elf2mod = ELF2MOD, .who = os9_who };
	args_add(&l.link, "-mos9call");
	args_add(&l.link, "-nostdlib");
	args_add(&l.link, "-Wl,-q");
	args_add(&l.link, "-Wl,--no-check-sections");
	/* GNU ld's map, also without -m/-s: elf2mod lists the initialized
	 * data's pointers by object from it, as l68 */
	const char *map_stdout = NULL;
	const char *map = st.map;
	if (map == NULL)
		map = os9_temp(".map");
	else if (strcmp(map, "-") == 0)
		map = map_stdout = os9_temp(".map");
	args_addf(&l.link, "-Wl,-Map=%s", map);
	args_add(&l.link, "-T");
	char *base = st.raw ? concat("0x", st.base != NULL ? st.base : "0", NULL) : xstrdup("0x400000");
	/* Raw output (-r) isn't biased either, whatever the type (as l68). */
	int biased = !st.raw && (type == 0x01 || type == 0x0B);
	args_add(&l.link, write_script(base, st.align, !st.ansi, biased ? "-0x8000" : "0", st.raw));
	free(base);
	for (int i = 0; i < objects.n; i++) {
		if (whole[i])
			args_add(&l.link, "-Wl,--whole-archive");
		add_input(&l.link, objects.v[i]);
		if (whole[i])
			args_add(&l.link, "-Wl,--no-whole-archive");
	}
	/* -l= libraries after all files, in one pass in their order, as l68
	 * searches them; then libgcc, for objects GCC compiled. With Ultra C's
	 * order the members it needs, chosen here, as objects. */
	struct args libs = { 0 }, order = { 0 };
	for (int i = 0; i < st.libs.n; i++) {
		char *lib0 = os9conv_as_elf(st.libs.v[i], 1);
		char *lib = equate_library(lib0);
		free(lib0);
		args_add(&libs, lib);
		free(lib);
	}
	if (!st.c32 && libs.n > 0 && !os9_dry_run) {
		ucc_members(&objects, &libs, &order);
		for (int i = 0; i < order.n; i++)
			add_input(&l.link, order.v[i]);
	} else {
		for (int i = 0; i < libs.n; i++)
			add_input(&l.link, libs.v[i]);
	}
	args_free(&libs);
	args_free(&order);
	args_add(&l.link, "-lgcc");

	char *elf = concat(output, ".elf", NULL);
	l.elf = elf;
	l.module = output;
	l.name = name;
	l.no_jmptbl = !st.jmptbl;
	l.jmptbl_spare = st.jmptblpad;
	if (st.jmptblpad > 0) {
		args_add(&l.mod, "--jmptbl-spare");
		args_addf(&l.mod, "%ld", st.jmptblpad);
	}
	args_add(&l.mod, "--header");
	args_add(&l.mod, header);
	args_add(&l.mod, "--ld-map");
	args_add(&l.mod, map);
	if (name_last)
		args_add(&l.mod, "-N");
	if (st.raw)
		args_add(&l.mod, "--raw");
	if (st.namepad != NULL) {
		args_add(&l.mod, "--name-pad-byte");
		args_add(&l.mod, st.namepad);
	}
	if (st.dbgversion != NULL) {
		args_add(&l.mod, "--dbg-version");
		args_add(&l.mod, st.dbgversion);
	}
	if (st.stbnamepad != NULL) {
		args_add(&l.mod, "--stb-name-pad-byte");
		args_add(&l.mod, st.stbnamepad);
	}
	if (st.irefspad != NULL) {
		args_add(&l.mod, "--irefs-pad");
		args_add(&l.mod, st.irefspad);
	}
	long xalign = st.xalign > st.align ? st.xalign : st.align;
	if (xalign > 2 && type == 0x0B && !st.raw) {
		/* A trap library's code stays at 0x48; l68 pads after its name
		 * instead, by what the name would have needed before the code. */
		size_t nm = strlen(name) + 1;
		nm += nm % 2;
		long pad = (xalign - (long)((0x48 + nm) % xalign)) % xalign;
		if (pad > 0) {
			args_add(&l.mod, "--name-pad-zeros");
			args_addf(&l.mod, "%ld", pad);
		}
	} else if (xalign > 2) {
		args_add(&l.mod, "-x");
		args_addf(&l.mod, "%ld", xalign);
	}
	if (st.edition != NULL) {
		args_add(&l.mod, "-e");
		args_add(&l.mod, st.edition);
	}
	if (st.revision != NULL) {
		args_add(&l.mod, "-r");
		args_add(&l.mod, st.revision);
	}
	if (st.owner != NULL) {
		args_add(&l.mod, "-u");
		args_add(&l.mod, st.owner);
	}
	if (st.perms != NULL) {
		args_add(&l.mod, "-p");
		args_add(&l.mod, st.perms);
	}
	if (st.sticky)
		args_add(&l.mod, "-S");
	if (st.stack > 0) {
		args_add(&l.mod, "-s");
		args_addf(&l.mod, "%lu", root.stack + (unsigned long)st.stack);
	}
	if (st.debug)
		args_add(&l.mod, st.ucc ? "--stb=ucc" : "-g");

	int status = os9_link(&l);
	if (status != 0)
		error("linking %s failed", output);
	/* -m/-s without a path: the map on standard output. GNU ld's map for
	 * now, not l68's layout. */
	if (map_stdout != NULL && !os9_dry_run) {
		FILE *fp = fopen(map_stdout, "r");
		char line[1024];
		while (fp != NULL && fgets(line, sizeof line, fp) != NULL)
			fputs(line, stdout);
		if (fp != NULL)
			fclose(fp);
	}

	free(elf);
	free(name);
	free(whole);
	args_free(&objects);
	args_free(&l.link);
	args_free(&l.mod);
	os9_cleanup();
	return 0;
}
