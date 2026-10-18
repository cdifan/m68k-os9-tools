# m68k-os9-l68 -namepad=<hex>: the byte after an odd-length module name (default 00; some native
# OS-9 l68 builds copy the name in words, so the pad is whatever followed it); -irefspad=<hex>:
# bytes after the initialized data's pointer tables (M$IRefs), before the CRC, as some old modules
# have them. Both in hex, two digits to a byte; anything else is an error.
src np <<'EOF'
 psect np,$101,$8000,1,0,entry
 vsect
pp dc.l fa
 ends
entry rts
fa rts
 ends
EOF
asm np
link m0 np.o -n=ab
linked m0
link m1 np.o -n=ab -namepad=6d -irefspad=00000000
linked m1
link m2 np.o -n=abc -namepad=6d
linked m2
# the name at 0x48: "ab", its zero, the pad
has_bytes m0 "61620000 4e75"
has_bytes m1 "6162006d 4e75"
# an even-length name ("abc" and its zero) has no pad
has_bytes m2 "61626300 4e75"
lacks_bytes m2 "6d4e75"
# the tables (code: pp at 00; data: none), then the pad, then the CRC
has_bytes m0 "0000 0001 0000 0000 0000 00"
has_bytes m1 "0000 0001 0000 0000 0000 00000000 00"
n0=$(wc -c < m0); n1=$(wc -c < m1)
[ $((n1 - n0)) = 4 ] || why="$why m1 is $n1 bytes, m0 $n0;"
# M$Size (offset 4) is the file's size
[ "$(od -An -tx1 -j4 -N4 m1 | hex)" = "$(printf %08x "$n1")" ] || why="$why m1's M\$Size isn't its size;"
link e1 np.o -n=ab -namepad=6
failed_with e1 "-namepad=6: expected one byte"
link e2 np.o -n=ab -namepad=6d6d
failed_with e2 "-namepad=6d6d: expected one byte"
link e3 np.o -n=ab -irefspad=000
failed_with e3 "-irefspad=000: expected bytes in hex"
link e4 np.o -n=ab -irefspad=zz
failed_with e4 "-irefspad=zz: expected bytes in hex"
