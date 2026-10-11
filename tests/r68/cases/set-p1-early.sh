# A set symbol used before its first set and set in a macro defined before that use: the body's
# set is the first pass's too; and the marker of a use file of equates after byte data is owed
# (as aligns at it after an odd dc.b).
# fixed: d939428 2026-10-11 r68togas: sets in macros defined before the use; use markers after byte data
cat > eq.d <<'X'
X equ 1
X
cat > t.a <<'X'
SETR macro
Rate set 3
 endm
 psect set_p1_early,0,0,0,0,0
 dc.b Rate
 dc.b 7
 use eq.d
Rate set 2
 SETR
 dc.w 0
 ends
X
asm t
has_bytes t.o "0307 0000"
