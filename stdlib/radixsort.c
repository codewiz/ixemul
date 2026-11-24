/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Peter McIlroy and by Dan Bernstein at New York University, 
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * radixsort.c,v
 *
 * Revision 1.1  2026/11/09  ChatGPT modifications (JJ)
 *
 *  Import NetBSD radixsort/sradixsort algorithms and adapt them
 *  for ixemul and GCC 2.95.3.  Use reallocarray() for stable
 *  sorting, validate arguments, and make sorting state local.
 *  Preserve in-place and stable sorting behavior respectively.
 */

#define _KERNEL
#include "ixemul.h"
#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const u_char **sa;
	int sn, si;
} stack;

static void simplesort(const u_char **, int, int, const u_char *, u_int);
static void r_sort_a(const u_char **, int, int, const u_char *, u_int);
static void r_sort_b(const u_char **,
	    const u_char **, int, int, const u_char *, u_int);

#define	THRESHOLD	20		/* Divert to simplesort(). */
#define	SIZE		512		/* Default stack size. */

/* Build the default weight table, or validate the supplied one. */
#define SETUP do { \
	if (tab == NULL) { \
		tr = tr0; \
		for (c = 0; c < endch; c++) \
			tr0[c] = (u_char)(c + 1); \
		tr0[c] = 0; \
		for (++c; c < 256; c++) \
			tr0[c] = (u_char)c; \
		endch = 0; \
	} else { \
		endch = tab[endch]; \
		tr = tab; \
		if (endch != 0 && endch != 255) { \
			errno = EINVAL; \
			return -1; \
		} \
	} \
} while (0)

static int
check_args(const u_char **a, int n, u_int endch)
{
	usetup;
	if (n < 0 || endch > UCHAR_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (n != 0 && a == NULL) {
		errno = EFAULT;
		return -1;
	}
	return 0;
}

int
radixsort(const u_char **a, int n, const u_char *tab, u_int endch)
{
	const u_char *tr;
	u_int c;
	u_char tr0[256];
	usetup;

	if (check_args(a, n, endch) < 0)
		return -1;
	if (n <= 1)
		return 0;
	SETUP;
	r_sort_a(a, n, 0, tr, endch);
	return (0);
}

int
sradixsort(const u_char **a, int n, const u_char *tab, u_int endch)
{
	const u_char *tr, **ta;
	u_int c;
	u_char tr0[256];
	usetup;

	if (check_args(a, n, endch) < 0)
		return -1;
	if (n <= 1)
		return 0;
	SETUP;
	if (n < THRESHOLD)
		simplesort(a, n, 0, tr, endch);
	else {
		ta = reallocarray(NULL, (size_t)n, sizeof(*ta));
		if (ta == NULL)
			return -1;
		r_sort_b(a, ta, n, 0, tr, endch);
		free(ta);
	}
	return (0);
}

#define empty(s)	(s >= sp)
#define pop(a, n, i)	a = (--sp)->sa, n = sp->sn, i = sp->si
#define push(a, n, i)	sp->sa = a, sp->sn = n, (sp++)->si = i
#define swap(a, b, t)	t = a, a = b, b = t

/* Unstable, in-place sort. */
static void
r_sort_a(const u_char **a, int n, int i, const u_char *tr, u_int endch)
{
	u_int count[256], nc, bmin;
	u_int c;
	const u_char **ak, *r;
	stack s[SIZE], *sp, *sp0, *sp1, temp;
	u_int *cp, bigc;
	const u_char **an, *t, **aj, **top[256];


	/* Set up stack. */
	sp = s;
	push(a, n, i);
	while (!empty(s)) {
		pop(a, n, i);
		if (n < THRESHOLD) {
			simplesort(a, n, i, tr, endch);
			continue;
		}
		an = a + n;

		/* Make a fresh histogram for this partition. */
		memset(count, 0, sizeof(count));
		nc = 0;
		{
			bmin = 255;	/* First occupied bin, excluding eos. */
			for (ak = a; ak < an;) {
				c = tr[(*ak++)[i]];
				if (++count[c] == 1 && c != endch) {
					if (c < bmin)
						bmin = c;
					nc++;
				}
			}
			if (nc > (u_int)((s + SIZE) - sp)) {	/* Get more stack. */
				r_sort_a(a, n, i, tr, endch);
				continue;
			}
		}

		/*
		 * Set top[]; push incompletely sorted bins onto stack.
		 * top[] = pointers to last out-of-place element in bins.
		 * count[] = counts of elements in bins.
		 * Before permuting: top[c-1] + count[c] = top[c];
		 * during deal: top[c] counts down to top[c-1].
		 */
		sp0 = sp1 = sp;		/* Stack position of biggest bin. */
		bigc = 2;		/* Size of biggest bin. */
		if (endch == 0)		/* Special case: set top[eos]. */
			top[0] = ak = a + count[0];
		else {
			ak = a;
			top[255] = an;
		}
		for (cp = count + bmin; nc > 0; cp++) {
			while (*cp == 0)	/* Find next non-empty pile. */
				cp++;
			if (*cp > 1) {
				if (*cp > bigc) {
					bigc = *cp;
					sp1 = sp;
				}
				push(ak, *cp, i+1);
			}
			top[cp-count] = ak += *cp;
			nc--;
		}
		if (sp > sp0)
			swap(*sp0, *sp1, temp);	/* Play it safe -- biggest bin last. */

		/*
		 * Permute misplacements home.  Already home: everything
		 * before aj, and in bin[c], items from top[c] on.
		 * Inner loop:
		 *	r = next element to put in place;
		 *	ak = top[r[i]] = location to put the next element.
		 *	aj = bottom of 1st disordered bin.
		 * Outer loop:
		 *	Once the 1st disordered bin is done, ie. aj >= ak,
		 *	aj<-aj + count[c] connects the bins in a linked list;
		 *	reset count[c].
		 */
		for (aj = a; aj < an;  *aj = r, aj += count[c], count[c] = 0)
			for (r = *aj;  aj < (ak = --top[c = tr[r[i]]]);)
				swap(*ak, r, t);
	}
}

/* Stable sort, requiring additional memory. */
static void
r_sort_b(const u_char **a, const u_char **ta, int n, int i, const u_char *tr,
    u_int endch)
{
	u_int count[256], nc, bmin;
	u_int c;
	const u_char **ak, **ai;
	stack s[SIZE], *sp, *sp0, *sp1, temp;
	const u_char **top[256];
	u_int *cp, bigc;


	sp = s;
	push(a, n, i);
	while (!empty(s)) {
		pop(a, n, i);
		if (n < THRESHOLD) {
			simplesort(a, n, i, tr, endch);
			continue;
		}

		memset(count, 0, sizeof(count));
		nc = 0;
		{
			bmin = 255;
			for (ak = a + n; ak > a;) {
				--ak;
				c = tr[(*ak)[i]];
				if (++count[c] == 1 && c != endch) {
					if (c < bmin)
						bmin = c;
					nc++;
				}
			}
			if (nc > (u_int)((s + SIZE) - sp)) {
				r_sort_b(a, ta, n, i, tr, endch);
				continue;
			}
		}

		sp0 = sp1 = sp;
		bigc = 2;
		if (endch == 0) {
			top[0] = ak = a + count[0];
			count[0] = 0;
		} else {
			ak = a;
			top[255] = a + n;
			count[255] = 0;
		}
		for (cp = count + bmin; nc > 0; cp++) {
			while (*cp == 0)
				cp++;
			if ((c = *cp) > 1) {
				if (c > bigc) {
					bigc = c;
					sp1 = sp;
				}
				push(ak, c, i+1);
			}
			top[cp-count] = ak += c;
			*cp = 0;			/* Reset count[]. */
			nc--;
		}
		if (sp > sp0)
			swap(*sp0, *sp1, temp);

		for (ak = ta + n, ai = a+n; ak > ta;)	/* Copy to temp. */
			*--ak = *--ai;
		for (ak = ta+n; --ak >= ta;)		/* Deal to piles. */
			*--top[tr[(*ak)[i]]] = *ak;
	}
}

/* insertion sort */
static void
simplesort(const u_char **a, int n, int b, const u_char *tr, u_int endch)
{
	u_char ch;
	const u_char  **ak, **ai, *s, *t;


	for (ak = a+1; --n >= 1; ak++)
		for (ai = ak; ai > a; ai--) {
			for (s = ai[0] + b, t = ai[-1] + b;
			    (ch = tr[*s]) != endch; s++, t++)
				if (ch != tr[*t])
					break;
			if (ch >= tr[*t])
				break;
			swap(ai[0], ai[-1], s);
		}
}
