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
 * Revision 1.7  2026/11/12  Claude/ChatGPT modifications (JJ)
 *
 * - FNDELAY path: bytes already read are now returned when a later
 *   one-byte Read() fails, and Read() == 0 is treated as EOF that ends
 *   the loop without error. Previously res was set to -1, discarding the
 *   bytes already stored in the caller's buffer, and EOF was reported as
 *   an error.
 * - Preserve the primary errno when the restoring Seek() after a failed
 *   virtual-offset probe also fails.
 * - CR/LF translation: when IXTTY_MAP_NL_TO_CR and IXTTY_MAP_CR_TO_NL
 *   are both set (termios INLCR and ICRNL in RAW mode), every byte is
 *   now mapped once, by its original value, so NL and CR are exchanged.
 *   Previously only NL -> CR was applied.
 * - Remove two dead ttyflags assignments; the snapshot taken at
 *   read_finish_io, before __release_file(), is the one that is used.
 *   Remove the always-true len > 0 test and an unreachable branch.
 * - Correct damaged comments.
 *
 * Revision 1.6  2026/11/10  ChatGPT modifications (JJ)
 *
 *  Correct AmigaDOS file handle types to BPTR in virtual-offset
 *  read handling and physical-position restoration. No change
 *  to read or TTY behavior.
 *
 * Revision 1.5  2026/11/09  ChatGPT modifications (JJ)
 *
 *  Check file-lock acquisition and honor logical offsets beyond
 *  EOF before delegating to AmigaDOS Read(). Ensure DOS failures
 *  report nonzero errno and check physical-position restoration
 *  after unsuccessful virtual-offset probes.
 *
 * Revision 1.4  2026/08/05  ChatGPT modifications (JJ)
 *
 * - Check FNDELAY before calling IsInteractive(), avoiding the historical
 *   IsInteractive() call on every normal blocking read.
 * - Snapshot f_ttyflags before releasing the file reference and use that
 *   snapshot for CR/LF post-processing, avoiding access to struct file after
 *   __release_file() without extending the reference across the buffer scan.
 *
 * Revision 1.3  2026/05/31  Copilot/ChatGPT modifications (JJ)
 *
 * Refactored internal TTY read datapath without changing the external API.
 *
 * - Extracted packet-mode pre/post logic into helpers.
 * - Extracted CR/LF translation into tty_apply_crlf_translation().
 * - Preserved RAW/cooked semantics, CR/LF mapping, errno propagation
 *   and normal interactive/non-interactive read behavior.
 * - Guard packet-mode pre-processing so a one-byte read buffer is not
 *   consumed by a packet header without room for payload.
 * - Avoid calling IsInteractive() on the normal read path unless FNDELAY
 *   is set.
 *
 *  __read.c,v 1.1.1.1 1994/04/04 04:30:12 amiga Exp
 *
 *  __read.c,v
 * Revision 1.1.1.1  1994/04/04  04:30:12  amiga
 * Initial CVS check in.
 *
 *  Revision 1.1  1992/05/14  19:55:40  mwild
 *  Initial revision
 *
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"

/*
 * Starting with ixemul 42.0 we switched from asynchronous to synchronous
 * reads.  Apparently the ADOS CON handler uses the mp_SigTask field of the
 * MsgPort struct of an ACTION_READ package to find the Task which it should
 * signal when the user presses Ctrl-C.  However, async reads use a special
 * port where the mp_SigTask field points to an Interrupt struct.  The CON
 * handler seems to either use that address as the new Task pointer (I hope
 * not) or disable Ctrl-C signalling entirely.  I never noticed this until it
 * was pointed out to me, since I always use KingCON. KingCON seems to handle
 * Ctrl-C differently. Just to be sure I also changed the ACTION_WAIT_CHAR
 * to synchronous mode.
 *
 * Starting with 43.1 I no longer use async writes at all. The overhead in
 * handling async writes turned out to be bigger than the gain.
 */

/* --- Internal helpers ---------------------------------------------------- */

/* Ensure a failed AmigaDOS operation never leaves errno equal to zero. */
static int
ix_read_io_errno(void)
{
  int err = __ioerr_to_errno(IoErr());
  return err ? err : EIO;
}

/* Restore a position saved before inspecting EOF.  The caller retains
 * its virtual offset if restoration fails; the physical DOS position
 * cannot be guaranteed when the handler rejects the restore Seek(). */
static int
ix_read_restore_pos(BPTR fh, LONG oldpos, int *err)
{
  if (Seek(fh, oldpos, OFFSET_BEGINNING) == -1)
    {
      *err = ix_read_io_errno();
      return -1;
    }
  return 0;
}

/*
 * Packet mode pre-processing:
 *   - If IXTTY_PKT is set, prepend a zero byte and reduce the available
 *     read length. Semantics identical to the inline logic previously used.
 *
 * Returns non-zero if packet mode was active, 0 otherwise.
 */
static inline int
tty_packetmode_pre(struct file *f, char **buf, int *len)
{
  /*
   * Packet mode requires space for both:
   *   - the packet header (1 byte)
   *   - at least one payload byte from Read()
   *
   * If len <= 1, there is no room for both header and payload.
   * Do not enter packet mode for this read; perform an ordinary read instead.
   */
  if ((f->f_ttyflags & IXTTY_PKT) && *len > 1)
    {
      (*len)--;
      *(*buf)++ = 0;
      return 1;
    }

  return 0;
}

/*
 * Packet mode post processing:
 *   - If packet mode was active and res > 0, increment the returned count
 *     to include the prepended zero byte.
 *
 * Returns the adjusted result.
 */
static inline int
tty_packetmode_post(int packet_mode_active, int res)
{
  if (packet_mode_active && res > 0)
    return res + 1;
  return res;
}

/*
 * CR/LF translation helper:
 *   - INLCR: NL -> CR
 *   - RAW + ICRNL: CR -> NL
 *
 * Each byte is mapped at most once, by its original value. If both
 * mappings are active, NL and CR are exchanged.
 */
static inline void
tty_apply_crlf_translation(int ttyflags, char *buf, int len)
{
  int i;
  int nl_to_cr = ttyflags & IXTTY_MAP_NL_TO_CR;
  int cr_to_nl = ttyflags & IXTTY_MAP_CR_TO_NL;

  /*
   * CR/LF mapping:
   *   - IXTTY_MAP_NL_TO_CR : NL -> CR
   *   - IXTTY_MAP_CR_TO_NL : CR -> NL
   *
   * These flags are precomputed in __tioctl.c and can be set together.
   * If neither is set, no translation is required.
   */

  if (!(nl_to_cr | cr_to_nl))
    return;

  for (i = 0; i < len; i++)
    {
      char c = buf[i];

      if (c == '\n' && nl_to_cr)
        buf[i] = '\r';
      else if (c == '\r' && cr_to_nl)
        buf[i] = '\n';
    }
}

int __read(struct file *f, char *buf, int len)
{
  usetup;
  int err = errno, res = 0;
  int omask;
  int packet_mode_active = 0;
  int ttyflags;
  char *orig_buf;

  /* --- Early exit: NIL handler always returns EOF ------------------------ */
  /*
   * If the file handle corresponds to a NIL device, there is never any data
   * to read. Returning 0 matches AmigaDOS semantics and avoids unnecessary
   * processing. No locking or signal masking is needed in this case.
   */

  /* always return EOF */
  if (HANDLER_NIL(f)) return 0;

  /* Early exit: nothing to read */
  if (len <= 0)
    return 0;

  /* Normal path begins here */

  omask = syscall (SYS_sigsetmask, ~0);
  if (__get_file (f) < 0)
    {
      syscall(SYS_sigsetmask, omask);
      errno = EINTR;
      return -1;
    }

  orig_buf = buf;
  if (f->f_type == DTYPE_FILE && f->f_virtual_valid)
    {
      LONG oldpos, eof;
      BPTR fh = CTOBPTR(f->f_fh);
      oldpos = Seek(fh, 0, OFFSET_END);
      if (oldpos == -1)
        {
          err = ix_read_io_errno();
          res = -1;
          goto read_finish_io;
        }
      eof = Seek(fh, 0, OFFSET_CURRENT);
      if (eof == -1)
        {
          int rerr;

          err = ix_read_io_errno();
          res = -1;
          (void)ix_read_restore_pos(fh, oldpos, &rerr);
          goto read_finish_io;
        }
      if (f->f_virtual_pos >= (off_t)eof)
        {
          if (ix_read_restore_pos(fh, oldpos, &err) < 0)
            res = -1;
          else
            res = 0; /* logical position stays beyond EOF */
          goto read_finish_io;
        }
      if (Seek(fh, (LONG)f->f_virtual_pos, OFFSET_BEGINNING) == -1)
        {
          int rerr;

          err = ix_read_io_errno();
          (void)ix_read_restore_pos(fh, oldpos, &rerr);
          res = -1;
          goto read_finish_io;
        }
      f->f_virtual_valid = 0;
    }
  
  /* ----------------------------------------------------------------------- */
  /*  Packet mode pre-processing                                             */
  /* ----------------------------------------------------------------------- */

  /* --- Packet mode: prepend zero byte ----------------------------------- */
  /*
   * Centralized packet mode pre-processing.
   * Semantics identical to the original inline logic.
   */
  packet_mode_active = tty_packetmode_pre(f, &buf, &len);
  orig_buf = buf;

  /* ----------------------------------------------------------------------- */
  /*  Main read logic                                                        */
  /* ----------------------------------------------------------------------- */

  /* --- Main read logic --------------------------------------------------- */
  /*
   * Two paths:
   *   1. Interactive + FNDELAY poll with WaitForChar(), read 1 byte at a time
   *   2. Normal blocking read: Read() for full length
   */

  /*
   * len >= 1 here: len <= 0 returned above, and
   * tty_packetmode_pre() only decrements len when len > 1.
   */

  /* FNDELAY slow path */
  if (__builtin_expect((f->f_flags & FNDELAY), 0) &&
      IsInteractive(CTOBPTR(f->f_fh)))
    {
      /*
       * Interactive + FNDELAY path:
       *   - Poll using WaitForChar()
       *   - Read 1 byte at a time
       *   - Bytes already read are returned; an error is reported
       *     only if no byte was read.
       *   - Read() == 0 is EOF: return what has been read so far.
       */
      while (res < len)
        {
          LONG n;

          if (!WaitForChar(CTOBPTR(f->f_fh), 0))
            {
              if (!res)
                {
                  res = -1;
                  err = EAGAIN;
                }
              break;
            }
          n = Read(CTOBPTR(f->f_fh), buf, 1);
          if (n == 1)
            {
              buf++;
              res++;
              continue;
            }
          /* If there really was no character to read, we should have
           * escaped already at the 'if WaitForChar' line. */
          if (n == -1 && !res)
            {
              err = ix_read_io_errno();
              res = -1;
            }
          break;
        }
    }
  else
    {
      /* Normal blocking read (fast path) */
      res = Read(CTOBPTR(f->f_fh), buf, len);
      if (res == -1)
        err = ix_read_io_errno();
    }

read_finish_io:
  /* Do not access f after dropping the active file reference. */
  ttyflags = f->f_ttyflags;
  __release_file (f);
  syscall (SYS_sigsetmask, omask);
  errno = err;

  /* ----------------------------------------------------------------------- */
  /*  Post processing: CR/LF translation                                     */
  /* ----------------------------------------------------------------------- */
  /* Debug trace (enabled only in debug builds) */
#ifdef DEBUG
  KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
#endif
  
  /* --- CR/LF translation ------------------------------------------------- */
  /*
   * Centralized CR/LF translation (see tty_apply_crlf_translation()).
   */
  if (res > 0)
    tty_apply_crlf_translation(ttyflags, orig_buf, res);

  /* ----------------------------------------------------------------------- */
  /*  Packet mode post processing                                            */
  /* ----------------------------------------------------------------------- */

  /* --- Packet mode post processing -------------------------------------- */
  /*
   * Centralized packet mode post-processing.
   * Semantics identical to the original inline logic.
   */
  return tty_packetmode_post(packet_mode_active, res);
}
