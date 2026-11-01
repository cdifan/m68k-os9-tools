# m68k-os9-l68: a GCC object's constructors and destructors (.ctors, .dtors, .init_array,
# .fini_array, as __attribute__((constructor)) makes them) are an error: cstart doesn't call
# them, and the module silently lost their tables. The same objects without them link.
# fixed: 2026-11-01 m68k-os9-l68: constructors and destructors are an error
src sr <<'EOF'
 psect sr,$101,$8000,1,0,entry
entry rts
 ends
EOF
asm sr
n=0
for s in .ctors .ctors.65434 .dtors .init_array .fini_array; do
	n=$((n + 1))
	printf '\t.text\n\t.globl f%s\nf%s:\trts\n\t.section %s,"aw"\n\t.long f%s\n' $n $n $s $n > c$n.s
	${B}as c$n.s -o c$n.o > as$n.log 2>&1 || why="$why gas failed on $s: $(head -1 as$n.log);"
	link m$n sr.o c$n.o -n=m$n
	failed_with m$n "constructors and destructors (.ctors, .dtors, .init_array, .fini_array) are not supported"
done
printf '\t.text\n\t.globl f\nf:\trts\n' > plain.s
${B}as plain.s -o plain.o > as.log 2>&1 || why="$why gas failed: $(head -1 as.log);"
link m0 sr.o plain.o -n=m0
linked m0
