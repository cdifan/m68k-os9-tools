# as's MRI mode aligns at any directive after an odd dc.b (mri_pending_align), r68 only at an
# instruction or a dc.w: a macro definition after byte data (a name dc.b "Abc",0 then a macro)
# is moved before the source; the line marker after a use file that ends in byte data is
# written later, before the next aligning statement.
# fixed: da4583a 2026-10-11 r68togas: no alignment at macro definitions and use-file markers after byte data
cat > bytes.d <<'X'
B dc.b 1,2,3
X
cat > odd.a <<'X'
 psect odd,0,0,0,0,0
 dc.b 7
M macro
 dc.b \1
 endm
 dc.b 8
 use bytes.d
 dc.b 9
 M 10
 nop
 ends
X
asm odd
has_bytes odd.o "0708 0102 0309 0a00 4e71"
