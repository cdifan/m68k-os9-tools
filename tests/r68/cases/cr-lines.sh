# Sources with OS-9's own line ends (CR alone) and a DOS end of file (Ctrl-Z after the last
# line): the pre-scan reads their lines as the translation does (it read a CR-only file as one
# line, so a set symbol used before its set wasn't known: dc.w X gave X's first value, not
# its last as r68), and Ctrl-Z ends the source (gas said "junk at end of line" there).
# fixed: 2be3f78 2026-10-11 r68togas: CR line ends in the pre-scan; Ctrl-Z ends a source
printf ' psect cr,0,0,0,0,0\r dc.w X\rX set 5\rX set 7\r ends\r\032' > cr.a
asm cr
has_bytes cr.o "0007 4e71"
