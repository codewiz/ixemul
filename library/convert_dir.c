/*
 *  This file is part of ixemul.library for the Amiga.
 *  Copyright (C) 1991, 1992  Markus M. Wild
 *  Portions Copyright (C) 1994 Rafael W. Luebbert
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
 */

/*
 * Revision 1.3  2026/11/09  ChatGPT modifications (JJ)
 *
 * - Check LockDosList() before enumerating root-directory volumes.
 * - Preserve and translate __lock() errors instead of forcing ENOENT.
 * - Remove redundant pathname termination after NameFromLock().
 *
 * Revision 1.2  2026/11/08  ChatGPT modifications (JJ)
 *
 * - Grow the directory buffer to fit the entire padded record.
 * - Avoid NULL-pointer arithmetic during the initial allocation.
 * - Guard size calculations against overflow.
 * - Clear buffer pointers after a failed allocation.
 * - Check NameFromLock() and Examine() before using their results.
 * - Distinguish normal ExNext() completion from DOS errors.
 * - Preserve errno across directory-buffer and lock cleanup.
 *
 * Revision 1.1  2026/06/08  ChatGPT modifications (JJ)
 *
 * - Handle kmalloc() failure when allocating f_name.
 * - Free the directory buffer, undo partial DTYPE_MEM initialization,
 *   set errno to ENOMEM and release the directory lock before returning.
 * - Leave the successful directory conversion path unchanged.
 *
 * Revision 1.1 was documented retrospectively on 2026/11/08.
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"
#include <string.h>
#include <limits.h>

extern int __mread (), __mclose ();

#define BUFINCR	512

/* we're writing records that look like this to the `file' */
struct file_dir {
  long	fd_key;
  long  fd_isdir;
  long	fd_namelen;	/* padded to even size, thus perhaps zero padded */
  char	fd_name[0];	/* fd_namelen bytes of filename */
};

struct buffer {
  u_char *buf, *bp, *bend;
  int  buf_size;
};

static int add_item(struct buffer *buf, long key, char *name, int is_dir)
{
  size_t len = strlen (name);
  size_t used = buf->buf ? (size_t)(buf->bp - buf->buf) : 0;
  size_t required;
  struct file_dir *fd;
  usetup;

  /* Include the even-byte name padding and avoid size arithmetic overflow. */
  if (len > (size_t)INT_MAX - sizeof (struct file_dir) - 1 ||
      used > (size_t)INT_MAX - (sizeof (struct file_dir) + len + (len & 1)))
    goto no_memory;

  required = used + sizeof (struct file_dir) + len + (len & 1);
  if (required > (size_t)buf->buf_size)
    {
      u_char *tmp;
      int new_size = buf->buf_size;

      /* A single increment need not accommodate the complete record. */
      while ((size_t)new_size < required)
        {
          if (new_size > INT_MAX - BUFINCR)
            goto no_memory;
          new_size += BUFINCR;
        }

      tmp = krealloc (buf->buf, (size_t)new_size);
      if (!tmp)
        goto no_memory;

      buf->buf_size = new_size;
      buf->buf = tmp;
      buf->bp = tmp + used;
      buf->bend = tmp + new_size;
    }

  fd = (struct file_dir *)buf->bp;
  fd->fd_key = key;
  fd->fd_isdir = is_dir;
  fd->fd_namelen = len;
  /* watch out for mc68000: don't let bp ever get odd ! */
  if (fd->fd_namelen & 1) 
    /* in that case zero pad the name */
    fd->fd_name[fd->fd_namelen++] = 0;

  bcopy (name, fd->fd_name, len);
  buf->bp += fd->fd_namelen + sizeof (struct file_dir);
  return 0;

no_memory:
  ix_warning("ixemul.library: out of memory!");
  kfree (buf->buf);
  buf->buf = buf->bp = buf->bend = NULL;
  buf->buf_size = 0;
  errno = ENOMEM;
  return -1;
}

/*
 * Convert a directory into a DTYPE_MEM file.
 *
 * NOTE: function assumes:
 *	 o f is allocated and locked, it's packet is initialized
 *       o name is an existing directory (S_IFDIR from a previous stat)
 *	 o signals are blocked (important! convert_dir() doesn't block them!)
 */

char *convert_dir (struct file *f, char *name, int omask)
{
  BPTR lock;
  struct buffer buf;
  struct FileInfoBlock *fib;
  int rc0;
  int saved_errno;
  LONG dos_error;
  char pathname[1024];
  usetup;

  fib = alloca (sizeof (*fib) + 2);
  fib = LONG_ALIGN (fib);

  buf.buf = buf.bp = buf.bend = NULL;
  buf.buf_size = 0;
  lock = __lock (name, ACCESS_READ);
  dos_error = lock ? 0 : IoErr();
  if (lock == 0 && dos_error == 6262)  /* root directory */
    {
      struct DosList *dl;
      int i = 3;

      /* put two dummy entries here.. some BSD code relies on the fact that
       * it can safely skip the first two `.' and `..' entries ;-)) */
      if (add_item(&buf, 1, ".", 1))
        goto do_return;
      if (add_item(&buf, 2, "..", 1))
        goto do_return;

      dl = LockDosList(LDF_VOLUMES | LDF_READ);
      if (!dl)
        {
          errno = EIO; /* LockDosList() does not set IoErr(). */
          goto do_return;
        }
      while ((dl = NextDosEntry(dl, LDF_VOLUMES)))
        {
          char name[256];
          u_char *s = BTOCPTR(dl->dol_Name);

          memcpy(name, s + 1, *s);
          name[*s] = 0;
          if (add_item(&buf, i++, name, 1))
            break;
        }
      UnLockDosList(LDF_VOLUMES | LDF_READ);
      if (dl)
        goto do_return;
      strcpy(pathname, "/");
      goto read_directory;
    }
  else if (lock > 0)
    {
      rc0 = Examine (lock, fib);
      if (!rc0)
        {
          dos_error = IoErr();
          errno = dos_error ? __ioerr_to_errno(dos_error) : EIO;
          goto do_return;
        }

      if (!NameFromLock(lock, pathname, sizeof(pathname)))
        {
          dos_error = IoErr();
          errno = dos_error ? __ioerr_to_errno(dos_error) : EIO;
          goto do_return;
        }

      /* put two dummy entries here.. some BSD code relies on the fact that
       * it can safely skip the first two `.' and `..' entries ;-)) */
      if (add_item(&buf, 1, ".", 1))
        goto do_return;
      if (add_item(&buf, 2, "..", 1))
        goto do_return;
      
      /* don't include the dir-information into the file, *ix doesn't either. */
      if (rc0)
	for (;;)
	  {
	    /* allow for a clean abort out of a very long directory scan */
	    if (u.p_sig & ~omask)
	      {
		/* if a signal is pending that was not blocked before entry to
		 * open(), break here and return with EINTR */
		errno = EINTR;
		goto do_return;
	      }

	    rc0 = ExNext (lock, fib);

    	    if (!rc0)
              {
                dos_error = IoErr();
                if (dos_error == ERROR_NO_MORE_ENTRIES)
                  break;
                errno = dos_error ? __ioerr_to_errno(dos_error) : EIO;
                goto do_return;
              }

            if (add_item(&buf, (long)get_unique_id(lock, NULL),
                         fib->fib_FileName, fib->fib_DirEntryType > 0))
              goto do_return;
	  }

read_directory:      
      /* fine.. fill out the memory file object */
      f->f_type		= DTYPE_MEM;
      f->f_mf.mf_offset = 0;
      f->f_mf.mf_buffer = buf.buf;
      f->f_read		= __mread;
      f->f_close	= __mclose;
      f->f_ioctl	= 0;
      f->f_select	= 0;
      f->f_stb.st_size	= buf.bp - buf.buf;
      /*
       * have to use kmalloc() instead of malloc(), because this is no task-private
       * data, it could (in the future) be shared by other tasks 
       */
      f->f_name = (void *)kmalloc(strlen(pathname) + 2);
      if (!f->f_name)
        {
          /* kmalloc failed: free the directory buffer and undo partial init */
          if (buf.buf)
            kfree(buf.buf);

          /* undo partial memfile init */
          f->f_type = 0;
          f->f_mf.mf_offset = 0;
          f->f_mf.mf_buffer = NULL;
          f->f_read = 0;
          f->f_close = 0;
          f->f_ioctl = 0;
          f->f_select = 0;
          f->f_stb.st_size = 0;

          if (lock)
            __unlock(lock);
          errno = ENOMEM;
          return NULL;
        }

      /* copy pathname into f->f_name */
      {
        char *p = strchr(pathname, ':');

        if (p)
        {
          *p = f->f_name[0] = '/';
          strcpy(f->f_name + 1, pathname);
        }
        else
        {
          strcpy(f->f_name, pathname);
        }
      }

      /* NOTE: the rest of the stb should be ok from the previous stat() in
       *       open() */
      if (lock)
        __unlock(lock);
      return f->f_name;
    }
  else
    {
      errno = dos_error ? __ioerr_to_errno(dos_error) : EIO;
      KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
      return NULL;
    }

  /* NOTE: granted, this is a bit spaghetti here.. the else above guarantees that
           we won't unlock a lock we never got. So it's safe to unconditionally
           unlock at the end. */

do_return:
  saved_errno = errno;
  if (buf.buf)
    kfree (buf.buf);
  if (lock)
    __unlock (lock);
  errno = saved_errno;
  KPRINTF (("&errno = %lx, errno = %ld\n", &errno, errno));
  return NULL;
}
