| .local + .comm (a function's static) go after all of .bss, where GNU as puts them (its
| subsection 1), so that the layout is GCC's: here s at 8, after b, though declared first.
| fixed: a8f2037 2026-10-13 gastor68: MIT index sizes, 16-bit index displacements, .comm layout
| expect: s ds.b 4
	.text
	.globl	f
f:
	lea s(%a6),%a0
	lea b(%a6),%a1
	rts
	.local	s
	.comm	s,4,2
	.globl	b
	.section	.bss
	.align	2
b:
	.zero	8
