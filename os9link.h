/* os9link.h: what m68k-os9-xcc and m68k-os9-gcc share: argument vectors,
 * running commands (shown or only printed), and linking a program into an
 * OS-9 module, with elf2mod's jump table when the code needs one.
 *
 * MIT License; see elf2mod.c.
 */

#ifndef OS9LINK_H
#define OS9LINK_H

/* A growing, null-terminated argument vector. */
struct args {
	char **v;
	int n, size;
};

void args_add(struct args *a, const char *arg);
void args_addf(struct args *a, const char *format, ...);
void args_append(struct args *a, const struct args *more);
void args_free(struct args *a);

/* How commands run: shown before they run (C 3.2's -bp), or only shown
 * (Ultra C's -h). */
extern int os9_show, os9_dry_run;

/* Runs ARGV (see host_run), showing it as asked. Returns its exit status. */
int os9_run(const struct args *argv, const char *out, const char *err);

/* A temporary file name, unique to this process, ending in SUFFIX
 * (allocated); removed by os9_cleanup. They go to DIR if set (C 3.2's -t=),
 * else to the host's temporary directory. */
char *os9_temp(const char *suffix);
void os9_set_tempdir(const char *dir);
void os9_cleanup(void);
/* A temporary directory, created (allocated; null if it can't be), and a
 * file put in one, both removed by os9_cleanup. */
char *os9_temp_dir(void);
void os9_temp_add(const char *path);

/* Finding the programs to run (see "Finding the tools" in m68k-os9-xcc.md).
 * os9_tools_init: this program's directory, and the prefix of the name it
 * runs as: NAMES are its own short names ("xcc", "cc", ...), so that
 * m68k-os9-xcc has the prefix "m68k-os9-" and xcc none. */
void os9_tools_init(const char *argv0, const char *const names[]);

/* -gnu-prefix=VALUE, which overrides M68K_OS9_GNU_PREFIX: a directory of
 * the GNU tools under their short names, or the prefix of their names ("-"
 * added unless it ends in one). */
void os9_set_gnu_prefix(const char *value);
/* Whether ARG is -gnu-prefix=...; then it is taken. */
int os9_tools_option(const char *arg);

/* The program to run for GNU tool TOOL ("gcc", "nm", ...): from the prefix
 * set, else m68k-elfos9-TOOL in this program's directory, TOOL there when
 * this program runs under its short name, else m68k-elfos9-TOOL on PATH. */
const char *os9_gnu(const char *tool);

/* The program to run for one of our tools without a prefix ("elf2mod",
 * "rof2elf", ...): from this program's directory, else from PATH. */
const char *os9_tool(const char *name);

/* The program to run for another front end, by its short name ("r68",
 * "l68", ...): m68k-os9-NAME in this program's directory, then on PATH;
 * then NAME with this program's prefix in its directory (r68 for xcc). */
const char *os9_front(const char *name);

struct os9link {
	const char *gcc;	/* the driver, e.g. m68k-elfos9-gcc */
	const char *elf2mod;
	struct args link;	/* the driver's link arguments, without -o */
	const char *elf;	/* the linked ELF file */
	const char *module;	/* the module made from it */
	const char *name;	/* its module name */
	struct args mod;	/* more elf2mod options (-e, -s, -g, ...) */
	int no_jmptbl;		/* fail rather than use a jump table */
	const char *who;	/* the program, for messages */
};

/* Links L->link into L->elf; when the code is too large for 16-bit calls
 * (the link fails with truncated relocations only), links again with as
 * large a jump table as elf2mod asks for. Then makes L->module. Returns 0,
 * or nonzero (reported). */
int os9_link(struct os9link *l);

#endif
