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

The linker script only has to define them, with any value, so that `ld` accepts the references;
`PROVIDE` defines them only where something refers to them:

```
    .text : { ... }
    PROVIDE (btext = ADDR(.text)); PROVIDE (_btext = ADDR(.text));
    PROVIDE (bname = ADDR(.text)); PROVIDE (_bname = ADDR(.text));
    PROVIDE (etext = ADDR(.text)); PROVIDE (_etext = ADDR(.text));
```

## Symbol modules

With `-g`, `elf2mod` also writes a symbol module `<outfile>.stb`, as `l68 -g` does: an OS-9 data
module named `<module>.stb` that lists the program's global symbols for the user-state debugger and
the source debugger. With a directory `STB` next to `<outfile>`, it goes there, as with `l68`. The
format is described in appendix A of the *OS-9/68000 User-State Debugger* manual:

- the module header, its symbol field pointing to the STB header; the name;
- the STB header: the format (0x0100), the program module's CRC (so the debugger can check that the
  symbols belong to the program), and the offset and number of the symbol entries;
- the symbols the linker defines, with type flag 0x2000: `btext` (0), `bname` (the module name),
  `etext` (the module size), `end` (the end of the data area) and, for modules with the program
  header's layout (programs, trap libraries, `-i` system modules), `_jmptbl`;

The symbol module's own attributes and revision are `$8001` (reentrant, revision 1), whatever the
program module's, as both `l68`s write them.

The two `l68`s write this a little differently, and `--stb=STYLE` follows either:

## The owner

The module's owner (group.user) is `--owner`, else the `GRPUSER` environment variable, as with
`l68`, else 0.0; `l68` makes it 1.0 when `GRPUSER` isn't set.

## Microware compatibility

For the same objects, the module and the symbol module are `l68`'s byte for byte (the symbol
module's header parity in Ultra C's format aside), checked by `test/l68cmp` with both linkers and
through more than 1,100 modules rebuilt from real sources and compared with the originals.
