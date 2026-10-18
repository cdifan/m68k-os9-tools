# gastor68

## Usage

    gastor68 [-mode=c32|ucc] [-m<n>] [-n=<psect>] [-o <output>] <source>

| Option | Meaning |
|---|---|
| `-mode=c32` | Microware C 3.2's style (the default): the psect line and `nam` as C 3.2's `c68` writes them, branches sized `.w` |
| `-mode=ucc` | Ultra C's style: the psect named after the source as it is, branches within the file unsized (see "Branches") |
| `-n=<psect>` | the psect's name; by default the name in the source's `.file` directive (GCC writes the C file's name there), else the input file's name |
| `-o <output>` (or `-o=`) | the output; by default the source's name with a final `.s` replaced by `.a` |

The input file, and any existing file `-o` doesn't name, is never overwritten. `m68k-os9-xcc`
passes `-mode=`, `-m2` for 68020 code and `-n=`, and names the output itself.

What has no r68 form is an error naming the construct, with the input line; the translation goes
on, so one run lists every such line, but no output is kept and the exit status is 1.

## What it accepts

Not accepted: hand-written GNU sources beyond that subset (`.macro`, `.irp`, `.rept` with
expressions, `.weak`, `.uleb128`, section tricks), which have no r68 equivalent or need an
assembler to resolve, and sections r68 has no form for, such as `.ctors` (C constructors). The
asymmetry with r68togas is deliberate: r68togas takes everything real r68 sources use, gastor68
only what GCC writes.

## Translation

### The psect

- **The psect line:** in C 3.2's style `psect name_c,0,0,0,0,0` (the source's name, every
  character other than a letter, digit, `_`, `$` or `@` as `_`) followed by `nam name_c`; in Ultra
  C's style `psect name.c,0,0,0,0,0`, as Ultra C writes it.
- **A mainline:** a GNU source that defines the `__os9_*` symbols (`__os9_typelang`,
  `__os9_attrev`, `__os9_edition`, `__os9_stack`, `__os9_entry`, as hand-written startup code
  does) gets their values in the psect line's fields; compiled C files have zeros.
- **Sections:**

  | GCC | r68 |
  |---|---|
  | `.text`, `.text.startup` (`main` at `-O2`) | the psect (code) |
  | `.rodata`, `.rodata.str1.1` (merged strings) | the psect, with the code (read-only data stays with the code, as with `-mpcrel`) |
  | `.data`, `.data.rel`, `.data.rel.local` | `vsect`, `dc` |
  | `.bss` | `vsect`, `ds` |
  | `.remote.data` | `vsect remote`, `dc` |
  | `.remote.bss` | `vsect remote`, `ds` |

  The vsects are written after all of the code, at the end of the psect, in their order. GCC
  switches to `.data` between a function's strings; written in place, a vsect after an
  odd-length `dc.b` would make `m68k-os9-r68`'s translation align the code. The data's layout is
  the same either way.
- **Function statics:** GCC writes `.comm` only after `.local` (GCC's default `-fno-common`), for
  function statics. They become `vsect` `ds` with a label without a colon (not global), aligned
  as given, at the end of the file, where `gas` puts them (after all of `.bss`). r68's `com` is
  an overlaid common block and isn't used.

### Instructions and operands

- **Registers** lose their `%`. To `gas`, `%fp` is a6 and `%sp` a7; `%fp0`-`%fp7` are 68881
  registers and stay `fp0`-`fp7`.
- **Operands:** GCC's `(8,%a5)`, `(%pc)` and `(4,%a0,%d1.l)` forms become r68's `8(a5)`,
  `x(pc)` and `4(a0,d1.l)`; MIT syntax (switch code: `jmp %pc@(2,%d0:w)`) becomes the same forms,
  with the index register's size kept (`2(pc,d0.w)`, `d0.l*4`), since r68 takes an index without
  a size as `.l`. Bit fields: `d0{#30:#2}` is `d0{30:2}`.
- **`movem` and `fmovem` masks become register lists** (`d2-d7/a2`, `fp0-fp1`). GCC writes
  masks as numbers in the order of the addressing mode; r68 reads a numeric mask in normal
  register order and reverses it itself for `-(An)`, so a number would select other registers.
  A register list has no such trap, and reads as C 3.2's `c68` output does.
- **System calls stay numeric:** `trap #0` and `trap #15` with the call's number as `dc.w
  <number>`; GCC's output has no names for them.

### Branches

GCC writes `jbsr`, `jra` and `j<cc>`, which `gas` sizes itself; r68 has no such mnemonics.

- **C 3.2's style:** `bsr.w`, `bra.w` and `b<cc>.w`, the word forms, as Microware's compilers
  write them; GCC's explicit sizes are kept (`bsr.l` with `-m68020`).

### Symbols and labels

- Names pass through unchanged where r68 allows them (`CSWTCH.11`, `buf.0` are legal); a name r68
  can't take is an error. `.globl` labels get r68's `:` (global); undefined ones need no
  declaration in r68 and are dropped.
- GCC's `.L` and `.LC` local labels are renamed into a collision-free r68 form, and GNU's numeric
  local labels (`1:`, `1f`, `1b`, in inline assembly) into `g<n>.<k>`.
- A label alone on a line in a data section goes onto the next data line (`zero: ds.b 4`): in
  r68 a lone label in a vsect takes the initialized data's counter, not that of the `ds` after it.

### Data

- `.byte`, `.word` and `.long` become `dc.b`, `dc.w` and `dc.l`.
- `.ascii` and `.string` become `dc.b` with r68's string syntax, their escapes written as numeric
  bytes: r68 has no C escapes (a backslash takes the next character literally).
- `.align` and `.balign` become `align`; `.balignw` with a filler word becomes `align` where the
  filler doesn't matter, otherwise the filler written out.
- `.zero` and `.skip` become `ds.b` in `.bss` and `.remote.bss` only; elsewhere (GCC pads structs
  in `.data` with `.zero`) `dz.b` or `dc.b 0`, since `ds` there would move the data to the
  uninitialized counter, and `ds` is illegal in a psect.
- Switch tables (at `-O0`): `.swbeg` (a hint for `gas`) is dropped; `.word .L11-.L4` becomes
  `dc.w` with the renamed labels.

### Dropped

`.type`, `.size`, `.ident`, `.file` (read for the psect's name), `.loc` and the other debug and
CFI directives, `.swbeg`, and the DWARF sections. Inline assembly arrives between `#APP` and
`#NO_APP`, with `| <line> "<file>"` comments; it is translated like the rest, and
`.pushsection`/`.popsection` keep a section stack.

## r68togas's output

Input that r68togas made (it starts with the prelude's line marker, `# 1 "r68.prelude"`) is
recognised without an option and given back as the r68 source it came from:

- `r68togas | gastor68 | r68 == r68`: the source given back, assembled by Microware's `r68`,
  gives the same object as the source itself (the ROF but for its date);
- `gastor68 | r68togas | as == as`: for GCC's output, gastor68's r68 translated back by r68togas
  and assembled by `gas` gives GCC's own object;
- the text is the source's but for blanks and parentheses (r68's precedence may need others).

How:

- The prelude, the epilogue and the lines r68togas adds are dropped: line markers, its labels and
  `set` symbols (`.Lr68...`, `r68__...`, `NAME__p1`), `reloc` and `xdef` lines, and the blocks it
  writes before or around a statement.
- `* r68togas: the source` marks where the source starts; a macro definition r68togas moved
  before it goes back to its place.
- An inlined `use` file (between `* r68togas: use file PATH, sum CRC` and `* r68togas: end of
  use file PATH`) is its `use` statement again. The sum is the CRC-32 of the file's text,
  insensitive to layout (LF line ends, runs of blanks as one space, none at a line's end, blank
  lines left out); gastor68 warns where the file as PATH names it has another text now, and checks
  nothing where it can't read it.
- A statement noted `;[r68: ...]` (see "Annotations" in [r68togas.md](r68togas.md)) is the note,
  or the operation alone where only it is noted; a block noted on its last line is one statement.
- The prelude's macros are their statements again (`r68dc`, `r68ds`, `r68dz`, `r68fill`, `r68lab`,
  `r68align`, `r68do`, `r68lo`, `r68glab`, `r68xlab`, `r68gdef`, `r68chip`, `r68L`, `r68error`, and
  the ones for statements `gas` must not see), in the operation's case (`R68DC` for `DC.B`); a label
  passed to one is global where the epilogue exports it.
- Names lose r68togas's renaming (`_at_`, `__dot__`, `__r68`, `__r68m<n>`, `__r68pc`,
  `__final`); `r68__ofs` is `.` again, `r68__n\@` `\#`, `r68__L<n>` `\L<n>`.
- The comment after a statement loses the `;` r68togas puts before it.

## Microware compatibility

The output assembles with Ultra C's `r68` and with `m68k-os9-r68` to the same code and data as
`gas` makes of GCC's source, but for the differences below. This is checked against Microware's
own assemblers on GCC's output for a large set of C sources, our own and old CD-i programs (more
than 73k lines in more than 310 files), and, through the round trip with r68togas, on more than
900 real r68 sources. The translation follows what
Microware's assemblers do where GCC's syntax would mean something else to them:

- r68 sizes an unsized branch as a word (with a warning) unless `-b`; hence `.w` in C 3.2's style.
- r68 reads a numeric `movem` mask in register order and reverses it for `-(An)`; hence register
  lists.
- r68 takes an index register without a size as `.l`; hence the size kept.
- r68 makes `(d,An,Xn)` 32 bits without `.w`; hence `(d.w,An,Xn)` where 16 bits fit.
- A lone label in a vsect takes the initialized data's counter; hence the label on the data line.
- r68 has no C escapes in strings; hence numeric bytes.

Long symbol names (up to 300 characters, distinct in their last one) and 64-bit immediates in
68881 code (`fmul.d #0x4008000000000000,fp0`) assemble with Ultra C's `r68`; C 3.2's `r68` hasn't
been checked for either.

## Differences and limitations

- **Not translated:** sections other than those above (`.ctors`), and GNU constructs outside
  GCC's subset (see "What it accepts"). C 3.2's `ttl` lines (one per function in `c68`'s output)
  aren't written.
- **Merged strings:** GCC puts its strings in a mergeable section (`.rodata.str1.1`), so when
  `gas` assembles GCC's output, `ld` can keep one copy of a string that several files share.
  gastor68's output has them as `dc.b` lines in the code, like any other read-only data, so each
  file keeps its own copy: the code is a few bytes longer where strings repeat across files.
- **r68 fills unused register fields** where `gas` writes zeros: `ftst`'s destination field and
  the unused Dh field of 32-bit `muls.l`, `mulu.l` and `divs.l`. The instructions are the same.
- **A 16-bit relocation's addend** beyond 16 bits (a6-relative data beyond 32 KB): the ROF holds
  the offset in the instruction's 16 bits, so it is the same modulo 65536. In OS-9's a6-relative
  data, biased by 32 KB, it fits when the vsect does.
- **A symbol's displacement plus more than 16 bits on the 68020** (`filler+39999(%a6)`): `gas`
  makes it 32 bits, r68 16 bits, the offset modulo 65536 (as above).
- **Undefined symbols nothing refers to** (GCC's `.globl __udivsi3`): `gas` keeps them in the
  object, r68 doesn't.

## Tests

      TOOLS=<dir> GASTOR68=<gastor68> [R68=<r68.exe> R68WORK=/mnt/c/...] bash tests/gastor68/run.sh

- **`make check-gastor68`** in `test/abi-exec`: each test's C side compiled with `-S`, translated by
  gastor68, translated back by r68togas, assembled by `gas`, linked and run, at `-O0` and `-O2`.
