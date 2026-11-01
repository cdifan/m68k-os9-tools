| Blanks in an operand field (GCC's subq.l #8, %sp when it pushes a double; inline assembly)
| are dropped: r68's operand field ends at the first blank, so the rest was taken as a comment
| ("Missing operand").
| fixed: 6d9cb36 2026-11-01 gastor68: no blanks in operands
| expect: subq.l #8,sp
| expect: move.l d0,(a0,d1.l)
| expect: add.l #3,d2
	.text
	.globl	f
f:
	subq.l #8, %sp
	move.l %d0, (%a0, %d1.l)
	add.l	#3 ,	%d2
	addq.l #8,%sp
	rts
