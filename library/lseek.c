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
 *  lseek.c,v 1.1.1.1 1994/04/04 04:30:29 amiga Exp
 *
 *  Revision 1.1.1.9  2026/11/12  ChatGPT modifications (JJ)
 *
 *    Reject lseek() on interactive AmigaDOS handles with ESPIPE.
 *    Prevent console Seek() replies from creating a virtual
 *    position beyond EOF, which can cause later read() to
 *    report a false EOF. Keep regular-file seek behavior intact.
 *
 *  Revision 1.1.1.8  2026/11/11  ChatGPT modifications (JJ)
 *
 *    Restore the fast path for lseek(fd, 0, SEEK_CUR). Return the
 *    stored logical position directly when a virtual offset is
 *    active; otherwise query the physical position with a single
 *    AmigaDOS Seek(OFFSET_CURRENT) call, without moving the pointer.
 *
 *    Preserve locking, return values, errno handling and POSIX seek
 *    results. Unlike the normal path, this leaves f_virtual_valid
 *    set even if the stored virtual position is now within EOF.
 *
 *  Revision 1.1.1.7  2026/11/10  ChatGPT modifications (JJ)
 *
 *    Optimize regular-file lseek() by reducing the number of
 *    AmigaDOS Seek() calls from five to three in the normal path.
 *    Use the previous-position return value from Seek() when
 *    moving to EOF and avoid an unnecessary intermediate restore.
 *
 *    Preserve logical file positions beyond EOF, existing error
 *    handling and position restoration on failure.
 *
 *  Revision 1.1.1.6  2026/11/10  ChatGPT modifications (JJ)
 *
 *    Correct AmigaDOS file handle declarations to use BPTR
 *    instead of struct FileHandle pointers. Fix GCC 2.95.3
 *    pointer/integer conversion warnings without changing
 *    lseek() behavior.
 *
 *  Revision 1.1.1.5  2026/11/09  ChatGPT modifications (JJ)
 *
 *    Implement POSIX-compatible lseek() semantics for regular files.
 *    Track logical positions beyond EOF without extending the file
 *    until a subsequent write(). Preserve shared file positions
 *    across dup() and dup2().
 *
 *    Handle SEEK_SET, SEEK_CUR and SEEK_END using logical offsets.
 *    Check Seek() results, preserve errors and restore the original
 *    file position on failure where possible.
 *
 *    Check __get_file() before accessing or releasing file objects.
 *    Allow DTYPE_MEM seeks beyond EOF and validate seek offsets.
 *
 *    Remove the old file-extension logic, including its zero-fill
 *    allocation and redundant Seek() operations. Coordinate the
 *    new position handling with read(), write(), ftruncate(),
 *    ioctl() and file descriptor management.
 *
 *  Revision 1.1.1.4  2026/09/13  ChatGPT modifications (JJ)
 *
 *    Fix DTYPE_MEM lseek() return value to report the resulting file
 *    position instead of the previous position.
 *
 *  Revision 1.1.1.3  2026/06/23  ChatGPT modifications (JJ)
 *
 *    Validate DTYPE_FILE whence values before HANDLER_NIL handling.
 *    Added a fast path for lseek(fd, 0, SEEK_CUR), avoiding a redundant
 *    AmigaOS Seek() call when only the current file position is requested.
 *
 *  File extension semantics and EOF handling were unchanged in 1.1.1.3.
 *
 *  Revision 1.1.1.2  2026/06/17  ChatGPT modifications (JJ)
 *
 *  Harden lseek() argument validation.
 *
 *  - Validate fd before indexing u.u_ofile[] to avoid out-of-bounds
 *    access for negative or too-large file descriptors.
 *  - Reject invalid whence values with EINVAL for both regular files
 *    and memory-backed file descriptors.
 *  - Initialize internal error/result state defensively.
 *  - Guard seek-position arithmetic against signed overflow.
 *  - Treat zero-length Write() progress while extending files as EIO to
 *    avoid an infinite loop.
 *  - Set extension errno only from failing Seek() calls; clear it after
 *    successful final positioning.
 *
 *  These checks also affect extreme offsets on valid descriptors.
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"
#include <limits.h>
#include <string.h>

/* Never report a failed DOS Seek() with errno equal to zero. */
static int
ix_seek_errno(void)
{
  int err = __ioerr_to_errno(IoErr());
  return err ? err : EIO;
}

off_t
lseek (int fd, off_t off, int dir)
{
  usetup;
  struct file *f;
  int omask;
  int err = 0;
  int res = -1;
  int old_phys = -1, eof;
  int restore_needed = 0;
  off_t base, target;
  int old_virtual;
  off_t old_virtual_pos;
  int saved_errno = errno;

  if (fd >= 0 && fd < NOFILE && (f = u.u_ofile[fd]))
    {
      if (f->f_type == DTYPE_FILE)
        {
          BPTR fh;

          if (dir != SEEK_SET && dir != SEEK_CUR && dir != SEEK_END)
            errno_return(EINVAL, -1);
          if (HANDLER_NIL(f))
            {
              if (off == 0)
                return 0;
              errno_return(ESPIPE, -1);
            }

          omask = syscall(SYS_sigsetmask, ~0);
          if (__get_file(f) < 0)
            {
              syscall(SYS_sigsetmask, omask);
              errno_return(EINTR, -1);
            }

          fh = CTOBPTR(f->f_fh);
          old_virtual = f->f_virtual_valid;
          old_virtual_pos = f->f_virtual_pos;

          /* Interactive handles are not seekable POSIX files. */
          if (IsInteractive(fh))
            {
              err = ESPIPE;
              goto posix_done;
            }

          /*
           * Fast path for lseek(fd, 0, SEEK_CUR).
           *
           * A virtual offset is already known. Otherwise query
           * the current physical position without moving it.
           */
          if (dir == SEEK_CUR && off == 0)
            {
              if (old_virtual)
                res = (int)old_virtual_pos;
              else
                {
                  res = Seek(fh, 0, OFFSET_CURRENT);
                  if (res == -1)
                    err = ix_seek_errno();
                }
              goto posix_done;
            }

          /* Seek(OFFSET_END) returns the old physical position.
           * Keep it for SEEK_CUR and for restoration after errors. */
          old_phys = Seek(fh, 0, OFFSET_END);
          if (old_phys == -1)
            {
              err = ix_seek_errno();
              goto posix_done;
            }
          restore_needed = 1;
          eof = Seek(fh, 0, OFFSET_CURRENT);
          if (eof == -1)
            {
              err = ix_seek_errno();
              goto posix_done;
            }

          if (dir == SEEK_END)
            base = (off_t)eof;
          else if (dir == SEEK_CUR)
            base = old_virtual ? old_virtual_pos : (off_t)old_phys;
          else
            base = 0;

          /* The AmigaDOS file interface used by ixemul is signed 32-bit. */
          if (off < 0 && off < -base)
            {
              err = EINVAL;
              goto posix_done;
            }
          if (off > 0 && off > (off_t)INT_MAX - base)
            {
#ifdef EOVERFLOW
              err = EOVERFLOW;
#else
              err = EINVAL;
#endif
              goto posix_done;
            }
          target = base + off;

          /* The file is now physically positioned at EOF. Restore
           * the original position if the target is logical, or move
           * directly to the requested position if it is physical. */
          if (target > (off_t)eof)
            {
              if (Seek(fh, old_phys, OFFSET_BEGINNING) == -1)
                {
                  err = ix_seek_errno();
                  goto posix_done;
                }
              restore_needed = 0;
              f->f_virtual_pos = target;
              f->f_virtual_valid = 1;
            }
          else
            {
              if (Seek(fh, (LONG)target, OFFSET_BEGINNING) == -1)
                {
                  err = ix_seek_errno();
                  goto posix_done;
                }
              restore_needed = 0;
              f->f_virtual_valid = 0;
            }
          res = (int)target;

posix_done:
          if (res == -1)
            {
              f->f_virtual_valid = old_virtual;
              f->f_virtual_pos = old_virtual_pos;
              if (restore_needed && old_phys >= 0)
                {
                  /* Preserve the original error if restoration fails. */
                  (void)Seek(fh, old_phys, OFFSET_BEGINNING);
                }
            }
          __release_file(f);
          syscall(SYS_sigsetmask, omask);
          if (res == -1)
            errno = err;
          else
            errno = saved_errno;
          return (off_t)res;
        }

      else if (f->f_type == DTYPE_MEM)
        {
          int real_off;
          int old_off;
          int mem_size;

          omask = syscall(SYS_sigsetmask, ~0);
          if (__get_file(f) < 0)
            {
              syscall(SYS_sigsetmask, omask);
              errno = EINTR;
              return -1;
            }
          old_off = f->f_mf.mf_offset;
          mem_size = f->f_stb.st_size;

          /* Memory-backed files have an independent logical offset. */
          if (dir == SEEK_SET)
            base = 0;
          else if (dir == SEEK_CUR)
            base = (off_t)old_off;
          else if (dir == SEEK_END)
            base = (off_t)mem_size;
          else
            {
              __release_file(f);
              syscall(SYS_sigsetmask, omask);
              errno_return(EINVAL, -1);
            }

          if (off < 0 && off < -base)
            {
              __release_file(f);
              syscall(SYS_sigsetmask, omask);
              errno_return(EINVAL, -1);
            }
          if (off > 0 && off > (off_t)INT_MAX - base)
            {
              __release_file(f);
              syscall(SYS_sigsetmask, omask);
#ifdef EOVERFLOW
              errno_return(EOVERFLOW, -1);
#else
              errno_return(EINVAL, -1);
#endif
            }
          real_off = (int)(base + off);

          f->f_mf.mf_offset = real_off;
          __release_file(f);
          syscall(SYS_sigsetmask, omask);
          return real_off;
        }

      else
        {
          errno = ESPIPE;
          KPRINTF(("&errno = %lx, errno = %ld\n", &errno, errno));
        }
    }
  else
    {
      errno = EBADF;
      KPRINTF(("&errno = %lx, errno = %ld\n", &errno, errno));
    }

  return -1;
}
