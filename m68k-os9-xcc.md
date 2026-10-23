# m68k-os9-xcc and m68k-os9-ucc

`m68k-os9-xcc` and `m68k-os9-ucc` are drop-in replacements for Microware's compiler drivers, for
existing makefiles: they take the options of Microware C 3.2's `xcc`/`cc` or of Ultra C's `xcc`,
and compile with GCC (`m68k-elfos9-gcc`), assemble `.a` sources with
[m68k-os9-r68](m68k-os9-r68.md) and link with [m68k-os9-l68](m68k-os9-l68.md). A makefile only
needs its `CC=` or `XCC=` changed. For GCC's own options there is
[m68k-os9-gcc](m68k-os9-gcc.md).

- `m68k-os9-xcc`: Microware C 3.2's options (Ultra C's `-mode=compat`), the default.
- `m68k-os9-ucc`: Ultra C's own options (`-mode=ucc`).

It is one program. The name it runs as sets the default mode (a name containing `ucc` gives
Ultra C's), and `-mode=compat|c89|ucc` anywhere on the line selects it, as in Ultra C's `xcc`:
`m68k-os9-xcc -mode=ucc` is `m68k-os9-ucc`. It also works under the short names `xcc`, `cc` and
`ucc` (see "Finding the tools"). The two option sets are parsed separately: most short options
mean different things in them (`-a`, `-c`, `-e`, `-k`, `-m`, `-o`, `-r`, `-s`, `-t`, `-x`).

## Microware's files

Makefiles expect Microware's headers and libraries, which can't be shipped: the drivers use the
user's own copies, found as Microware's drivers find them, and convert libraries and objects
from ROF with [rof2elf](rof2elf.md) as needed.

### Where they are found

In C 3.2 mode, as C 3.2's `xcc` does:

- headers: the `-v=<dir>` directories, in order, then `CDEF` (one directory; default
  `\os9c\defs`);
- libraries: `cstart.r` and the default libraries from `CLIB` (one directory; default
  `\os9c\lib`), which `-w=<dir>` replaces (the last one counts); `-l=<file>` adds libraries, as
  given, before the default ones;
- names are looked up ignoring case (`CLIB.L`, `CSTART.R` on a DOS installation).

In Ultra C mode, see "Ultra C mode". `GRPUSER` goes to the linker, which takes the module's owner
from it, as `l68` does.

### Converting them

Every Microware input is converted when there's no converted file or it's older than the
original or than the `rof2elf` that would convert it: `.r` files on the command line, `-l=`
libraries, and the default `cstart.r` and libraries. Libraries are also indexed (`ranlib`). Each
conversion writes a temporary file and renames it, so parallel builds (`make -j`) never see half a
file. An input that's already ELF (see "Objects") isn't converted.

Where the converted files go:

| Setting | `DIR/cstart.r` → | `DIR/clib.l` → |
|---|---|---|
| `cache` (default) | `CACHE/m68k-os9-elf/DIR/cstart.o` | `CACHE/m68k-os9-elf/DIR/clib.a` |
| `beside` | `DIR/cstart.o` | `DIR/libclib.a` |
| `subdir` | `DIR/m68k-os9-elf/cstart.o` | `DIR/m68k-os9-elf/clib.a` |

The default writes nothing into the directories of Microware's files. `beside` uses names that
can't clash with Microware's: Microware never uses `.o`, and the `lib` prefix keeps a library
clear of r68 sources, which are `.a`; `lib<name>.a` also lets GNU tools use the libraries directly
(`-L$CLIB -lclib`). Where the chosen place isn't writable (a read-only installation), the file
goes to the cache, with a note; so does a ROF object a makefile named `.o`, which would be its
own `beside` target. A long cache path plus a long library path can exceed Windows' 260-character
limit; a short cache directory avoids it.

### Settings

| Option | In `M68K_OS9_ELF` | Values | Default |
|---|---|---|---|
| `-elf-save=` | `save=` | `cache`, `beside`, `subdir` | `cache` |
| `-elf-dir=` | `dir=` | the subdirectory's name, also used in the cache | `m68k-os9-elf` |
| `-elf-cache=` | `cache=` | the cache's root | `${XDG_CACHE_HOME:-$HOME/.cache}` |
| `-obj=` | `obj=` | `elf`, `rof`, `ucc`: what `-r` writes into `.r` files (see "Objects"); with `-a`, `r68` or `gas`: the syntax `-a` writes | `elf`; with `-a`, from `-asm=` |
| `-asm=` | none | `auto`, `r68`, `gas`: the syntax of `.a` files, read (`auto`: detected) and written by `-a` unless `-obj=` says (`auto`: `r68`) | `auto` |

The environment variable takes the same values, comma-separated, for makefiles that shouldn't
change (`M68K_OS9_ELF=save=subdir,obj=rof`); options override it. Neither Microware driver has
these options, so `-elf…` and `-obj=` are checked before `-e` and `-o`. `m68k-os9-l68`,
`m68k-os9-libgen` and `m68k-os9-r68` read the same variable and take the `-elf-*` options for
their own conversions, and `m68k-os9-libgen` and `m68k-os9-r68` `obj=` for their output.

`obj=` names what a kit writes, and each front end applies it to its own output: `elf` an ELF
object, or an `ar` archive for a library; `rof` (C 3.2's kit) a ROF, or concatenated ROFs; `ucc`
(Ultra C's kit) a ROF, or `libgen`'s format. For objects `ucc` is the same as `rof` (ROF edition
9, which both Microware linkers read). In `M68K_OS9_ELF`, `obj=` takes these object formats only:
an assembly syntax there would turn a whole build's objects into sources. So the assembly values
are command-line options: `m68k-os9-r68 -obj=gas`, and `-a -obj=r68|gas` here (without `-a` an
error).

**`-asm=`** names the syntax of `.a` files in both directions: inputs (`auto`, the default,
detects it; `r68` or `gas` force it) and, where no `-obj=` says, what `-a` writes (`auto`
writing r68). The detection reads the statements up to the first decisive one: a Microware
directive (`psect`, `vsect`, `ends`, `use`, `nam`, `ttl`, `opt`, `spc`, `pag`) decides r68; a
known `gas` directive (`.text`, `.globl`, `.long`, ...), a register written with `%` (in r68 `%`
starts a binary number), or GCC's `#APP`/`#NO_APP` and line markers decide `gas`. Plain
instructions, labels, `equ`, `dc` and comments decide nothing; a file with nothing decisive is
r68's.

## Finding the tools

All the front ends (`m68k-os9-xcc`, `-l68`, `-r68`, `-libgen`, `-gcc`) find the programs they run
alike:

- **Our own tools** without a prefix (`elf2mod`, `rof2elf`, `elf2rof`, `r68togas`, `gastor68`):
  in the front end's own directory, else on `PATH`.
- **The other front ends** (`xcc` runs `r68` and `l68`): `m68k-os9-NAME` in the front end's own
  directory, then on `PATH`; then `NAME` with the front end's own prefix in its directory. The
  prefix is what precedes the front end's own name in the name it runs as: none for `xcc`, `cc`
  or `ucc`, so `xcc` runs `r68` and `l68` from its directory when there is no `m68k-os9-r68` or
  `m68k-os9-l68`. The full names come first, so a directory may also hold Microware's `r68` or
  `l68` under their own names without ours losing to them.
- **The GNU tools** (`gcc`, `as`, `ar`, `ranlib`, `nm`, `objcopy`, `objdump`, `readelf`; `ld` and
  the compiler's passes are found by GCC's driver):
  - `-gnu-prefix=<p>` (`xcc`, `l68`, `r68`, `libgen`), or `M68K_OS9_GNU_PREFIX` (also
    `m68k-os9-gcc`; the option wins): an existing directory holds them under their short names;
    anything else is the prefix of their names, `-` added unless it ends in one
    (`C:\gnu\bin\m68k-elfos9` gives `C:\gnu\bin\m68k-elfos9-nm`). A front end given the option
    passes it on to the programs it runs through the environment variable.
  - Without them (the default prefix, `m68k-elfos9`): `m68k-elfos9-NAME` in the front end's own
    directory; then `NAME` there, but only when the front end runs under its short name (in a
    directory of short names, where a short `gcc` or `nm` can't be the host's; never the front
    end itself, so `m68k-os9-gcc` copied as `gcc` runs the real GCC); then `m68k-elfos9-NAME`
    on `PATH`.

**Microware's `r68` found as `r68`** works too: `xcc` gives `r68` only the options both take
(`-o=`, `-q`, `-m2`) and the object format through `M68K_OS9_ELF`, which Microware's ignores; a
ROF where an ELF object was asked for is converted with `rof2elf`. (C 3.2's DOS `r68` can't run
on Windows directly; Ultra C's `r68` runs natively.)

## Objects

Makefiles name their objects `.r` (`RELS/foo.r: foo.c`, then link `RELS/foo.r`), so `-r` writes the
object under that name. By default it's an ELF object in a `.r` file, which only our tools link:
they tell ELF from ROF by the first bytes, use ELF objects as they are and convert ROF ones.
`-obj=rof` writes a real ROF instead (through [elf2rof](elf2rof.md)), for linking with
Microware's `l68`. The same goes for `.o` inputs, which some makefiles use for ROF objects:
whatever the suffix, the first bytes decide.

## C 3.2 mode

### Compiling

GCC gets `-mpcrel -ma6rel -mos9call -mbuiltin=os9call -mos9newline -mos9regfunc -std=gnu89
-fno-strict-aliasing -w`, the CPU and floating-point options, and `-mos9stkchk` (stack checking,
with `cstart.r`'s `_stkcheck`, as C 3.2 does) unless `-s`. C 3.2's predefined names come from GCC
itself on `m68k-elfos9`.

- **`-std=gnu89`** takes K&R code.
- **`-mos9newline`:** `'\n'` is a carriage return, as in Microware's compilers, and `\l` a line
  feed (see GCC's manual).
- **`-mos9regfunc`:** `register` on a function declared inside a block (`register char *p,
  *f();`), which C 3.2 accepts and ignores, is accepted; the function is an ordinary external.
- **`-fno-strict-aliasing`:** C 3.2 assumes nothing from pointer types, and old code often reads a
  value through a pointer to another type, which GCC's optimizer may otherwise reorder or drop.
  The code is about 0.1% bigger for it.
- **Common symbols:** an uninitialized global defined in two files (`int x;` in both) is an
  error, as with C 3.2, whose `l68` reports "duplicate symbol names"; GCC's default `-fno-common`
  does the same.

**Optimization** follows C 3.2's levels, which select its optimizer passes:

| `xcc` | C 3.2 | GCC |
|---|---|---|
| (none), `-o=1` | `c68`, then `o68`'s peephole pass: the compact default | `-Os` |
| `-o=2` | also `c68 -c` (more values in registers; the code no smaller) | `-O2` |
| `-o=0`, `-o` alone | `c68` only, no optimizer | `-O0` |

The last `-o` given counts. `-Os` gives the smallest code: on a large set of old C 3.2 programs
(more than 54k lines in more than 180 files) it is smaller than C 3.2's own optimized output,
`-O2` larger.

### C 3.2's assembler text

A line starting with `@` (the rest of the line) and the lines between `#asm` and `#endasm` go
into the assembler output where they stand, at file level or inside a function, as in C 3.2.
`#include` is expanded inside `#asm`; macros are not. The driver:

1. replaces each such line or block with a numbered marker (a basic `asm` statement), keeping the
   source's line numbers for GCC's messages;
2. compiles to assembler with `-fno-toplevel-reorder`, so that file-level markers stay in order;
3. converts GCC's output to r68 syntax with [gastor68](gastor68.md);
4. puts each kept text in place of its marker;
5. assembles the result with `m68k-os9-r68`, as C 3.2 assembles `c68`'s output; with `-a` that
   r68 source is the output (`-obj=gas` is then an error).

Text inside a function can't rely on C 3.2's registers or stack frame, and gets a note. Such a
file is compiled without inlining and loop header copying, which would copy the text and its
labels; any other copy GCC makes is an error naming the block's line (then `-o=0`). Files without
such text are compiled to objects directly. A block in a part the preprocessor leaves out stays
out, so the common `#ifndef __GNUC__` / `#asm` / `#include "x.a"` / `#endasm` / `#endif` works.

C 3.2's storage class **`direct`**, which changes nothing in C 3.2's code, is removed where it is
one (not in `struct direct`).

### Registers across calls

With `-mos9call`, GCC assumes that a function declared with a prototype preserves a0 and a1, as
Microware C 3.2's functions and libraries do, and keeps values in them across such calls; d0, d1
and, with `-m68881`, fp0/fp1 are assumed changed. A call through an unprototyped (K&R)
declaration, as in Microware's own headers, assumes a0 and a1 changed as well. For assembler
routines called from GCC code:

- A routine declared with a prototype must preserve a0, a1 and d2-d7, and d1 unless it carries an
  argument or the result, as Microware's own routines do.
- A value can't be passed from one call to the next in a register by itself. GCC decides the
  order in which it evaluates a call's arguments and which registers it uses in between, so a
  routine that leaves a value in a register for a later call to pick up (a pointer in a1 for the
  system call the next library function makes, say) works only with Microware's compilers, which
  happen to leave that register alone. The workaround is to reserve the register in the source
  files that use such a routine, with a file-scope register variable, for example in the
  routine's header:

  ```c
  #ifdef __GNUC__
  register void *os9_a1_reserved __asm__("a1");  /* a1 carries a value between calls */
  #endif
  ```

  GCC then never uses a1 in those files, not for variables, temporaries or spills (as
  `-ffixed-a1` would do), so the value stays in it until the next call, in whatever order the
  arguments are evaluated; functions called in between preserve it (Microware's do, and
  `-mos9call` functions save a1 if they use it). The cost is one address register less in those
  files. Not covered: a GCC runtime routine called in between (for example `memcpy` for a struct
  passed by value) may still change the register, and a built-in system call
  (`-mbuiltin=os9call`) that needs a1 for a parameter may conflict with the reservation.
  Otherwise, wrap the calls in one assembler routine.

### Options

Options are case-insensitive, `=` optional where C 3.2 allows it.

| Option | Meaning in C 3.2 | Here |
|---|---|---|
| `-a` | assembler output (`.a`) | `-S`, then [gastor68](gastor68.md) into r68 syntax; `-obj=gas` (or `-asm=gas`) keeps GCC's syntax |
| `-bg` | adhesive (sticky) module | not supported (an error) |
| `-bp` | show the phases' command lines | the commands run |
| `-c` | comments in assembler output | with `-a`, `-fverbose-asm` |
| `-cs=<file>` | another root psect | that file instead of `cstart.r` |
| `-d<name>[=<value>]` | define | `-D` |
| `-e=<n>` | module edition | the linker's `-e=` |
| `-f=<path>`, `-fd=<path>` | output (execution or data directory) | the output path, as given |
| `-g` | source-level debugging | `-g`, and a symbol module (`.stb`); no `.dbg` |
| `-h` (Ultra C's compat mode) | don't run the phases | prints them only |
| `-i` | link with `cio` | `cio.l` before the C library |
| `-j` | no jump table | the link without one: an error if the program needs one |
| `-k=<n>[w\|l][cw\|cl][f]` | target, offset sizes, 68881 | `0`: `-m68000`; `2`: `-m68020`; `f`: `-m68881`; `l`, `cl` (long offsets): an error |
| `-l=<file>` | another library | linked (converted if ROF) |
| `-lo=<opts>` | options for `l68` | not supported (an error) |
| `-m=<n>[k]` | more stack, in K | `<n>` K added to the module's stack size |
| `-n=<name>` | module name | the module's name |
| `-nl` | no default libraries | none (`cstart.r` still, unless `-cs=`) |
| `-nv` | OS-9000 only | ignored |
| `-o[=<n>]` | optimization | see "Compiling" |
| `-q` | quiet | quiet |
| `-r[=<dir>]` | stop at `.r` files, in `<dir>` | objects written as `<dir>/<name>.r` |
| `-s` | no stack checking | no `-mos9stkchk` |
| `-t=<dir>`, `-t?` | temporary files; list targets | `TMPDIR`; lists the targets |
| `-to=<name>` | target system | `osk` only, else an error |
| `-tp=<cpu>[...]` | target processor | `68k`, `020` (with the 68881, as in C 3.2), `cpu32`; others an error |
| `-u<name>` | undefine | `-U` |
| `-v=<dir>` | header directory | `-I`, before `CDEF` |
| `-w=<dir>` | library directory | replaces `CLIB` |
| `-x` | math trap | `-mos9math`, and the C library as C 3.2 picks it |
| `-z[=<file>]` | options from a file | read the same way |

The default libraries, after `cstart.r`, as C 3.2's `xcc` picks them (the `n` libraries go with
`c68 -t`, which calls `math.l`'s floating-point routines instead of the trap):

| Options | Libraries |
|---|---|
| (default) | `clibn.l math.l sys.l` |
| `-x` | `clib.l sys.l` |
| `-k=2` | `clib020n.l math.l sys.l` |
| `-k=2f`, `-tp=020` | `clib020h.l math881.l sys.l` |
| `-k=2 -x`, `-k=2f -x` | `clib020.l sys.l` |

GCC code doesn't need `math.l`/`math881.l` (its floating point is libgcc's, or with
`-mos9math` the math module's), but they stay for Microware objects linked in.

### Source files

By suffix, in any case: `.c` compiled; `.r`, `.o` and `.l` linked (converted as needed); `.a`
assembled with [m68k-os9-r68](m68k-os9-r68.md) (`-m2` for the 68020 and the CPU32), into an
object for the link or, with `-r`, into `<dir>/<name>.r` (a ROF with `-obj=rof`). An `.a` file in
`gas` syntax (as `xcc -a -obj=gas` writes; detected, or `-asm=gas`) goes to GCC's assembler
instead, with its line ends made LF. Both Microware drivers reject `.s` and `.S` ("no recognized
suffix"), and so does this one; r68 sources named `.s` are assembled by calling `m68k-os9-r68`
directly. The default output name is the first source file's, without its suffix (`pp.c` makes
`pp`).

### Linking

As C 3.2's `xcc` does, the driver runs the linker, `m68k-os9-l68`: with `cstart.r` or `-cs=`, the
objects as given, the `-l=` libraries and then the default ones, `-o=`, `-n=`, `-e=`, `-M=` and
`-g` from the options, `-a` (the jump table) unless `-j`, and `-dialect=c32` (C 3.2's order of
library members); `-gnu-prefix=`, `-elf-*` and `-obj=` go along. `-bp` and `-h` show that command.

The jump table serves calls between source files, which the linker resolves. Within one source
file with more than 32 KB of code, the assembler resolves PC-relative calls itself and fails
("value of -65864 too large for field of 2 bytes"); such a file has to be split.

## Ultra C mode

Ultra C's own options, as `m68k-os9-ucc` or with `-mode=ucc`/`c89`. What each does was measured
with Ultra C's `xcc -h -b`, which shows the phases' command lines without running them.

| Option | Meaning in Ultra C | Here |
|---|---|---|
| `-a[=warn\|err]` | ANSI source mode (default: extended ANSI) | `-std=c89 -pedantic[-errors]`, and `l68 -c` (as Ultra C's xcc) |
| `-bc` | K&R source mode | `-std=gnu89`, K&R's `cstart.r` and libraries (`sys_clib.l` ... `math.l`), and the names `OSK` and `mc68000` |
| `-b`, `-h` | show phases, don't run them | as `-bp` and `-h` in C 3.2 mode |
| `-c`, `-n`, `-x=`, `-m=<phase>=<n>` | constant pointers, data layout, skipped phases, phase stacks | ignored |
| `-co` | C code as comments in assembler | `-fverbose-asm` |
| `-cq` | C++ comments in C | accepted (`-std=gnu89` takes them, also with `-a`) |
| `-cs=`, `-d`, `-f=`, `-g`, `-l=`, `-u`, `-v=`, `-z` | as in C 3.2 mode | as in C 3.2 mode; `-g` gives Ultra C's `.stb` |
| `-fd=` | output in the data directory | `l68 -O=` |
| `-cw` | warnings | `-Wall` (else `-w`) |
| `-e=<phase>[=<dir>]` | stop after a phase | `as`: objects (`.r`, as `-r`); `be`, `ao`: assembler (`.a`, GNU syntax); others (I-code): an error |
| `-i` | C shared library module | `csl.l` and `os_csl.l` instead of `clib.l` and `os_lib.l` |
| `-j`, `-y`, `-cx`, `-q…`, `-edgx` | I-code libraries, cross references, C++ | an error |
| `-k` | no default libraries | none, and no `acstart.r` either (as Ultra C's xcc) |
| `-mt[=…]` | multi-threading | an error unless `none` |
| `-mw=<dir>` | MWOS tree | instead of `MWOS` |
| `-o[=<n>]` | optimization 0-7 (default 7; `-o` alone 0) | `0`: `-O0`, `1`-`3`: `-O1`, `4`-`7`: `-O2` |
| `-p` | preprocess | `-E`, to standard output (Ultra C's goes to standard error); `-p<mode>`, `-p=<dir>`: an error |
| `-r` | no stack checking | no `-mos9stkchk` (else `_stkcheck`, which Ultra C's `acstart.r` has too) |
| `-s=<n>`, `-t=<n>` | space and time weights (default 1, 1) | `-Os` where space outweighs time; `_SPACE_FACTOR`, `_TIME_FACTOR` |
| `-sl=<dir>`, `-w=<dir>` | library directories | as below |
| `-td=<dir>` | temporary files | as C 3.2's `-t=` |
| `-to=osk`, `-tp=68k\|68020\|cpu32`, `-t?` | targets | `-m68000`, `-m68020`, `-mcpu=cpu32` (`-msoft-float`) |
| `-ol=<opt>` | an option for the linker | `l68 -<opt>` (`-ol=a` for the jump table) |
| `-<phase>=<opt>` | an option for another phase | an error |

**Compiling:** GCC with `-mpcrel -ma6rel -mos9call=ucc -mbuiltin=os9call -mos9newline`, Ultra C's
predefined names (`_UCC`, `_MAJOR_REV=2`, `_MINOR_REV=5`, `_SPACE_FACTOR`, `_TIME_FACTOR`, `_OSK`,
`_MPF68000`/`_MPF68020`/`_MPFCPU32`, `_MPF68K`, `_FPF881`, `_BIG_END`), headers from the `-v=`
directories, then `MWOS/SRC/DEFS` and `MWOS/OS9/SRC/DEFS`.

- **`-mos9call=ucc`:** Ultra C's convention, in which a variadic call's unnamed arguments go on
  the stack (see GCC's manual).
- **`-mos9newline`:** Ultra C's compiler makes `'\n'` a carriage return in all its modes, as C 3.2
  does. With `-a` it also makes `'\r'` a line feed and `\l` the letter `l`: GCC gets
  `-mos9newline=swapped` then.
- **ANSI mode renames four calls:** Ultra C's compiler calls `printf`, `scanf`, `fopen` and
  `freopen` as `__ansi_printf`, `__ansi_scanf`, `__ansi_fopen` and `__ansi_freopen` (in the
  default mode and with `-a`, not with `-bc`), which the library has besides the plain ones: the
  plain `printf` and `scanf` take the first argument after the format from d1, C 3.2's placement,
  the `__ansi_` ones from the stack. GCC gets `-Dprintf=__ansi_printf` and so on.
- **K&R mode (`-bc`)** calls the plain names, so GCC gets `-mos9call`, C 3.2's placement, instead
  of `-mos9call=ucc`. `direct` is removed there as in C 3.2 mode. Ultra C has no `@` lines or
  `#asm` blocks, in any mode.
- **`va_start`** is a builtin of Ultra C's compiler: a `stdarg.h` in a temporary directory,
  searched first, includes GCC's instead (with `_VA_LIST`, Ultra C's name for the type, defined as
  `va_list`). Under `-mos9call=ucc` GCC's `va_list` is a plain pointer to the stack arguments, as
  Ultra C's.

`.a` sources go to `r68` as Ultra C's xcc runs it: the predefined names as `-a=`, `-bt`, `-m0` or
`-m2`, the header directories as `-u=`.

**Linking:** as Ultra C's xcc, through `l68`: `l68 -t=os9_68k acstart.r -o=<out> [-c] -f=orowoe
-x=<n> -b=<n> [-g] -l=<-l= libraries> -l=clib.l -l=os_lib.l -l=sys.l [-ol= options] <objects>`,
`-x=`/`-b=` 2 (16 for the 68020, 4 for the CPU32); `.l` files on the line go to `l68` as files
(linked whole). Libraries and `acstart.r` are looked up in `-w=`'s directory, else
`MWOS/OS9/68020/LIB` (68020), `MWOS/OS9/CPU32/LIB` (CPU32) and `MWOS/OS9/68000/LIB`, then the
`-sl=` directories, regardless of case; not found, they go to `l68` as named. No `-a`: as with
Ultra C's xcc, a program needing a jump table needs `-ol=a`. The module is named after the one
input file, or `output` with more than one, as Ultra C's xcc names it.

**Ultra C's libraries** are in `libgen`'s format, which `rof2elf` reads (see
[rof2elf.md](rof2elf.md)); they are converted on demand like any other Microware library.

## Usage message

`-?` (and no arguments) prints the usage of the current mode: every option the mode accepts, in
Microware's order for that mode, each with what the driver does where that differs from
Microware's, in square brackets (ignored, not supported, or how it's mapped); then the driver's
own options, the files it takes by suffix, and the environment it reads (`CDEF`, `CLIB` in C 3.2
mode; `MWOS` in Ultra C mode; `M68K_OS9_GNU_PREFIX`, `M68K_OS9_ELF`, `GRPUSER`). `-t?` lists the
targets the mode accepts. The message is generated from the parser's own table of options, so
the two can't differ.

Options without an effect on the program (`-nv`, weights, phase stack sizes) are ignored.
Options asking for something the driver can't produce (long offsets, adhesive modules, linker
options, I-code, C++, OS-9000) are errors naming the option, with status 1, rather than a build of
a different program than the makefile expects.

## Module names

Without `-n=`, the module is named after the output file, as `l68` names it, but a final `.mod`
(in lower case only) is dropped: on the host a module `a` can then be kept as `a.mod`, as
`elf2mod`'s default output is. The case is kept as given, and any other extension stays in the
name (`cdilogo.img`, `font1.ft`; Sony's `.Mod` code modules keep theirs). `m68k-os9-l68` and
`m68k-os9-gcc` name modules the same way.

## Limitations

- No `.dbg` source-level debug file; `-g` gives the symbol module (`.stb`).
- Long data and code offsets (`-k=...l`, `-k=...cl`) and adhesive modules (`-bg`) aren't
  available.
- One source file with more than 32 KB of code can't be compiled (see "Linking").

## Tests

- **`tests/r68`** cases for the driver: the commands it runs in both modes, the `-o` levels,
  `'\n'` in every mode, aliasing and common symbols, `@` lines, `#asm` blocks and `direct`, the
  conversion cache, `.a` files of either syntax, tools found by their short names.
- **On CD-i Emulator's 605**, with C 3.2's `CLIB` and `CDEF`: programs built with the default
  options, `-x`, `-r` then linking ELF or ROF `.r` files, `-m=`, `-e=`, `-n=`, `-i`, a program of
  six source files with 124 KB of code through a jump table (`-j` then an error), and programs in
  Ultra C mode (ANSI with Ultra C's `printf` and a variadic function, and K&R), whose output and
  status are those of Ultra C's own build.
- **On a real CD-i 605 and on CD-i Emulator's:** `test/abi-exec`'s OS-9 tests (with Microware
  C 3.2's libraries, the module tests, newlib, floating point), also at `-Os` with this driver's
  C 3.2 compiler flags.
- **Code size and compilation** on a large set of old C 3.2 programs (more than 54k lines in more
  than 180 files).
