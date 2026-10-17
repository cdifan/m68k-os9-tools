# debug.b/.w/.l (C 3.2's c68 -g writes them): the operands in the ROF's debug area, after the
# remote data, its size in the header (offset 52); words and longs at even offsets in it (a zero
# byte after the odd string), its end not padded; * the code's location. References located in
# it have both location bits ($220). The references in the order both Microware assemblers write
# them: the external ones in the order made, the local ones those made in the psect (code, debug
# lines there) then those made in vsects (data, debug lines there), each from the last made; here
# the remote vsect is first, so its references come last. All as C 3.2's R68 writes this source
# (measured 2026-10-17, the whole ROF the same but for the date). Before, r68 refused the lines.
# fixed: 55093e0 2026-10-17 debug.b/.w/.l, the ROF debug area and l68 -g's .dbg
src dbg <<'EOF'
 psect dbg_p,$101,$8000,1,0,c1
 vsect remote
rv dc.l c1
 debug.l rv
 ends
 vsect
dv dc.l c2
 debug.l dv
 ends
 debug.w 0,2
 debug.l *
 debug.b "ab",0
 debug.w 7
c1 move.l dv(a6),d0
 debug.l c2+4
 debug.w c2
 debug.l xcode
 debug.l xdata
c2 bsr xcode
 move.l xdata(a6),d1
 rts
 debug.l *
 ends
EOF
m68k-os9-r68 dbg.a -q -dialect=c32 -obj=rof -o=dbg.r > dbg.log 2>&1 ||
	why="$why assembling failed: $(head -1 dbg.log);"
# the debug size, the name
has_bytes dbg.r "000000286462675f7000"
# the debug area, after the remote data (rv: c1, 0)
has_bytes dbg.r "00000000 00000000 00000000 00000002 00000000 61620000 00070000 00080004 00000000
	00000000 0000000e"
# the externals' references
has_bytes dbg.r "78636f646500 0002 0238 0000001c 00b0 00000006"
has_bytes dbg.r "786461746100 0002 0238 00000020 0030 0000000a"
# the local references
has_bytes dbg.r "0009 023c 00000024 0234 0000001a 023c 00000016 0031 00000002 023c 0000000c
	0239 00000004 001c 00000000 023b 00000000 021c 00000000"
# an external's references from the code, vsects and debug lines: in the order made (as C 3.2's
# R68 writes this source, measured 2026-10-17)
src dbgo <<'EOF'
 psect dbgo_p,0,0,0,0,0
 bsr xf
 vsect
vd dc.l xf
 debug.l xf
 ends
 debug.l xf
 bsr xf
 vsect
ve dc.l xf
 ends
 debug.l xf
 rts
 ends
EOF
m68k-os9-r68 dbgo.a -q -dialect=c32 -obj=rof -o=dbgo.r > dbgo.log 2>&1 ||
	why="$why assembling the external's failed: $(head -1 dbgo.log);"
has_bytes dbgo.r "786600 0007 00b0 00000002 0018 00000000 0238 00000000 0238 00000004 00b0 00000006
	0018 00000004 0238 00000008"
# a macro's and a rept's debug lines (no order records there)
src dbgm <<'EOF'
 psect dbgm_p,0,0,0,0,0
dbgm macro
 debug.l *
 endm
 dbgm
 nop
 dbgm
 rept 2
 debug.w 1
 endr
 rts
 ends
EOF
m68k-os9-r68 dbgm.a -q -dialect=c32 -obj=rof -o=dbgm.r > dbgm.log 2>&1 ||
	why="$why assembling the macro's failed: $(head -1 dbgm.log);"
has_bytes dbgm.r "0000000c 6462676d5f7000"
has_bytes dbgm.r "4e714e75 00000000 00000002 0001 0001"
