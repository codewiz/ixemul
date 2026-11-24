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
 *    Correct FIONREAD for AmigaDOS Seek() return semantics and logical
 *    positions beyond EOF. Check file-lock acquisition and preserve
 *    the DOS position across the EOF query, reporting restore errors.
 *    Report EOF-probe failures separately from unsupported seeking and
 *    use EIO if DOS error translation yields no valid errno.
 *
 *  __fioctl.c,v 1.1.1.1 1994/04/04 04:29:44 amiga Exp
 *
 *  __fioctl.c,v
 * Revision 1.1.1.1  1994/04/04  04:29:44  amiga
 * Initial CVS check in.
 *
 *  Revision 1.1  1992/05/17  21:00:51  mwild
 *  Initial revision
 *
 * Revision 1.1  1992/05/14  19:55:40  mwild
 * Initial revision
 *
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"
#include <sys/ioctl.h>

extern int __read(), __aread(), __write(), __awrite();

/* IOCTLs on files in general */

int
__fioctl(struct file *f, unsigned int cmd, unsigned int inout,
         unsigned int arglen, unsigned int arg)
{
  usetup;
  int omask;
  int result = 0;
  int err=errno;
  
  omask = syscall (SYS_sigsetmask, ~0);
  if (__get_file (f) < 0)
    {
      syscall (SYS_sigsetmask, omask);
      errno = EINTR;
      return -1;
    }

  switch (cmd)
    {
    case FIONREAD:
      {
	unsigned int *pt = (unsigned int *)arg;
	if (!IsInteractive(CTOBPTR(f->f_fh)))
	  {
            LONG this_pos, old_pos, eof_pos;
            int seek_err;
            BPTR fh = CTOBPTR(f->f_fh);

            /* Seek() returns the previous position. Query EOF with a
             * second Seek(), then restore the original DOS position. */
            this_pos = Seek(fh, 0, OFFSET_CURRENT);
            if (this_pos >= 0)
              {
                old_pos = Seek(fh, 0, OFFSET_END);
                if (old_pos == -1)
                  {
                    /* The current position was readable, so an EOF
                     * probe failure is an I/O error, not a reason to
                     * use the non-seekable-handler fallback. */
                    seek_err = __ioerr_to_errno(IoErr());
                    if (Seek(fh, this_pos, OFFSET_BEGINNING) == -1)
                      {
                        int restore_err = __ioerr_to_errno(IoErr());
                        err = restore_err ? restore_err : EIO;
                      }
                    else
                      err = seek_err ? seek_err : EIO;
                    result = -1;
                    goto ret;
                  }

                eof_pos = Seek(fh, 0, OFFSET_CURRENT);
                if (eof_pos == -1)
                  seek_err = __ioerr_to_errno(IoErr());
                else
                  seek_err = 0;

                if (Seek(fh, this_pos, OFFSET_BEGINNING) == -1)
                  {
                    int restore_err = __ioerr_to_errno(IoErr());
                    err = restore_err ? restore_err : EIO;
                    result = -1;
                    goto ret;
                  }

                if (eof_pos == -1)
                  {
                    err = seek_err ? seek_err : EIO;
                    result = -1;
                    goto ret;
                  }

                {
                  off_t logical_pos = f->f_virtual_valid ?
                    f->f_virtual_pos : (off_t)this_pos;
                  *pt = logical_pos >= (off_t)eof_pos ? 0 :
                    (unsigned int)((off_t)eof_pos - logical_pos);
                }
                result = 0;
                goto ret;
              }
	    /* well, since the device can't seek, AND it's not	    
	     * interactive, chances are bad, we ever will get at the
	     * right result, but we'll try nevertheless the WaitForChar
	     * Packet, it can only fail... */
	  }
	/* if the docs would all speak the same language... some
	 * say, that the timeout should be in 1/50s, others say
	 * its actually in micro/s.. who knows.. */
	*pt = WaitForChar(CTOBPTR(f->f_fh), 0) != 0;
	result = 0;
        goto ret;
      }

    case FIONBIO:
      {
	/* that's probably the most inefficient part of the whole
	 * library... */
	result = f->f_flags & FNDELAY ? 1 : 0;
	if (*(unsigned int *)arg)
	  f->f_flags |= FNDELAY;
	else
	  f->f_flags &= ~FNDELAY;
	/* I didn't find it documented in a manpage, but I assume, we
	 * should return the former state, not just zero.. */
	goto ret;
      }

    case FIOASYNC:
      {
	/* DOESN'T WORK YET */

	int flags = *(unsigned long*)arg;
	result = f->f_flags & FASYNC ? 1 : 0;
	if (flags)
	  f->f_flags |= FASYNC;
	else
	  f->f_flags &= ~FASYNC;

	/* ATTENTION: have to call some function here in the future !!! */

	/* I didn't find it documented in a manpage, but I assume, we
	 * should return the former state, not just zero.. */
	goto ret;
      }

    case FIOCLEX:
    case FIONCLEX:
    case FIOSETOWN:
    case FIOGETOWN:
      /* this is no error, but nevertheless we don't take any actions.. */      
      result = 0; goto ret;
    }

ret:
    __release_file (f);
    syscall (SYS_sigsetmask, omask);
    errno = err;
    KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
    return result;
}
