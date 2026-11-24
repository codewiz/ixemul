/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Revision 1.1  2026/10/23  ChatGPT/Claude modifications (JJ)
 *
 *    Replace the C byte loop with an optimized m68k assembler
 *    implementation.  Use a byte loop for fills shorter than 12
 *    bytes and longword stores for larger fills.
 *
 *    Align destinations to longword boundaries on 68020+ and
 *    even addresses on 68000/68010.  Preserve memset() semantics
 *    and the existing calling convention.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)memset.c	5.6 (Berkeley) 1/26/91";
#endif /* LIBC_SCCS and not lint */

#include "defs.h"

/*
 * memset(dst, c, n): fill n bytes at dst with (unsigned char)c, return dst.
 *
 * Fills shorter than 12 bytes use a byte loop.  Larger fills replicate
 * the fill byte into a longword, align the destination, then use
 * longword stores followed by up to three remaining bytes.
 *
 * On 68020+, destinations are aligned to four-byte boundaries.
 * On 68000/68010, destinations are aligned to even addresses.
 *
 * Arguments are read from the stack: dst at sp@(4), c at sp@(8)
 * (its low byte at sp@(11)), and n at sp@(12).
 */
#if defined(__mc68020__) || defined(mc68020) || \
    defined(__m68020__)  || defined(m68020)  || \
    defined(__mc68030__) || defined(mc68030) || \
    defined(__m68030__)  || defined(m68030)  || \
    defined(__mc68040__) || defined(mc68040) || \
    defined(__m68040__)  || defined(m68040)  || \
    defined(__mc68060__) || defined(mc68060) || \
    defined(__m68060__)  || defined(m68060)

/* 68020+: align to four bytes before the longword loop. */
ENTRY(memset)
asm("
	movl	sp@(12),d0	/* count; MOVE sets Z for the test below */
	jeq	msret
	movl	sp@(4),a0	/* destination */
	movb	sp@(11),d1	/* fill byte: low byte of the int argument */
	cmpl	#12,d0
	jcs	msbyte		/* short fill: byte loop only */
	movl	a0,a1
	addl	d0,a1		/* a1 = end of area */
	lslw	#8,d1
	movb	sp@(11),d1	/* d1.w = fill byte twice */
	movl	d1,d0
	swap	d1
	movw	d0,d1		/* d1 = fill byte in all four bytes */
	movl	a0,d0
	negl	d0
	andl	#3,d0		/* bytes up to the next longword boundary */
	jeq	msaligned
msalign:
	movb	d1,a0@+
	subql	#1,d0
	jne	msalign
msaligned:
	movl	a1,d0
	subl	a0,d0		/* bytes left (>= 4) */
	subql	#4,d0		/* bias: carry from SUBQ ends the loop */
msloop:
	movl	d1,a0@+		/* store a longword */
	subql	#4,d0
	jcc	msloop
	andl	#3,d0		/* bytes left over, 0..3 */
	jeq	msret
msbyte:
	movb	d1,a0@+		/* store a byte (d1.b = fill byte) */
	subql	#1,d0
	jne	msbyte
msret:
	movl	sp@(4),d0	/* return dst */
	rts
");

#else

/* 68000/68010: align to even to avoid address errors. */
ENTRY(memset)
asm("
	movl	sp@(12),d0	/* count; MOVE sets Z for the test below */
	jeq	msret
	movl	sp@(4),a0	/* destination */
	movb	sp@(11),d1	/* fill byte: low byte of the int argument */
	cmpl	#12,d0
	jcs	msbyte		/* short fill: byte loop only */
	movl	a0,a1
	addl	d0,a1		/* a1 = end of area */
	lslw	#8,d1
	movb	sp@(11),d1	/* d1.w = fill byte twice */
	movl	d1,d0
	swap	d1
	movw	d0,d1		/* d1 = fill byte in all four bytes */
	movl	a0,d0
	btst	#0,d0		/* address odd? */
	jeq	msaligned
	movb	d1,a0@+		/* yes, store one byte */
msaligned:
	movl	a1,d0
	subl	a0,d0		/* bytes left (>= 4) */
	subql	#4,d0		/* bias: carry from SUBQ ends the loop */
msloop:
	movl	d1,a0@+		/* store a longword */
	subql	#4,d0
	jcc	msloop
	andl	#3,d0		/* bytes left over, 0..3 */
	jeq	msret
msbyte:
	movb	d1,a0@+		/* store a byte (d1.b = fill byte) */
	subql	#1,d0
	jne	msbyte
msret:
	movl	sp@(4),d0	/* return dst */
	rts
");

#endif
