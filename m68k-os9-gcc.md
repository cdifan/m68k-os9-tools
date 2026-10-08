# m68k-os9-gcc

`m68k-os9-gcc` compiles and links an OS-9/68000 program into a module in one step, with GCC's own
options: it runs `m68k-elfos9-gcc` with newlib's OS-9 runtime (`-specs=os9.specs`, from the
`newlib-cygwin` fork's `libgloss/m68k`) and makes the linked ELF program into a module with
[elf2mod](elf2mod.md). For Microware's compiler options and libraries, use
[m68k-os9-xcc](m68k-os9-xcc.md) (Microware C 3.2's) or
[m68k-os9-ucc](m68k-os9-xcc.md#ultra-c-mode) (Ultra C's) instead.

## Usage

    m68k-os9-gcc -Os -o hello hello.c          # hello.elf, then the module hello
    m68k-os9-gcc -Os -o hello hello.c -Wm,-g   # also the symbol module hello.stb

- **GCC's options** go to `m68k-elfos9-gcc` as they are, with `-specs=os9.specs` added. With
  `-c`, `-S`, `-E`, `-M` (and the others after which GCC doesn't link) it only runs GCC.
- **`-Wm,<option>,...`** passes options to `elf2mod`, comma-separated: `-Wm,-g` for a symbol
  module, `-Wm,-e,<edition>`, `-Wm,-s,<stack>`, `-Wm,-n,<name>` and so on (see
  [elf2mod.md](elf2mod.md)).
- **The output:** `<prog>.elf`, the linked program (kept for debugging), and the module `<prog>`
  (`a.out` by default), named after `<prog>`, a final lower-case `.mod` dropped (see "Module names"
  in [m68k-os9-xcc.md](m68k-os9-xcc.md)).

The OS-9 options of GCC (`-mos9call`, `-mos9newline`, `-mos9stkchk`, `-mos9math`, ...) are
described in GCC's manual, under the m68k options.

## Large programs

A program with more than 32 KB of code can't reach every function with 16-bit calls: the first
link fails with "relocation truncated" errors. `m68k-os9-gcc` then links again with as large a jump
table as `elf2mod` asks for, which it fills, as `m68k-os9-xcc` and `m68k-os9-l68` do (see "Far
calls" in [elf2mod.md](elf2mod.md)). Within one source file of more than 32 KB of code, GCC's
assembler resolves the calls itself and fails; such a file has to be split.

## The runtime

On OS-9, `malloc` has 16 KB plus what the shell's `#` modifier adds (`hello #64k`); the module's
other defaults are in `os9.ld` in newlib's `libgloss/m68k`.

## Tests

`test/abi-exec`'s newlib targets build their programs the same way (`-specs=os9.specs`, `elf2mod`)
and run them on a real CD-i 605 and on CD-i Emulator's (`make check-os9newlib`,
`check-os9math`).
