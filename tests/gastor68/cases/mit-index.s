| An MIT index register's size (switch code at -O0): %d0:w stays d0.w, since r68 takes an
| index without a size as .l (gastor68 wrote d0 alone).
| fixed: a8f2037 2026-10-13 gastor68: MIT index sizes, 16-bit index displacements, .comm layout
| expect: jmp 2(pc,d0.w)
| expect: move.w 4(a0,d1.l),d2
	.text
	.globl	f
f:
	jmp %pc@(2,%d0:w)
	.word 4
	.word 8
	move.w %a0@(4,%d1:l),%d2
	rts
	nop
