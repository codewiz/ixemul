/*	$NetBSD: qsort.c,v 1.24 2025/03/02 16:35:41 riastradh Exp $	*/
/*-
 * Copyright (c) 1992, 1993
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
 * Revision 1.1  2026/11/11  ChatGPT modifications (JJ)
 *
 * Adapt NetBSD qsort.c revision 1.24 (2025) for ixemul and GCC 2.95.3.
 * Keep the traditional qsort() interface without the qsort_r() wrapper.
 * Use m68k-safe alignment checks for longword swaps, and guard empty
 * or invalid element-size calculations. Preserve Bentley-McIlroy
 * three-way partitioning and smaller-partition recursion.
 */

#define _KERNEL
#include "ixemul.h"
#include <stdlib.h>

/*
 * Bentley and McIlroy, "Engineering a Sort Function".
 * Three-way partitioning keeps equal keys together.
 */

static void
qsort_swapfunc(char *a, char *b, size_t n, int swaptype)
{
  if (swaptype <= 1)
    {
      size_t i = n / sizeof(long);
      long *pa = (long *)(void *)a;
      long *pb = (long *)(void *)b;
      while (i-- != 0)
        {
          long t = *pa;
          *pa++ = *pb;
          *pb++ = t;
        }
    }
  else
    {
      while (n-- != 0)
        {
          char t = *a;
          *a++ = *b;
          *b++ = t;
        }
    }
}

#define QS_SWAPINIT(a, es) \
  swaptype = (((unsigned long)(void *)(a) % sizeof(long)) != 0 || \
              (es) % sizeof(long) != 0) ? 2 : \
             ((es) == sizeof(long) ? 0 : 1)

#define QS_SWAP(a, b) do { \
  if ((a) != (b)) { \
    if (swaptype == 0) { \
      long t = *(long *)(void *)(a); \
      *(long *)(void *)(a) = *(long *)(void *)(b); \
      *(long *)(void *)(b) = t; \
    } else \
      qsort_swapfunc((a), (b), es, swaptype); \
  } \
} while (0)

#define QS_VECSWAP(a, b, n) do { \
  if ((n) > 0) \
    qsort_swapfunc((a), (b), (size_t)(n), swaptype); \
} while (0)

#define QS_MIN(a, b) ((a) < (b) ? (a) : (b))

static char *
qsort_med3(char *a, char *b, char *c,
    int (*cmp)(const void *, const void *))
{
  return cmp(a, b) < 0 ?
      (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a)) :
      (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
}

void
qsort(void *a, size_t n, size_t es,
    int (*cmp)(const void *, const void *))
{
  char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
  size_t d, r, s;
  int swaptype, cmp_result;

  /* Avoid zero-length elements and wrapped byte counts. */
  if (n < 2 || es == 0 || n > ((size_t)-1) / es)
    return;

loop:
  QS_SWAPINIT(a, es);
  if (n < 7)
    {
      for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
        for (pl = pm; pl > (char *)a && cmp(pl - es, pl) > 0;
             pl -= es)
          QS_SWAP(pl, pl - es);
      return;
    }

  pm = (char *)a + (n / 2) * es;
  pl = (char *)a;
  pn = (char *)a + (n - 1) * es;
  if (n > 40)
    {
      d = (n / 8) * es;
      pl = qsort_med3(pl, pl + d, pl + 2 * d, cmp);
      pm = qsort_med3(pm - d, pm, pm + d, cmp);
      pn = qsort_med3(pn - 2 * d, pn - d, pn, cmp);
    }
  pm = qsort_med3(pl, pm, pn, cmp);
  QS_SWAP(a, pm);

  pa = pb = (char *)a + es;
  pc = pd = (char *)a + (n - 1) * es;
  for (;;)
    {
      while (pb <= pc && (cmp_result = cmp(pb, a)) <= 0)
        {
          if (cmp_result == 0)
            {
              QS_SWAP(pa, pb);
              pa += es;
            }
          pb += es;
        }
      while (pb <= pc && (cmp_result = cmp(pc, a)) >= 0)
        {
          if (cmp_result == 0)
            {
              QS_SWAP(pc, pd);
              pd -= es;
            }
          pc -= es;
        }
      if (pb > pc)
        break;
      QS_SWAP(pb, pc);
      pb += es;
      pc -= es;
    }

  pn = (char *)a + n * es;
  r = QS_MIN((size_t)(pa - (char *)a), (size_t)(pb - pa));
  QS_VECSWAP(a, pb - r, r);
  r = QS_MIN((size_t)(pd - pc), (size_t)(pn - pd - es));
  QS_VECSWAP(pb, pn - r, r);

  /* Recurse on the smaller partition, iterate on the larger. */
  r = (size_t)(pb - pa);
  s = (size_t)(pd - pc);
  if (r < s)
    {
      if (s > es)
        {
          if (r > es)
            qsort(a, r / es, es, cmp);
          a = pn - s;
          n = s / es;
          goto loop;
        }
    }
  else
    {
      if (r > es)
        {
          if (s > es)
            qsort(pn - s, s / es, es, cmp);
          n = r / es;
          goto loop;
        }
    }
}

#undef QS_MIN
#undef QS_VECSWAP
#undef QS_SWAP
#undef QS_SWAPINIT
