
/*
 * Copyright (c) 1990 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * div.c,v
 *
 * Revision 1.1  2026/11/06  ChatGPT modifications (JJ)
 *
 *    Remove incorrect operand sign normalization that could
 *    produce an invalid remainder for a negative divisor.
 *
 *    Use GCC's signed division and remainder semantics directly,
 *    preserving truncation toward zero and the dividend's sign
 *    for a nonzero remainder.
 *
 *    Avoid signed overflow caused by negating INT_MIN.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)div.c	5.1 (Berkeley) 5/16/90";
#endif /* LIBC_SCCS and not lint */

#define _KERNEL
#include "ixemul.h"

#include <stdlib.h>		/* div_t */

div_t
div(int num, int denom)
{
	div_t r;

	r.quot = num / denom;
	r.rem = num % denom;

	return (r);
}
