# m68k-os9-l68: global _sysedit and _sysattr (a C source's @_sysedit: equ 7) give the module's
# edition and attr/rev, over the root psect's and over -e= and -R=, as both Microware linkers
# (measured 2026-10-15); before, the psect's.
# fixed: FIXME
src sr <<'EOF'
 psect sr,$101,$8000,1,0,entry
entry rts
 ends
EOF
src sv <<'EOF'
 psect sv,0,0,0,0,0
_sysedit: equ 7
_sysattr: equ $A005
 ends
EOF
asm sr; asm sv
link m1 sr.o sv.o -n=m1
linked m1
has_bytes m1 "05550101 a005 0007"
link m2 sr.o sv.o -e=5 -R=7 -n=m2
linked m2
has_bytes m2 "05550101 a005 0007"
link m3 sv.o sr.o -n=m3
linked m3
has_bytes m3 "05550101 a005 0007"
link m4 sr.o -n=m4
linked m4
has_bytes m4 "05550101 8000 0001"
