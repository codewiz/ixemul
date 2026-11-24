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
 *  __fstat.c,v 1.1.1.1 1994/04/04 04:30:08 amiga Exp
 *
 *  __fstat.c,v
 * Revision 1.1.1.1  1994/04/04  04:30:08  amiga
 * 
 * Initial CVS check in.
 *
 * Revision 1.8  2026/11/15  ChatGPT modifications (JJ)
 *
 * - Initialize the wide intermediate to silence GCC 2.95.3
 *   potentially-uninitialized warning; arithmetic unchanged.
 *
 * Revision 1.7  2026/11/14  ChatGPT modifications (JJ)
 *
 * - Obtain block size from ixemul settings instead of AmigaDOS Info().
 * - Remove repeated lock/Info() work, and the 32 KiB I/O limit.
 * - Use a fixed 64/128 factor, bounded to 512 KiB in settings.
 *
 * Revision 1.6  2026/11/14  ChatGPT modifications (JJ)
 *
 * - Report st_blocks in 512-byte units, including the ExamineFH
 *   fallback, without confusing zero blocks with missing data.
 * - Avoid arithmetic overflow in block accounting and cap preferred
 *   I/O block size before multiplication.
 * - Preserve raw block-size caching and existing Info() checks.
 *
 * Revision 1.5  2026/11/09  ChatGPT modifications (JJ)
 *
 *   Check file-lock acquisition and verify restoration of the DOS
 *   offset in the ExamineFH fallback, without treating virtual
 *   positions beyond EOF as changes to the real file size.
 *
 * Revision 1.4  2026/09/28  ChatGPT modifications (JJ)
 * - Cap reported st_blksize at 32 KiB.
 * - Keep cached f_fs_blocksize as the raw Info().id_BytesPerBlock value.
 * - Preserve existing st_blocks accounting and 512-byte fallback behaviour.
 *
 * Revision 1.3  2026/07/27  ChatGPT modifications (JJ)
 * - Cache the filesystem id_BytesPerBlock value per open struct file.
 * - Avoid repeated DupLockFromFH(), Info() and UnLock() calls after the
 *   first successful block-size query.
 * - Preserve the original st_blksize policy, st_blocks calculation and
 *   512-byte fallback behaviour.
 *
 *  Revision 1.2  1993/11/05  21:49:59  mw
 *  "grp/oth-perms,
 *
 *  Revision 1.1  1992/05/14  19:55:40  mwild
 *  Initial revision
 *
 */

#define _KERNEL
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "ixemul.h"
#include "kprintf.h"

/* Convert allocated filesystem blocks into POSIX 512-byte units.
 * Common power-of-two block sizes use only shifts and comparisons. */
static long
ix_fs_blocks_512(long blocks, int blocksize)
{
  unsigned long shift, limit;
  unsigned long long wide = 0;
  if (blocks <= 0)
    return 0;
  switch (blocksize)
    {
    case 512:  shift = 0; break;
    case 1024: shift = 1; break;
    case 2048: shift = 2; break;
    case 4096: shift = 3; break;
    case 8192: shift = 4; break;
    default:
      wide = (unsigned long long)(unsigned long)blocks *
             (unsigned long long)(unsigned int)blocksize;
      wide /= 512;
      return wide > LONG_MAX ? LONG_MAX : (long)wide;
    }
  limit = (unsigned long)LONG_MAX >> shift;
  if ((unsigned long)blocks > limit)
    return LONG_MAX;
  return (long)((unsigned long)blocks << shift);
}
/* Estimate 512-byte blocks without overflowing size + 511. */
static long
ix_stat_size_blocks(off_t size)
{
  if (size <= 0)
    return 0;
  return (long)(size / 512 + (size % 512 != 0));
}

#ifndef ACTION_EXAMINE_FH
#define ACTION_EXAMINE_FH 1034
#endif

/************************************************************************/
/*                                                                      */
/*    fstat() function.                                                 */
/*                                                                      */
/************************************************************************/

int
__fstat(struct file *f)
{
  long len, pos;
  struct FileInfoBlock *fib;
  int omask;
  int have_examine = 0;
  int is_interactive = IsInteractive(CTOBPTR(f->f_fh));
  struct stat *st = &f->f_stb;
  usetup;

  omask = syscall (SYS_sigsetmask, ~0);
  if (__get_file (f) < 0)
    {
      syscall (SYS_sigsetmask, omask);
      errno = EINTR;
      return -1;
    }
  if (!(f->f_stb_dirty & FSDF_OWNER))
    {
      st->st_uid = -2;
      st->st_gid = -2;
    }

  /* take special care of NIL:, /dev/null and friends ;-) */
  if (HANDLER_NIL(f))
    {
      st->st_mode = S_IFCHR | 0777;
      st->st_nlink = 1;
      st->st_blksize = ix.ix_fs_blocksize * ix.ix_fs_buf_factor;
      st->st_blocks = 0;
      goto end;
    }

  fib  = alloca (sizeof (*fib) + 2);
  fib  = LONG_ALIGN (fib);
  fib->fib_OwnerUID = fib->fib_OwnerGID = 0;

  /* we now have two possibilities.. either the filesystem understands the
   * new ACTION_EXAMINE_FH packet, or we have to do some guesses at fields.. */
  if (ExamineFH(CTOBPTR(f->f_fh), fib))
    {
      int mode = fill_stat_mode(st, fib); /* see stat.c */
      have_examine = 1;

      /* read the uid/gid data */
      if (!(f->f_stb_dirty & FSDF_OWNER))
        {
          st->st_uid = __amiga2unixid(fib->fib_OwnerUID);
          st->st_gid = __amiga2unixid(fib->fib_OwnerGID);
        }

      if (!(f->f_stb_dirty & FSDF_MODE))
        st->st_mode = mode;
      else
        st->st_mode |= (mode & 0170000);
  
      /* ARGLLLLL !!!
         Some (newer, hi Bill Hawes ;-)) handlers support EXAMINE_FH, but
         don't know yet about ST_PIPEFILE. So console windows claim they're
         plain files... Until this problem is fixed in a majority of
         handlers, do an explicit SEEK here to find those fakers.. */
      if (is_interactive || Seek(CTOBPTR(f->f_fh), 0, OFFSET_CURRENT) == -1)
        st->st_mode = (st->st_mode & ~S_IFREG) | S_IFCHR;

      /* some kind of a default-size for directories... */
      st->st_size = fib->fib_DirEntryType < 0 ? fib->fib_Size : 1024; 
      st->st_handler = (long)f->f_fh->fh_Type;
      st->st_dev = (dev_t)st->st_handler; /* trunc to 16 bit */
      st->st_ino = get_unique_id(NULL, f->f_fh);
      st->st_atime =
      st->st_ctime =
      st->st_mtime = OFFSET_FROM_1970 * 24 * 3600 + ix_get_gmt_offset() +
				  fib->fib_Date.ds_Days * 24 * 60 * 60 +
				  fib->fib_Date.ds_Minute * 60 +
				  fib->fib_Date.ds_Tick/TICKS_PER_SECOND;
      /* in a try to count the blocks used for filemanagement, we add one for
       * the fileheader. Note, that this is wrong for large files, where there
       * are some extension-blocks as well */
      st->st_blocks = fib->fib_NumBlocks == LONG_MAX ?
                      LONG_MAX : fib->fib_NumBlocks + 1;
    }
  else
    {
      /* ATTENTION: see lseek.c for Bugs in Seek() and ACTION_SEEK ! */
      /* seek to EOF */
      pos = (is_interactive ? -1 : Seek(CTOBPTR(f->f_fh), 0, OFFSET_END));
      if (pos >= 0)
        {
          len = Seek(CTOBPTR(f->f_fh), pos, OFFSET_BEGINNING);
          if (len == -1)
            {
              int error = __ioerr_to_errno(IoErr());
              /* Retry restoration without losing the original error. */
              (void)Seek(CTOBPTR(f->f_fh), pos, OFFSET_BEGINNING);
              __release_file(f);
              syscall(SYS_sigsetmask, omask);
              errno = error ? error : EIO;
              return -1;
            }
        }
      else
        len = 0;

      bzero (st, sizeof(struct stat));

      if (!(f->f_stb_dirty & FSDF_OWNER))
        {
          st->st_uid = syscall(SYS_geteuid);
          st->st_gid = syscall(SYS_getegid);
        }

      if (!(f->f_stb_dirty & FSDF_MODE))
        {
          st->st_mode = (len >= 0 && !is_interactive) ?
                        S_IFREG | (0666 & ~u.u_cmask) : S_IFCHR | 0777;
        }

      st->st_handler = (long)f->f_fh->fh_Type;
      /* the following is a limited try to support programs, that assume that
       * st_dev is always valid */
      st->st_dev = (dev_t)(long)st->st_handler; /* truncate to 16 bit */
      /* yet another kludge.. if we call this with different descriptors, we
       * should get different inode numbers.. grmpf.. */
      st->st_ino = (ino_t)~(long)(f->f_fh);
      st->st_nlink = 1; /* for now no problem.. 2.0... */
      /* len could be -1, if the device doesn't allow seeking.. */
      st->st_size = len >= 0 ? len : 0;
      st->st_atime =  
      st->st_mtime =
      st->st_ctime = syscall (SYS_time, 0);
    }

  /* The configured size replaces Info() and the file-table cache. */
  st->st_blksize = ix.ix_fs_blocksize * ix.ix_fs_buf_factor;
  if (have_examine)
    st->st_blocks = ix_fs_blocks_512(st->st_blocks,
                                      ix.ix_fs_blocksize);
  else
    st->st_blocks = ix_stat_size_blocks(st->st_size);

end:
  __release_file (f);
  syscall (SYS_sigsetmask, omask);
  return 0;
}
