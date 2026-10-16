/* m68k-os9-gcc: compiles and links an OS-9/68K program into a module in one
 * step, with newlib's OS-9 runtime (libgloss: -specs=os9.specs) and elf2mod.
 *
 *   m68k-os9-gcc [GCC OPTIONS] [-Wm,ELF2MOD OPTIONS] FILES... [-o PROG]
 *
 * Without -c, -S, -E or -M, it links PROG.elf (kept for debugging) and
 * makes it into the module PROG (a.out by default), named after PROG (a
 * final lower-case .mod dropped). When the code is too large for 16-bit
 * calls (more than 32 KB), the first link fails with "relocation truncated"
 * errors; m68k-os9-gcc then links again with as large a jump table as
 * elf2mod asks for, which it fills (os9link.c, as m68k-os9-xcc).
 * -Wm,OPTION,... passes options to elf2mod: -Wm,-g for a .stb symbol
 * module, -Wm,-e,EDITION, -Wm,-s,STACK, -Wm,-n,NAME and so on.
 *
 * Environment: OS9GCC_CC (default m68k-elfos9-gcc), ELF2MOD (default
 * elf2mod).
 *
 * In C rather than a shell script so that it runs on Windows too, on the
 * front ends' shared code (hostsys.c runs the programs through libiberty).
 *
 * MIT License; see elf2mod.c.
 */

#include "hostsys.h"
#include "os9conv.h"
#include "os9link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libiberty.h>

#define GCC	"m68k-elfos9-gcc"
#define ELF2MOD	"elf2mod"
#define SPECS	"-specs=os9.specs"

/* Options after which GCC doesn't link. */
static int no_link(const char *arg)
{
	static const char *const exact[] = {
		"-c", "-S", "-E", "-M", "-MM", "-fsyntax-only", "--version", "--help", "-v", NULL
	};
	for (int i = 0; exact[i] != NULL; i++)
		if (strcmp(arg, exact[i]) == 0)
			return 1;
	return strncmp(arg, "-print-", 7) == 0 || strncmp(arg, "-dump", 5) == 0;
}

int main(int argc, char **argv)
{
	os9_who = "m68k-os9-gcc";
	const char *gcc = getenv("OS9GCC_CC");
	const char *elf2mod = getenv("ELF2MOD");
	if (gcc == NULL || *gcc == '\0')
		gcc = GCC;
	if (elf2mod == NULL || *elf2mod == '\0')
		elf2mod = ELF2MOD;

	struct args gccargs = { 0 }, modargs = { 0 };
	const char *out = NULL;
	int link = 1;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strncmp(arg, "-Wm,", 4) == 0) {
			/* -Wm,A,B: elf2mod's options A and B */
			char *list = xstrdup(arg + 4);
			for (char *p = list, *comma; p != NULL; p = comma) {
				comma = strchr(p, ',');
				if (comma != NULL)
					*comma++ = '\0';
				args_add(&modargs, p);
			}
			free(list);
		} else if (strcmp(arg, "-o") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "%s: -o needs a file name\n", os9_who);
				return 1;
			}
			out = argv[++i];
		} else if (strncmp(arg, "-o", 2) == 0) {
			out = arg + 2;
		} else {
			if (no_link(arg))
				link = 0;
			args_add(&gccargs, arg);
		}
	}

	int status;
	if (!link) {
		struct args run = { 0 };
		args_add(&run, gcc);
		args_add(&run, SPECS);
		args_append(&run, &gccargs);
		if (out != NULL) {
			args_add(&run, "-o");
			args_add(&run, out);
		}
		status = os9_run(&run, NULL, NULL);
		args_free(&run);
	} else {
		if (out == NULL)
			out = "a.out";
		struct os9link l = { .gcc = gcc, .elf2mod = elf2mod, .who = os9_who };
		args_add(&l.link, SPECS);
		args_append(&l.link, &gccargs);
		char *elf = concat(out, ".elf", NULL);
		char *name = os9_module_name(out);
		l.elf = elf;
		l.module = out;
		l.name = name;
		args_append(&l.mod, &modargs);
		status = os9_link(&l);
		args_free(&l.link);
		args_free(&l.mod);
		free(elf);
		free(name);
	}
	args_free(&gccargs);
	args_free(&modargs);
	os9_cleanup();
	return status >= 0 && status <= 255 ? status : 1;
}
