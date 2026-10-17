/* hostsys.h: what m68k-os9-xcc needs from the host system, for POSIX and
 * Windows: running programs (through libiberty's pex_one), file times,
 * renaming over a file, directories, finding a file regardless of case,
 * absolute paths, the temporary directory. Paths are allocated with
 * libiberty's xmalloc, which exits when out of memory.
 *
 * MIT License; see elf2mod.c.
 */

#ifndef HOSTSYS_H
#define HOSTSYS_H

#include <stddef.h>
#include <time.h>

/* Runs ARGV[0] (looked up on PATH) with ARGV, a null-terminated list, and
 * waits for it. OUT and ERR, if not null, are files its standard output and
 * standard error go to (created or truncated). Returns its exit status, or -1
 * if it could not be run (reported). */
int host_run(const char *const argv[], const char *out, const char *err);

/* The modification time of PATH, or (time_t)-1 if it doesn't exist. */
time_t host_mtime(const char *path);

/* Renames FROM to TO, replacing TO if it exists. Returns 0, or -1. */
int host_replace(const char *from, const char *to);

/* Creates directory PATH and the ones above it that are missing. Returns 0,
 * or -1. */
int host_mkdirs(const char *path);

/* Removes empty directory PATH. Returns 0, or -1. */
int host_rmdir(const char *path);

/* Tests whether files can be created in directory PATH (it must exist). */
int host_writable_dir(const char *path);

/* Looks for NAME in directory DIR regardless of case. Returns the path of the
 * file found (allocated), or null. */
char *host_find_nocase(const char *dir, const char *name);

/* The absolute form of PATH, which needn't exist (allocated), with '/' as the
 * separator. */
char *host_abspath(const char *path);

/* The directory for temporary files: TMPDIR, TMP or TEMP, else the system's
 * (allocated, without a trailing separator). */
char *host_tempdir(void);

/* A number for unique temporary names: the process ID. */
long host_pid(void);

/* Whether character C separates path components. */
int host_is_sep(int c);

/* Whether PATH is an existing directory. */
int host_is_dir(const char *path);

/* The directory of this program's executable (allocated, '/' as the
 * separator), from the system, else from ARGV0 when it has a directory; null
 * if neither tells. */
char *host_self_dir(const char *argv0);

/* Program NAME in directory DIR (on Windows also NAME.exe): its path
 * (allocated), or null if there's no such file. */
char *host_program_in(const char *dir, const char *name);

/* Program NAME found on PATH, as host_program_in: its path (allocated), or
 * null. */
char *host_program_on_path(const char *name);

/* Sets environment variable NAME to VALUE for the programs run after it, or
 * removes it (VALUE null). */
void host_setenv(const char *name, const char *value);

#endif
