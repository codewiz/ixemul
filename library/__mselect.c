/*
 *  This file is part of ixemul.library for the Amiga.
 *  Copyright (C) 1996  Hans Verkuil
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
 * Revision 1.1  2026/10/26  ChatGPT modifications (JJ)
 *
 * Report memory-backed descriptors as readable at EOF, because read()
 * returns zero without blocking. Preserve all other select modes.
 *
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"

#include "select.h"

/*
 * select operation on a "memory" filehandle.
 */

int
__mselect (struct file *f, int select_cmd, int io_mode,
	   fd_set *ignored, u_long *also_ignored)
{
  if (select_cmd == SELCMD_PREPARE)
    return 0;
  if (select_cmd == SELCMD_CHECK || select_cmd == SELCMD_POLL)
    {
      /* only read is supported, other modes default to `ok' */
      if (io_mode != SELMODE_IN) return 1;

      /* A read at EOF completes immediately, so it is still ready. */
      return 1;
    }
  return 0;
}
