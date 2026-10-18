| GCC's -mspanreg=d7 marks the file with __os9_spanreg; in Ultra C's style gastor68 then names
| the register to r68 (spanreg d7, after the psect line) and leaves every branch unsized, to an
| external too, so that r68 -bt makes a far one long through d7. Without -bt the branches come
| out as gas makes GCC's (words), so the objects stay the same.
| mode: ucc
| expect: spanreg d7
| expect: bra f2
| expect: bsr f
| expect: bne g.L2
	.set	__os9_spanreg,7
	.text
	.globl	t
t:
	tst.l %d0
	jne .L2
	jbsr f
	rts
.L2:
	jra f2
