# elf2mod

`elf2mod` converts a linked m68k ELF program into an OS-9/68000 module: it lays out the module
header, the code, the initialized data and its pointer tables, as Microware's linker `l68` writes
them, and can also write the debugger's symbol module and the source debugger's `.dbg` file. It is
Murachue's [elf2mod](https://github.com/murachue/elf2mod), heavily extended. The front ends
[m68k-os9-l68](m68k-os9-l68.md), [m68k-os9-xcc](m68k-os9-xcc.md) and
[m68k-os9-gcc](m68k-os9-gcc.md) run it after GNU `ld`; it can also be run on its own on a
suitably linked ELF file (see "The ELF input").

## Usage

    elf2mod [options] <elffile> <outfile>

| Option | Meaning |
|---|---|
| `-n`, `--name=NAME` | the module name; default the ELF file's base name, without extension, in lower case |
| `-s`, `--stack=SIZE` | the stack size; default `__os9_stack`, else `0xC00` |
| `-r`, `--revs=N` | the revision; default from `__os9_attrev`, else 1 |
| `-e`, `--edit=N` | the edition; default `__os9_edition`, else 0 |
| `-g`, `--stb[=STYLE]` | also write the symbol module `<outfile>.stb`, in C 3.2's `l68`'s format (`c32`, the default) or Ultra C's (`ucc`); see "Symbol modules" |
| `-u`, `--owner=G.U` | the owner; default `GRPUSER`, else 0.0 |
| `-p`, `--perms=HEX` | the header's permissions; default 555 |
| `-S`, `--sticky` | the sticky bit (`M$Attr` bit 6), on top of `__os9_attrev` |
| `-N`, `--name-last[=NAME]` | the module name after the code, not after the header |
| `-x`, `--code-align=N` | the code's start in the module aligned to N (2, 4, 8, 16) |
| `--typelang=HEX` | type and language; default `__os9_typelang`, else `0101` |
| `--header=SIZE` | `0x48` (the program header, the default), `0x3C` (`M$Exec`, `M$Excpt`, `M$Mem`; name last) or `0x30` (no fields; name last) |
| `--raw` | the module's body only: no header, name or CRC (see "Raw output") |
| `--entry=none` | no entry point, whatever `__os9_entry` says |
| `--ld-map=FILE` | GNU `ld`'s map of the link, for the order of the pointer tables and the jump table |
| `--name-pad-byte=HH` | the byte after an odd-length module name (default 00) |
| `--irefs-pad=HEX` | bytes after the pointer tables, before the CRC |
| `--jmptbl-spare=N` | N unused jump table entries (`jmp 0`) after the used ones (the table must have room) |

A stack, entry point, data area, initialized data or trap entry that is non-zero but has no field
in the chosen header is an error.

## The ELF input

- **Sections:** `.text`, `.data` and `.bss`, and optionally `.remote.data` and `.remote.bss`;
  other sections are ignored, so the linker script must put `.text.*`, `.rodata` and the like into
  these. `.text` may be at any address.
- **The data area** starts at address -0x8000, where a6 points 0x8000 into it, with `.bss` or
  `.data`, whichever comes first; the -0x8000 bias is the linker script's, not `elf2mod`'s.
  `.bss` must be next to `.data` and have no contents.
- **Relocations** must be applied but kept: GNU `ld`'s `-q`.
- **In `.text`:** `R_68K_16` relocations to the data (a6-relative), `R_68K_32` and `R_68K_8` to data
  or absolute symbols (link-time constants: a6-relative offsets, equates), and PC-relative ones.
  Values must fit their fields (a6-relative `R_68K_16` from -0x8000 to 0x7FFF, `R_68K_8`,
  `R_68K_PC8`): GNU `ld` doesn't check all of these, and `elf2mod` reports them. An absolute
  reference from the code to a code symbol (`dc.w Init` in a driver's entry table) holds the
  symbol's offset in the module, as Microware's linkers write it, whatever address `.text` has (not
  with `--raw`, where the code runs at its address); a 16-bit one doesn't fit at link time, so link
  with `--noinhibit-exec`.
- **In `.data`:** only `R_68K_32`. To code or data: relocated when OS-9 loads the module (listed in
  the pointer tables). To absolute symbols (equates) or undefined weak symbols (0): kept as they
  are. Undefined symbols are errors.

### More than 32 KB of data, and remote data

a6-relative addressing reaches 64 KB of data, from -0x8000 to 0x7FFF; with more than 32 KB the data
crosses from 0xFFFFFFFF to 0, which GNU `ld` accepts with `--no-check-sections`. Remote data
(GCC's `remote` attribute, or Microware's remote vsects) goes in `.remote.data` and `.remote.bss`,
after the other data, beyond what a6 reaches; code addresses it as a6 plus a 32-bit offset. The data
area then extends past 0, so `.text` goes above it (at 0x400000, say). The module's initialized
data is one block, `.data` and `.remote.data`; with `.bss` before `.data` the two are adjacent and
no zeros are stored for `.bss`. For example:

```
SECTIONS {
    . = 0x400000;
    /* .rodata in the same statement: each object's constants follow its code,
       within PC-relative reach also in programs over 32K */
    .text : { *(.text .text.* .rodata .rodata.*) . = ALIGN(2); }
    . = -0x8000;
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) . = ALIGN(2); }
    .data : { *(.data .data.*) . = ALIGN(2); }
    .remote.data : { *(.remote.data .remote.data.*) . = ALIGN(2); }
    .remote.bss (NOLOAD) : { *(.remote.bss .remote.bss.*) . = ALIGN(2); }
    end = .;
}
```

## The module header

A root psect converted by [rof2elf](rof2elf.md) (such as Microware's `cstart.r`) defines `__os9_*`
symbols with its module header's values, which `elf2mod` uses: type and language
(`__os9_typelang`), attributes and revision (`__os9_attrev`), edition (`__os9_edition`), stack
size (`__os9_stack`), entry point (`__os9_entry`) and uninitialized trap entry (`__os9_trapent`).
The options override them. Without them: a program module, object code, reentrant, revision 1,
edition 0, stack 0xC00.

A global `_sysedit` or `_sysattr` (from any object: a C file's `@_sysedit: equ 7`) gives the
edition, or the attributes and revision, over all of these and over `--edit` and `--revs`, as both
Microware linkers take them.

## The pointer tables

The initialized data's pointer tables (`M$IRefs`: the pointers to the code, then those to the data)
list the pointers in address order, or, with `--ld-map`, as both Microware linkers do:

- first the jump table's entries, in reverse order;
- then object by object, in reverse link order. Within an object:
  its local pointers vsect by vsect in the source's order, each vsect's in address order (a remote
  vsect's pointers before those of a plain vsect after it); then its pointers to other objects'
  global symbols, by symbol name in descending order, and for one name by address, descending.

The objects and their link order come from the map's `LOAD` lines (an archive member's place is its
archive's), then from where the map first names them; a pointer's object is the one whose `.data` or
`.remote.data` input section holds it. The vsects' order comes from the reference order records
(section `.r68ord`) that `r68togas` and `rof2elf` write; an object without them (GCC's) is in
address order. `m68k-os9-l68` passes the map.

## Far calls: the jump table

Calls and address loads are PC-relative with 16-bit displacements, so they reach ±32 KB. For larger
programs, `elf2mod` builds a jump table, as Microware's linker does: each target that's too far
gets an entry `jmp target` in `_jmptbl`, in the data area (relocated when OS-9 loads the module),
and each far reference is redirected through it, keeping its size:

| Instruction | Becomes |
|---|---|
| `bsr.w f` | `jsr _jmptbl+x(a6)` |
| `bra.w f` | `jmp _jmptbl+x(a6)` |
| `lea f(pc),An` | `movea.l _jmptbl+x+2(a6),An` (the real address of `f`) |
| `pea f(pc)` | `move.l _jmptbl+x+2(a6),-(sp)` |

- Link with `--noinhibit-exec` as well as `-q`: `ld` then reports "relocation truncated to fit" for
  the far references but still writes the output, which `elf2mod` completes.
- The linker script reserves the table at the end of `.data`, from `_jmptbl` to `_ejmptbl`, 6 bytes
  per entry; `elf2mod` reports how many entries it needs if there aren't enough. With the size on
  the command line (`--defsym __jmptbl_size=600`):

  ```
      .data : {
          *(.data .data.*)
          . = ALIGN(2);
          _jmptbl = .;
          . += DEFINED(__jmptbl_size) ? __jmptbl_size : 0;
          _ejmptbl = .;
      }
  ```

- Conditional branches (`Bcc`, `DBcc`) and other far PC-relative references can't be redirected,
  and are errors.
- `move.l` changes the condition codes where `pea` doesn't, which only matters for hand-written
  assembly that relies on them.

## The linker's module symbols

Microware's linker defines symbols for the module's start, name and end, which Microware's
libraries use (C 3.2's `_errmsg` finds the module's name through `btext`, and `cio.l` refers to
it). Their values depend on the module's layout, which only `elf2mod` knows, so it puts them into
every reference to them:

| Symbol | Value |
|---|---|
| `btext`, `_btext` | the module's start (offset 0) |
| `bname`, `_bname` | the module's name |
| `etext`, `_etext` | the module's end (its size; plus 4 when exactly one of the two pointer tables has entries, as both Microware linkers write it) |

The linker script only has to define them, with any value, so that `ld` accepts the references;
`PROVIDE` defines them only where something refers to them:

```
    .text : { ... }
    PROVIDE (btext = ADDR(.text)); PROVIDE (_btext = ADDR(.text));
    PROVIDE (bname = ADDR(.text)); PROVIDE (_bname = ADDR(.text));
    PROVIDE (etext = ADDR(.text)); PROVIDE (_etext = ADDR(.text));
```

A pointer to one of them in the initialized data is relocated when OS-9 loads the module, as any
pointer into the code; an absolute reference in the code (`dc.w bname` in a descriptor) gets the
offset in the module; a PC-relative reference gets the right displacement, and a far one goes
through the jump table. `etext` is known only at the end, so a reference to it is taken as near
only if even the largest the module can become is in reach, and a far one must be to `etext`
itself, without an offset. A link-time expression with one of these symbols in it (`etext+Size`,
from a ROF) is corrected by each such term's coefficient times the difference between the symbol's
value and the linker script's placeholder. The other symbols Microware's linker defines are
ordinary values the linker script gives: `end` (Ultra C: `_enddata`), the end of the data area, and
`_jmptbl`.

## Raw output

`--raw` (`l68 -r`) writes the module's body without header, name and CRC: the code, the initialized
data block and the pointer tables, the code running at its link address. It has more of the
linker's symbols, as Ultra C's `l68 -r` defines them: `_bidata`, the initialized data block's start
(after the code, where `etext` is); `edata`/`_birefs`, the pointer tables' start (after that
block); and `dsize`/`_dsize`, the data size rounded up to `$100`, which the linker script gives. In
raw output an absolute reference to `btext`, `etext`, `_bidata` or `edata` is the address (the
link's base plus the offset); PC-relative references and pointers in the initialized data stay
offsets in the image. The image ends with four zero bytes more when exactly one of the two pointer
tables has entries, as `l68 -r` writes it.

## Symbol modules

With `-g`, `elf2mod` also writes a symbol module `<outfile>.stb`, as `l68 -g` does: an OS-9 data
module named `<module>.stb` that lists the program's global symbols for the user-state debugger and
the source debugger. With a directory `STB` next to `<outfile>`, it goes there, as with `l68`. The
format is described in appendix A of the *OS-9/68000 User-State Debugger* manual:

- the module header, its symbol field pointing to the STB header; the name;
- the STB header: the format (0x0100), the program module's CRC (so the debugger can check that the
  symbols belong to the program), and the offset and number of the symbol entries;
- the symbol entries, by value: a 4-byte value, a 2-byte type (0 uninitialized data, 1 initialized
  data, 2 uninitialized remote data, 3 initialized remote data, 4 code, 6 absolute) and the 4-byte
  offset of the name. Code symbols are offsets in the module, data symbols a6-relative; a
  (non-remote) data symbol's value from `$8000` to `$FFFF` (a module's unbiased data area beyond
  32 KB) is sign-extended from 16 bits (`$FFFF8004`), larger ones are kept, as `l68` writes them.
  Static symbols, `__os9_*`, `_ejmptbl`, link-time expressions and section aliases (`r68x_*`,
  `r68l_*`, see [r68togas.md](r68togas.md)) and equates aren't listed: neither `l68` lists equates,
  though the format has a type for them;
- the symbols the linker defines, with type flag 0x2000: `btext` (0), `bname` (the module name),
  `etext` (the module size), `end` (the end of the data area) and, for modules with the program
  header's layout (programs, trap libraries, `-i` system modules), `_jmptbl`;

The symbol module's own attributes and revision are `$8001` (reentrant, revision 1), whatever the
program module's, as both `l68`s write them.

The two `l68`s write this a little differently, and `--stb=STYLE` follows either:

- **`c32`** (the default): Microware C 3.2's `l68`. The type flag 0x2000 also marks the symbols
  other psects refer to: `elf2mod` sets it on the symbols relocations refer to, also those named in
  a link-time expression a relocation uses. Symbols with the same value are listed in reverse order
  of definition: `l68` defines the root psect's symbols, then its own (`btext`, `bname`, `etext`,
  `end`, `_jmptbl`), then the other psects'; so the other psects' come first (in reverse link
  order), then its own, then the root psect's (within a psect, by name descending). Each symbol's
  object comes from `--ld-map`; without it, all count as the root psect's.

## The source debugger's file

Where psects have debug information (C 3.2's `c68 -g` writes `debug.b/.w/.l` lines, which r68togas
and rof2elf put into a section `.r68dbg`, put together in link order), `-g` also writes
`<outfile>.dbg` next to the symbol module, as `l68 -g` does:

- `c32`: `$BEEFCAFE`, a zero word, the program module's CRC as a long, the psects' debug areas;
- `ucc`: `$CAFEBEEF`, the word 3, the CRC, a zero long, the areas.

## Pad bytes

For byte-identical rebuilds of old modules: `--name-pad-byte=HH` gives the byte that pads an
odd-length module name to a word (default 0, as both Microware linkers on DOS and Windows write it;
native OS-9 builds of `l68` copy the name in words from the command line, so their pad is whatever
followed the name there); `--irefs-pad=HEX` writes bytes after the pointer tables, before the CRC,
as some old modules have them; `--jmptbl-spare=N` writes N unused jump table entries (`jmp 0`, the
module's start) after the used ones, listed among the pointers to code, as some of Ultra C's `l68
-a` links have them.

## The owner

The module's owner (group.user) is `--owner`, else the `GRPUSER` environment variable, as with
`l68`, else 0.0; `l68` makes it 1.0 when `GRPUSER` isn't set.

## Microware compatibility

For the same objects, the module and the symbol module are `l68`'s byte for byte (the symbol
module's header parity in Ultra C's format aside), checked by `test/l68cmp` with both linkers and
through more than 1,100 modules rebuilt from real sources and compared with the originals.
