/*-
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ronnie Kon at Mindcraft Inc., Kevin Lew and Elmer Yglesias.
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
 *   Adapt the NetBSD heapsort algorithm (common/lib/libc/stdlib/heapsort.c,
 *   revision 1.4, 2025/03/02) for ixemul and GCC 2.95.3.
 *   Retain the heapsort() API and ixemul's per-process errno handling.
 *   Use Floyd's sift-down and upward insertion, an element-sized buffer,
 *   zero-based indexing and size_t heap indices.  Avoid pointer-before-
 *   array arithmetic and check size multiplication for overflow.
 *   Do not introduce heapsort_r() or modify the build system.
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * Restore a max-heap after replacing element 'start' with the saved
 * element in 'saved'.  Descend by promoting the larger child, then
 * insert the saved element on the way back up (Floyd's method).
 * The elements start at index zero; pointers stay within the array.
 */
static void
heap_sift(char *base, size_t start, size_t count, size_t size,
          int (*compar)(const void *, const void *), char *saved)
{
    size_t pos, child, parent;

    pos = start;
    while (pos < count / 2) {
        child = pos * 2 + 1;
        if (child + 1 < count &&
            compar(base + child * size,
                   base + (child + 1) * size) < 0)
            ++child;

        memcpy(base + pos * size, base + child * size, size);
        pos = child;
    }

    while (pos > start) {
        parent = (pos - 1) / 2;
        if (compar(saved, base + parent * size) <= 0)
            break;
        memcpy(base + pos * size, base + parent * size, size);
        pos = parent;
    }

    memcpy(base + pos * size, saved, size);
}

int
heapsort(void *bot, size_t nmemb, size_t size,
         int (*compar)(const void *, const void *))
{
    char *base, *saved;
    size_t i;
    usetup;

    if (nmemb <= 1)
        return 0;

    if (size == 0 || nmemb > ((size_t)-1) / size) {
        errno = EINVAL;
        return -1;
    }

    saved = (char *)malloc(size);
    if (saved == NULL)
        return -1;

    base = (char *)bot;

    /* Build a max-heap, starting with the last parent node. */
    for (i = nmemb / 2; i != 0; ) {
        --i;
        memcpy(saved, base + i * size, size);
        heap_sift(base, i, nmemb, size, compar, saved);
    }

    /* Move the maximum to the end and restore the remaining heap. */
    for (i = nmemb; i > 1; ) {
        --i;
        memcpy(saved, base + i * size, size);
        memcpy(base + i * size, base, size);
        heap_sift(base, 0, i, size, compar, saved);
    }

    free(saved);
    return 0;
}
