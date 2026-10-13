| 68020 index modes with a displacement beyond 8 bits: r68's (d,An,Xn), with .w where the
| number fits 16 bits, as GNU as sizes it (without it r68 makes the displacement 32 bits);
| beyond 16 bits it stays 32, and d(An) without an index beyond 16 bits is (d,An).
| fixed: a8f2037 2026-10-13 gastor68: MIT index sizes, 16-bit index displacements, .comm layout
| cpu: 68020
| expect: move.l d0,(-268.w,a5,d1.l*4)
| expect: move.l (300.w,a0,d0.l),d1
| expect: move.l d0,(-40004,a5,d1.l*4)
| expect: move.l d0,(-40004,a5)
	.text
	.globl	f
f:
	move.l %d0,-268(%a5,%d1.l*4)
	move.l (300,%a0,%d0.l),%d1
	move.l %d0,-40004(%a5,%d1.l*4)
	move.l %d0,-40004(%a5)
	move.l %d0,-4(%a5,%d1.l*4)
	rts
