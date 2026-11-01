| GCC's -mbsrw calls and tail calls (bsr.w g@PLTPC, bra.w g@PLTPC): the @PLTPC operator, in a
| static link the symbol itself, is dropped; r68 took g@PLTPC as a name, which no file defines.
| fixed: 2026-11-01 gastor68: GNU's @PLTPC dropped
| expect: bsr.w g
| expect: bra.w g
	.text
	.globl	f
f:
	bsr.w g@PLTPC
	addq.l #1,%d0
	rts
	.globl	h
h:
	bra.w g@PLTPC
