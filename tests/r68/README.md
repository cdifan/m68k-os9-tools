# r68 tests

Regression tests for the translation of r68 sources (`r68togas`, `m68k-os9-r68`), the conversion
of ROFs (`rof2elf`, `elf2rof`), the linking (`m68k-os9-l68`, `elf2mod`), the librarian
(`m68k-os9-libgen`) and the compiler driver (`m68k-os9-xcc`). Each case is a small source written
for the purpose, with the expected values, ours or derived from the Microware tools' output on the
same source; no Microware file is needed to run them.

```sh
TOOLS=<dir with the tools> bash tests/r68/run.sh [CASE...]
```

| Setting | Meaning |
|---|---|
| `TOOLS` | a directory put first in `PATH`, holding `m68k-os9-r68`, `r68togas`, `rof2elf`, `elf2rof`, `m68k-os9-l68` and `elf2mod`; without it they come from `PATH` |
| `TPREFIX` | the binutils' prefix (`m68k-elfos9-`); the binutils come from `PATH` |
| `R68` | Ultra C's `r68.exe` (WSL): each assembler case without `-dialect=c32` or an expected error is also assembled by it and its code and data compared with ours; the cases that need it (`needs_r68`) run only then, others are skipped |
| `R68WORK` | a directory on a Windows drive for `r68.exe`'s files (`/mnt/c/...`); default the temporary directory, which `r68.exe` can't use under WSL |

Everything is written in temporary directories. The result is one line per case, `PASS`, `FAIL`
(with the reasons), `KNOWN` (a known failure) or `SKIP`, then the counts; the exit status is 0
when no case failed.

## Cases

`cases/*.a` are assembled by `m68k-os9-r68` and checked by their header lines:

| Line | Check |
|---|---|
| `* options: OPTS` | `m68k-os9-r68`'s options |
| `* expect text: HEX` | the code section's bytes, all of them (spaces ignored) |
| `* expect data: HEX` | the initialized data's bytes |
| `* expect size: SECTION HEX` | a section's size |
| `* expect align: SECTION N` | a section's alignment, `2**N` |
| `* expect sym: TYPE NAME VALUE` | a global symbol, as `nm -g` lists it |
| `* expect nosym: NAME` | no symbol of that name |
| `* expect reloc: TEXT` | a relocation, a line of `objdump -r` containing TEXT |
| `* expect error: TEXT` | assembling fails, with TEXT among the messages |
| `* not r68: WHY` | not compared with `r68.exe`'s output (one `r68` doesn't make properly) |

A case without an `expect` line fails, since nothing would be checked. `cases/*.sh` are shell
scripts run in a directory of their own with the helpers that `run.sh` describes (`src`, `asm`,
`lib`, `link`, `has_bytes`, `linked`, `failed_with`, `has_sym`, `has_reloc`, `needs_r68`,
`mwasm`); a failed check adds to `$why`.

A case that tests a fix names the commit that made it, `* fixed: <id> <date> <title>` (`#` in the
scripts), one line per commit where it took more than one; it fails on the tools before that
commit. A case marked `* known: <why>` is an open problem, expected to fail: it is reported as
`KNOWN`, and as a failure once it passes, so that the mark is removed with the fix.
