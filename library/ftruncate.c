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
 * 
 * Revision 1.7  2026/11/16  Claude modifications (JJ)
 *
 *      Do not use the SetFileSize() return value as the resulting file
 *      length.  The ENV: handler resizes the file but returns the old
 *      length (measured 2026-11-16: ixemul 80.3 open(O_TRUNC) on ENV:
 *      failed with EIO although the file had been truncated to 0).
 *      After a SetFileSize() that did not return -1, the resulting EOF
 *      is read back with Seek(OFFSET_END)/Seek(OFFSET_CURRENT) and
 *      compared with len. The returned value decides only when the EOF
 *      cannot be read back.
 *
 * Revision 1.6  2026/11/13  ChatGPT modifications (JJ)
 *
 *      Reject ftruncate() on interactive AmigaDOS file handles with
 *      EINVAL before any Seek() or SetFileSize() operation. Prevent
 *      invalid virtual file positions on console handles.
 *
 * Revision 1.5  2026/11/09  ChatGPT modifications (JJ)
 *
 *      Preserve POSIX file position when resizing regular files,
 *      including virtual positions beyond EOF. Validate file-lock
 *      acquisition. Keep the existing FFS2 SetFileSize() workaround.
 *      Retain a virtual offset if physical position restoration fails.
 *      Do not confuse the saved DOS position with EOF on resize errors;
 *      preserve the original resize error if restoration also fails.
 *
 * Revision 1.4  2026/08/12  ChatGPT modifications (JJ)
 *
 *      Reject negative lengths and descriptors without FWRITE access.
 *      Treat a SetFileSize() result different from the requested length
 *      as an error, and stop before SetFileSize() if the preparatory
 *      Seek() fails.
 *
 * Revision 1.3  2026/08/01  ChatGPT modification (JJ)
 *      Added the FFS2-compatible SetFileSize() seek workaround from the
 *      later ixemul branch.  The current file position is saved by seeking
 *      to the beginning before SetFileSize(), then restored afterwards.
 *      If the file was shortened below the old position, the position is
 *      restored to the new end of file, matching the handler workaround.
 *
 * Revision 1.2  2026/06/07  Copilot/ChatGPT modifications (JJ)
 *      Normalized ftruncate() return value to POSIX semantics (0 on
 *      success, -1 on error), added early fd bounds check to avoid
 *      out-of-range indexing of u.u_ofile[], and simplified the open
 *      file test to 'if (f)' since fd is now validated.
 *
 *
 *  ftruncate.c,v 1.1.1.1 1994/04/04 04:30:18 amiga Exp
 *
 *  ftruncate.c,v
 * Revision 1.1.1.1  1994/04/04  04:30:18  amiga
 * Initial CVS check in.
 *
 *  Revision 1.1  1992/05/14  19:55:40  mwild
 *  Initial revision
 *
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"

#ifndef ACTION_SET_FILE_SIZE
#define ACTION_SET_FILE_SIZE 1022
#endif

int
ftruncate (int fd, off_t len)
{
  usetup;
  struct file *f;
  int err, res;
  int omask;

  /* validate fd before indexing u.u_ofile[] */
  if (fd < 0 || fd >= NOFILE)
    {
      errno = EBADF;
      return -1;
    }

  if (len < 0)
    {
      errno = EINVAL;
      return -1;
    }

  f = u.u_ofile[fd];

  /* if this is an open fd */
  if (f)
    {
      if (f->f_type == DTYPE_FILE)
	{
          BPTR fh;
          LONG old_pos;
          LONG physical_target;
          LONG probed_eof;
          off_t logical_pos;
          int old_virtual;
          int successful_resize;
          int saved_errno;

          if ((f->f_flags & FWRITE) == 0)
            {
              errno = EBADF;
              KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
              return -1;
            }

          if (HANDLER_NIL (f))
	    {
	      errno = EINVAL;
	      KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
	      return -1;
	    }

          err = 0;
          res = -1;
          successful_resize = 0;
          probed_eof = -1;
          saved_errno = errno;
          omask = syscall (SYS_sigsetmask, ~0);
          if (__get_file(f) < 0)
            {
              syscall(SYS_sigsetmask, omask);
              errno_return(EINTR, -1);
            }

          fh = CTOBPTR(f->f_fh);

          /* Interactive handles must never acquire virtual offsets. */
          if (IsInteractive(fh))
            {
              err = EINVAL;
              goto ftruncate_done;
            }

          old_virtual = f->f_virtual_valid;
          old_pos = Seek(fh, 0, OFFSET_BEGINNING);
          if (old_pos == -1)
            {
              err = __ioerr_to_errno(IoErr());
              if (!err) err = EIO;
            }
          else
            {
              logical_pos = old_virtual ? f->f_virtual_pos : (off_t)old_pos;

              /* FFS2 requires the file pointer to be at the beginning. */
              res = SetFileSize(fh, len, OFFSET_BEGINNING);
              if (res == -1)
                {
                  err = __ioerr_to_errno(IoErr());
                  if (!err) err = EIO;
                }
              else
                {
                  /* The returned value is not reliable: ENV: resizes the
                     file but returns the old length.  Read the resulting
                     EOF back; the returned value decides only when the
                     EOF cannot be read. */
                  if (Seek(fh, 0, OFFSET_END) != -1)
                    probed_eof = Seek(fh, 0, OFFSET_CURRENT);

                  if (probed_eof >= 0 ? (off_t)probed_eof == len
                                      : (off_t)res == len)
                    successful_resize = 1;
                  else
                    {
                      err = EIO;
                      res = -1;
                    }
                }

              /* Restore the logical offset, even when the new EOF is
                 smaller than the original position.  Physical seeking
                 beyond EOF is not available on AmigaDOS. */
              if (successful_resize && logical_pos > len)
                {
                  physical_target = (LONG)len;
                  f->f_virtual_pos = logical_pos;
                  f->f_virtual_valid = 1;
                }
              else if (successful_resize)
                {
                  physical_target = (LONG)logical_pos;
                  f->f_virtual_valid = 0;
                }
              else
                {
                  /* SetFileSize() failed and may have changed the file.
                     Probe the actual EOF rather than interpreting the
                     saved DOS position as the file size. */
                  LONG actual_eof;

                  if (probed_eof < 0 && Seek(fh, 0, OFFSET_END) != -1)
                    probed_eof = Seek(fh, 0, OFFSET_CURRENT);
                  actual_eof = probed_eof;

                  if (actual_eof >= 0)
                    {
                      physical_target = logical_pos > (off_t)actual_eof
                                      ? actual_eof : (LONG)logical_pos;
                      if (logical_pos > (off_t)actual_eof)
                        {
                          f->f_virtual_pos = logical_pos;
                          f->f_virtual_valid = 1;
                        }
                      else
                        f->f_virtual_valid = 0;
                    }
                  else
                    {
                      /* EOF is unknown.  Position at the beginning if
                         possible and keep the original logical offset
                         virtual; do not guess EOF from old_pos. */
                      physical_target = 0;
                      f->f_virtual_pos = logical_pos;
                      f->f_virtual_valid = 1;
                    }
                }

              if (Seek(fh, physical_target, OFFSET_BEGINNING) == -1)
                {
                  int restore_err = __ioerr_to_errno(IoErr());
                  /* Keep the required position representable even when
                     the DOS handler refuses physical restoration. */
                  f->f_virtual_pos = logical_pos;
                  f->f_virtual_valid = 1;
                  /* A failed resize is the primary error.  Only a
                     successful resize can be turned into an error by
                     failed restoration. */
                  if (successful_resize)
                    err = restore_err ? restore_err : EIO;
                  res = -1;
                }
              else if (successful_resize)
                {
                  err = 0;
                  res = 0;
                }
            }

        ftruncate_done:
	  __release_file (f);
	  syscall (SYS_sigsetmask, omask);
          errno = res < 0 ? err : saved_errno;
	  KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
	  return res;
	}
      else
	{
	  errno = ESPIPE;
	  KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
	  return -1;
	}
    }

  errno = EBADF;
  KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
  return -1;
}
