# m68k-os9-libgen

`m68k-os9-libgen` is a drop-in for Ultra C's librarian `libgen`, for makefiles that build
libraries: it takes `libgen`'s options and writes an `ar` archive, concatenated ROFs, or
`libgen`'s own format, and gives `libgen`'s listings, also of libraries `libgen` can't read.

Microware C 3.2 has no librarian: its libraries are ROFs concatenated (`merge a.r b.r >lib.l` on
OS-9, `copy /b` on DOS), so C 3.2 makefiles need no front end for them. GCC users make archives
with `ar rcs lib.a a.o b.o`, through `m68k-os9-ar`.

## Usage

    m68k-os9-libgen [options] <files>...

| Option | `libgen` | Here |
|---|---|---|
| `-b=<n>` | output buffer size, in KB | ignored |
| `-c` | create a library | writes the library in the `-obj=` format |
| `-e=<n>` | library edition | `-obj=ucc`: the header's edition word; ignored otherwise (no such field) |
| `-f=<path>` | output file for the listings | the same |
| `-l`, `-lu` | psect names, in columns or one per line | the same |
| `-li` | the library's identification | the same; for an `ar` archive or concatenated ROFs, only the name |
| `-ln` | `nm`-style list (`lib:psect:value T name`) | the same |
| `-le`, `-ll` | per psect: sizes, definitions and references; local references | not supported yet (an error) |
| `-mts` | mark the library thread-safe | `-obj=ucc`: the header's flag; ignored otherwise |
| `-o=<path>` | output file (default `output.l`) | the same |
| `-p=<psect>` | only that psect's information | not supported (an error, also with `-c`, where `libgen` accepts it silently) |
| `-z[=<path>]` | file names from standard input or `<path>` | the same; options in the list are taken too, as `libgen` takes them |
| `-obj=elf\|rof\|ucc` | ours | the output format (below); also `M68K_OS9_ELF`'s `obj=` |

Options are case-insensitive (`-C -O=`), as in `libgen`. `-obj=` and `-elf…` are read before
`-o=` and `-e=`. `-?` prints the usage in `libgen`'s order, each option with what differs in
square brackets, generated from the parser's own table of options.

As `libgen` does: of several listing options only the last counts (`-ln -lu` lists as `-lu`);
`-c` together with a listing lists the inputs and writes no library; neither `-c` nor a listing
gives a warning ("to create library, use the -c option") and the usage; `-c` writes over an
existing file; a symbol defined twice is an error, for each such symbol, and no library is
written.

## Inputs

ELF objects, ROF objects (converted with [rof2elf](rof2elf.md) for an ELF archive, with the
conversion settings of [m68k-os9-xcc.md](m68k-os9-xcc.md)), and libraries of any kind, `ar`
archives, concatenated ROFs and `libgen`'s, taken member by member. `libgen` takes concatenated
ROFs psect by psect, but refuses a `libgen` library ("file 'x.l' is not a relocatable module");
taking those and `ar` archives too is an extension. A member's psect name is its ROF's own, or for
an ELF member the file symbol `rof2elf` writes, else its member name without `.o`.

## Output formats

- **`-obj=elf`** (the default): an `ar` archive with its index, under the name the makefile gives
  (`lib.l`). Our tools tell `ar` from ROF by the first bytes, so it links as it is. Member names
  are made unique (`d1/a.r` and `d2/a.r` don't replace each other, as plain `ar r` would).
- **`-obj=rof`:** concatenated ROFs, which both Microware linkers read. ROF inputs are copied byte
  for byte (`a.r b.r` gives the bytes of `cat a.r b.r`); ELF members and `libgen` libraries go
  through [elf2rof](elf2rof.md).

## libgen's format

`rof2elf` reads the format (see [rof2elf.md](rof2elf.md)); writing it needs what the manual
(*Using Ultra C/C++*, chapter 9) leaves out:

- **The hash table** of the global definitions always has 378 buckets; a name's bucket is the sum
  of its bytes modulo 377, and each bucket's chain lists its definitions in reverse order.

## Differences and limitations

- `-le`, `-ll` and `-p=` aren't supported.
- `-e=` and `-mts` have no effect on `ar` archives and concatenated ROFs, which have no such fields.
- The listings and the inputs take `ar` archives, concatenated ROFs and `libgen` libraries alike,
  where `libgen`'s listings read only its own libraries ("not made with libgen, try using rdump")
  and `libgen -c` refuses `libgen` libraries as inputs.

## Tests

`tests/r68` cases for the librarian: `-obj=ucc` of a psect whose entry point isn't at offset 0
(`libgen-entry-offset`), and a library in Ultra C 1.x `libgen`'s older layout read back
(`libgen-old-layout`); `l68-member-libraries` links libraries of each kind. On Windows builds, the
listings are compared with the Linux build's.
