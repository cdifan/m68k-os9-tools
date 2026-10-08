# elf2rof

`elf2rof` converts m68k ELF relocatables (`.o`) and `ar` archives (`.a`) into Microware
OS-9/68000 ROF objects (`.r`) and libraries (`.l`), so that code from the
[m68k-elfos9 GCC](https://github.com/cdifan/toolchaincdi) can be linked with Microware's linker
`l68`. It is the reverse of [rof2elf](rof2elf.md); the front ends run it for `-obj=rof`.

## Usage

    elf2rof [-k] [-s <series>] [-n <name>] [-o <output>] <input>

- An `ar` archive becomes a library of concatenated ROFs (`libgcc.a` → `libgcc.l`), one psect per
  member, named the same way from each member.
- `-k` skips archive members that can't be converted, with a warning, instead of stopping.

For example, GCC code linked with Microware's `cstart.r` and C library (from your own
installation; Microware's files are not included here):

    m68k-elfos9-gcc -c -mpcrel -ma6rel -mos9call -mbuiltin=os9call main.c
    elf2rof main.o
    elf2rof -o libgcc.l $(m68k-elfos9-gcc -print-libgcc-file-name)
    l68 -o=prog cstart.r main.r -l=clib.l -l=libgcc.l -l=sys.l

As with rof2elf, GCC code called by, or calling, Microware C code must use Microware's calling
convention (`-mos9call`).

## Conversion

Each ELF object becomes one psect, in ROF edition 9, which the linkers of Microware C 3.2 and
Ultra C both read.

| ELF | ROF |
|---|---|
| read-only sections (`.text`, `.rodata`) | code |
| writable sections (`.data`) | initialized data |
| `.bss` | uninitialized data |
| `.remote.data` / `.remote.bss` | remote initialized / uninitialized data |
| global symbols | definitions |
| absolute symbols | `equ` definitions |
| common symbols | common definitions |
| relocations to undefined symbols | external references |
| relocations to the object's code and data | local references |
| PC-relative relocations within the code | resolved, no reference |
| relocations to absolute symbols | resolved, no reference |
| `__os9_*` symbols | the module header |
| DWARF, other sections | dropped |

- **Common definitions** have type 0x0100 and the size as their value; they are referred to by
  name, and `l68` merges them.

- **Relocations:** `R_68K_32`, `R_68K_16`, `R_68K_8` and their PC-relative forms, and the PLT
  forms, which are PC-relative in a static link (as GNU `ld` treats them). The addend goes into the
  object's bytes, as ROF has no addend field.
- **Not supported:** GOT relocations, weak undefined symbols, section groups (COMDAT), and
  constructors and destructors (`.init_array`, `.ctors`, ...), for which `l68` has no mechanism.

## Large programs

`l68 -a` redirects far `bsr.w` and `lea` through its jump table, but not `bra.w`, which GCC uses for
tail calls with `-mbsrw`. For programs with more than 32 KB of code, compile without `-mbsrw` (tail
calls are then `lea` and `jmp`) or with `-fno-optimize-sibling-calls`. elf2mod's jump table does
handle `bra.w`.

## Tests

In [toolchaincdi](https://github.com/cdifan/toolchaincdi):

- `test/l68cmp/run.sh`: Microware C 3.2's `cstart.r`, C library and a test program go from ROF
  through rof2elf and elf2rof back to ROF; `l68` links the same module from them as from the
  originals, byte for byte.
- `make check-l68` in `test/abi-exec`: the calling convention tests, compiled by GCC, converted by
  elf2rof (with libgcc), linked by `l68` and run as OS-9 modules in the test harness's emulator,
  including far calls through `l68`'s jump table; and GCC code linked by `l68` with Microware's own
  C library.

The ROF format is described in the *OS-9 Assembler/Linker User Manual*, chapter 3, and in *Using
Ultra C/C++*, chapter 6. Section 6 of
[OS9-COMPAT-DESIGN.md](https://github.com/cdifan/toolchaincdi/blob/main/OS9-COMPAT-DESIGN.md)
describes the design.
