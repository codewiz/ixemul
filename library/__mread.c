/*
 *  This file is part of ixemul.library for the Amiga.
 *  Copyright (C) 1991, 1992  Markus M. Wild
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Revision 1.1.1.2  2026/11/09  ChatGPT modifications (JJ)
 *
 *    Respect logical positions beyond EOF for memory-backed files;
 *    check file-lock acquisition and avoid out-of-bounds copies.
 *
 *  __mread.c,v 1.1.1.1 1994/04/04 04:30:11 amiga Exp
 *
 *  __mread.c,v
 * Revision 1.1.1.1  1994/04/04  04:30:11  amiga
 * Initial CVS check in.
 *
 *  Revision 1.1  1992/05/14  19:55:40  mwild
 *  Initial revision
 *
 */

#define _KERNEL
#include <string.h>
#include "ixemul.h"

int
__mread(struct file *f, char *buf, int len)
{
  usetup;
  int real_len;
  int left;
  int omask;

  omask = syscall (SYS_sigsetmask, ~0);
  if (__get_file(f) < 0)
    {
      syscall(SYS_sigsetmask, omask);
      errno = EINTR;
      return -1;
    }

  left = f->f_stb.st_size - f->f_mf.mf_offset;
  if (left <= 0 || len <= 0)
    real_len = 0;
  else
    real_len = left < len ? left : len;

  if (real_len > 0)
    {
      bcopy (((char *)f->f_mf.mf_buffer) + f->f_mf.mf_offset,
             buf, real_len);
      f->f_mf.mf_offset += real_len;
    }

  __release_file (f);
  syscall (SYS_sigsetmask, omask);

  return real_len;
}
