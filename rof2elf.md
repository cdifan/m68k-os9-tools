# rof2elf

`rof2elf` converts Microware OS-9/68000 ROF objects (`.r`) and libraries (`.l`) into m68k ELF
relocatables and `ar` archives, so that they can be linked with GNU `ld` together with code from
the [m68k-elfos9 GCC](https://github.com/cdifan/toolchaincdi) and made into an OS-9 module with
[elf2mod](elf2mod.md). The front ends ([m68k-os9-xcc](m68k-os9-xcc.md),
[m68k-os9-l68](m68k-os9-l68.md), [m68k-os9-libgen](m68k-os9-libgen.md),
[m68k-os9-r68](m68k-os9-r68.md)) run it on demand. Its reverse is [elf2rof](elf2rof.md).

## Usage

    rof2elf [-o <output>] [-l] <input>

- A ROF object becomes an ELF relocatable (`cstart.r` → `cstart.o`).
- A library becomes an `ar` archive (`clib.l` → `clib.a`), one member per psect; run `ranlib` on
  it before linking. Both kinds of library are read: ROFs concatenated (sometimes just one), as
  Microware C 3.2's libraries are, and the format Ultra C's `libgen` makes (type 1, the 68000's),
  as most of Ultra C's libraries are. A psect converts to the same ELF object from either. A
  library with one ROF becomes an archive too, unless `<output>` is named `.o`.
- `-l` lists the ROFs in `<input>`.

For example, with Microware's C library (from your own installation; Microware's files are not
included here):

    rof2elf cstart.r
    rof2elf clib.l && m68k-elfos9-ranlib clib.a
    rof2elf sys.l && m68k-elfos9-ranlib sys.a
    m68k-elfos9-gcc -c -mpcrel -ma6rel -mos9call -mbuiltin=os9call main.c
    m68k-elfos9-ld -q -T os9.lds -o prog.elf cstart.o main.o clib.a sys.a
    elf2mod prog.elf prog

GCC code called by, or calling, Microware C code must use Microware's calling convention
(`-mos9call`, or the `os9call` attribute); `-mbuiltin=os9call` makes `memcpy`, `strlen` and the
other C library functions GCC knows use it too, as `clib` defines them. The linker script must
define the symbols Microware's linker provides, such as `end` (Ultra C's startup uses
`_enddata`). References to equates (`sys.l`'s and other libraries') stay external, as `l68`
resolves them: link with those libraries.

## Conversion

| ROF | ELF |
|---|---|
| code | `.text` |
| initialized data | `.data` |
| uninitialized data | `.bss` |
| remote initialized / uninitialized data | `.remote.data` / `.remote.bss` |
| global definitions | global symbols |
| `equ` and `set` definitions | absolute symbols |
| common definitions | common symbols |
| external and local references | RELA relocations |

- **Absolute symbols** also come from kind 7, the name of a macro defined with a colon (`NAME:
  macro`, value 0), which both Microware assemblers export.
- **Relocations:** `R_68K_8`, `R_68K_16`, `R_68K_32`, or `R_68K_PC8`, `R_68K_PC16`, `R_68K_PC32`
  for relative references; the addend is taken from the object's bytes.

- **Data references** are a6-relative; they come out right with a linker script that places
  `.data` at -0x8000, as elf2mod requires.
- **An external reference paired with a negated reference to the same ROF's code,** at the same
  place, is a 32-bit PC-relative value (`cstart.r` calls through `jsr (pc,dN.l)` so): an
  `R_68K_PC32` relocation.
- **A negated reference to the ROF's code alone,** in the code (a constant less a code address,
  `dc.w K-*`), is a PC-relative value against the absolute zero, the field's offset added to the
  addend, as `gas` writes the same.
- **References at one place that add and subtract several externals,** none relative
  (`Dir_+Write_`, `-E$Abort`), refer to a link-time expression's symbol `r68x_<hash>`, defined at
  link time from the note in the section `.r68x` (see "Link-time expressions" in
  [m68k-os9-l68.md](m68k-os9-l68.md)). One added local reference may go with them (a label plus an
  equate, `Block+Field(a6)`): its section is named by an alias, `r68l_<name>_<section>` (the ROF's
  name with characters other than letters, digits and `_` as `_`; `text`, `data`, `bss`, `rdata`,
  `rbss`), a hidden global at that section's start, the label's offset staying the addend.
  `r68togas` writes the same for such a source, and `elf2rof` turns it back into the two
  references. No equate's value is folded in.
- **Other combinations** of references at one place are rejected (a PC-relative one with others,
  for example).
- **A root psect** (one with a module type, like `cstart.r`'s) also gets absolute symbols holding
  its module header's values, `__os9_typelang`, `__os9_attrev`, `__os9_edition` and
  `__os9_stack`, and code symbols `__os9_entry` and `__os9_trapent` (only with a trap entry, not
  0xFFFFFFFF), which elf2mod uses for the module header. A psect of type/language 0 (a library's)
  keeps its attributes, revision, edition and stack in the local symbols `__os9l_attrev`,
  `__os9l_edition` and `__os9l_stack`, for `elf2rof`.

## The ROF format

The ROF format is described in the *OS-9 Assembler/Linker User Manual*, chapter 3, "Relocatable
Object File Format", and in *Using Ultra C/C++*, chapter 6. rof2elf reads:

- edition 9, from Microware C 3.2's assembler;
- edition 9.1 (series 0xF9), from Ultra C's `r68` for the 68000, whose counts of symbols and
  references are 32 bits;

Other editions (9.2, which adds header fields, and 15) are refused.

## libgen's format

`libgen`'s library format is described in *Using Ultra C/C++*, chapter 9, "Library Format Created
by libgen": a header, a hash table of the global definitions, the definitions, a string table, a
table of psects, the references to symbols the library defines and to others, then each psect's
code, initialized data and references. rof2elf reads type 1 (psects of ROF edition 9 or 9.1; type
3 is for edition 15). What the manual leaves out:

- The header is 46 bytes: type 1 has the 4 reserved bytes the manual lists only for type 3.
- A psect entry has a 4-byte field more than listed, after the stack size; it stays zero. Only
  the stack size is kept of a psect's header values; `libgen` refuses root psects, so a library
  never needs a module header.
- Reference entries are 16 bytes, with a 4-byte count; list indices count entries, and -1 ends a
  list.
- A psect's local references are a count (2 or 4 bytes, by its ROF edition) and the references,
  as in a ROF.
- `libgen` lists a psect's references to library symbols before the others; rof2elf sorts them by
  name, as r68 writes them, so the ELF object is the same as from the ROF.

Ultra C 1.x's `libgen` wrote an older layout, ID `$2D00D5BB` (the later one `$2D00D5BC`), which
rof2elf reads too: the header without the 4 reserved bytes (42 bytes), psect entries without the
field after the stack size (62), reference entries with a 2-byte count (14); the rest the same.

## Microware compatibility

Libraries made by `libgen` from ROFs of both editions convert to members byte-identical to the
ROFs' own conversions, and a library converted from the older layout links as the later one's. A
program compiled by Ultra C and linked with GNU `ld` from the converted `acstart.r`, `clib.l`,
`os_lib.l` and `sys.l` runs on a CD-i 605 as Ultra C's own link of it does. See also "Microware
compatibility" in [m68k-os9-l68.md](m68k-os9-l68.md).

Section 6 of
[OS9-COMPAT-DESIGN.md](https://github.com/cdifan/toolchaincdi/blob/main/OS9-COMPAT-DESIGN.md)
describes the design.
