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

## Objects

Makefiles name their objects `.r` (`RELS/foo.r: foo.c`, then link `RELS/foo.r`), so `-r` writes the
object under that name. By default it's an ELF object in a `.r` file, which only our tools link:
they tell ELF from ROF by the first bytes, use ELF objects as they are and convert ROF ones.
`-obj=rof` writes a real ROF instead (through [elf2rof](elf2rof.md)), for linking with
Microware's `l68`. The same goes for `.o` inputs, which some makefiles use for ROF objects:
whatever the suffix, the first bytes decide.

## C 3.2 mode

### Compiling

- **`-std=gnu89`** takes K&R code.

The last `-o` given counts. `-Os` gives the smallest code: on a large set of old C 3.2 programs
(more than 54k lines in more than 180 files) it is smaller than C 3.2's own optimized output,
`-O2` larger.

### C 3.2's assembler text

1. replaces each such line or block with a numbered marker (a basic `asm` statement), keeping the
   source's line numbers for GCC's messages;
3. converts GCC's output to r68 syntax with [gastor68](gastor68.md);
4. puts each kept text in place of its marker;
5. assembles the result with `m68k-os9-r68`, as C 3.2 assembles `c68`'s output; with `-a` that
   r68 source is the output (`-obj=gas` is then an error).

### Options

Options are case-insensitive, `=` optional where C 3.2 allows it.

| Option | Meaning in C 3.2 | Here |
|---|---|---|
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

### Linking

The jump table serves calls between source files, which the linker resolves. Within one source
file with more than 32 KB of code, the assembler resolves PC-relative calls itself and fails
("value of -65864 too large for field of 2 bytes"); such a file has to be split.

## Ultra C mode

| Option | Meaning in Ultra C | Here |
|---|---|---|
| `-b`, `-h` | show phases, don't run them | as `-bp` and `-h` in C 3.2 mode |
| `-co` | C code as comments in assembler | `-fverbose-asm` |
| `-cs=`, `-d`, `-f=`, `-g`, `-l=`, `-u`, `-v=`, `-z` | as in C 3.2 mode | as in C 3.2 mode; `-g` gives Ultra C's `.stb` |
| `-cw` | warnings | `-Wall` (else `-w`) |
| `-mw=<dir>` | MWOS tree | instead of `MWOS` |
| `-o[=<n>]` | optimization 0-7 (default 7; `-o` alone 0) | `0`: `-O0`, `1`-`3`: `-O1`, `4`-`7`: `-O2` |
| `-sl=<dir>`, `-w=<dir>` | library directories | as below |
| `-td=<dir>` | temporary files | as C 3.2's `-t=` |

**Ultra C's libraries** are in `libgen`'s format, which `rof2elf` reads (see
[rof2elf.md](rof2elf.md)); they are converted on demand like any other Microware library.

## Usage message

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

- One source file with more than 32 KB of code can't be compiled (see "Linking").

## Tests

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
