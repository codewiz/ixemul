/*	$NetBSD: ansi.h,v 1.5 1994/10/26 07:50:40 cgd Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 * GCC v3 compatiblity modifications by Pavel Fedin <sonic_amiga@rambler.ru>, 19.05.2004
 *
 *	@(#)ansi.h	8.2 (Berkeley) 1/4/94
 */

 /*

 */

/*
 * Revision 1.3  2026/11/25  ChatGPT modifications (JJ)
 *  Do not predefine the GCC wchar_t typedef guard. Keep the historical
 *  rune_t type independent of the compiler-defined wchar_t type.
 *
 * Revision 1.2  2026/11/16  ChatGPT modifications (JJ)
 *  Leave ptrdiff_t to the compiler's <stddef.h>.  _BSD_PTRDIFF_T_
 *  is a typedef guard in GCC, so defining it here without defining
 *  ptrdiff_t hides the type when <stddef.h> is included later.
 *  Preserve the other BSD compatibility type macros.
 * 
 * Revision1.1  2026/07/21  ixemul fork ChatGPT modifications (JJ)
 *  Corrected GCC version detection for _BSD_VA_LIST_.
 *  GCC 3 and later use __builtin_va_list; older GCC and
 *  non-GCC compilers retain the historical char * definition.
 */

#ifndef	_ANSI_H_
#define	_ANSI_H_

/*
 * Types which are fundamental to the implementation and may appear in
 * more than one standard header are defined here.  Standard headers
 * then use:
 *	#ifdef	_BSD_SIZE_T_
 *	typedef	_BSD_SIZE_T_ size_t;
 *	#undef	_BSD_SIZE_T_
 *	#endif
 */
#define	_BSD_CLOCK_T_	unsigned long		/* clock() */
/* ptrdiff_t is provided by <stddef.h>; do not set its GCC guard. */
#define	_BSD_SIZE_T_	unsigned long		/* sizeof() */
#define	_BSD_SSIZE_T_	int			/* byte count or error */
#define	_BSD_TIME_T_	long			/* time() */
#if defined(__GNUC__) && (__GNUC__ >= 3)
#define _BSD_VA_LIST_ __builtin_va_list /* va_list */
#else
#define _BSD_VA_LIST_ char *			/* va_list */
#endif

/* wchar_t is defined by <stddef.h> or <stdlib.h>.  GCC uses
 * _BSD_WCHAR_T_ as a typedef guard, not as a type macro.
 * Preserve the historical rune_t type used by the ctype interface.
 */
#define	_BSD_RUNE_T_	int			/* rune_t */

#endif	/* _ANSI_H_ */
