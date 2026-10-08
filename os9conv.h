/* os9conv.h: what the m68k-os9-* front ends share besides linking (see
 * os9link.h): messages, case-insensitive names, paths, and converting
 * Microware's ROF objects and libraries to ELF on demand, with the settings
 * of M68K_OS9_ELF and the -elf-* and -obj= options (see m68k-os9-xcc.md).
 *
 * MIT License; see elf2mod.c.
 */

#ifndef OS9CONV_H
#define OS9CONV_H

#include <stddef.h>

/* The program, for messages; quiet suppresses notes. */
extern const char *os9_who;
extern int os9_quiet;

/* Reports an error (the program's name first) and exits with status 1,
 * after removing the temporary files. */
void os9_error(const char *format, ...);
/* Reports something worth knowing, unless quiet. */
void os9_note(const char *format, ...);

/* Case-insensitive comparisons (strcasecmp isn't everywhere). */
int ci_eq(const char *a, const char *b);
int ci_prefix(const char *s, const char *prefix);

/* PATH's suffix, with its dot ("" if none). */
const char *path_suffix(const char *path);
/* PATH's base name without its suffix (allocated); lowercase if LOWER. */
char *path_stem(const char *path, int lower);
/* The directory part of absolute path PATH (allocated). */
char *path_dir(const char *path);
/* Whether file PATH starts with BYTES. */
int file_starts_with(const char *path, const char *bytes, size_t length);

/* The module name for output file OUTPUT, as all our front ends make it
 * (m68k-os9-xcc.md, "Module names"): its base name, with only a final
 * lower-case ".mod" dropped and the case kept (allocated). */
char *os9_module_name(const char *output);

/* Where converted files go, and what -obj= asks for. */
enum os9_save { SAVE_BESIDE, SAVE_SUBDIR, SAVE_CACHE };
enum os9_obj { OBJ_ELF, OBJ_ROF, OBJ_UCC, OBJ_GAS };

extern struct os9conv {
	enum os9_save save;
	const char *elfdir;	/* -elf-dir= (default m68k-os9-elf) */
	const char *cache;	/* -elf-cache= */
	enum os9_obj obj;	/* -obj= */
	const char *libdir;	/* where sys.l is, for rof2elf -e (null: CLIB) */
} os9conv;

/* -elf-save=VALUE; -obj=VALUE, of the kinds in ALLOWED (a mask of
 * 1 << enum os9_obj), named in an error by OPTION. */
void os9conv_set_save(const char *value);
void os9conv_set_obj(const char *value, unsigned allowed, const char *option);
/* M68K_OS9_ELF: save=...,dir=...,cache=...,obj=...; obj= takes object
 * formats only (elf, rof, ucc), and only those in ALLOWED. */
void os9conv_environment(unsigned allowed);
/* Handles the shared options -elf-save=, -elf-dir=, -elf-cache= and -obj=
 * (ARG with its '-'); returns whether ARG was one of them. */
int os9conv_option(const char *arg, unsigned allowed);

/* The library directory's sys.l (any case), whose equates rof2elf folds
 * into the code (-e); null if none. */
const char *os9conv_sys_l(void);

/* INPUT as ELF: itself if it is (an object, or an ar archive), else its
 * conversion (LIBRARY: an ar archive), made or remade when older than INPUT
 * or sys.l (allocated). */
char *os9conv_as_elf(const char *input, int library);

/* Whether file PATH is ELF, an ar archive, or a libgen library. */
int file_is_elf(const char *path);
int file_is_ar(const char *path);
int file_is_libgen(const char *path);

#endif
