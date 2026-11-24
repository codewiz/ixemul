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
 *
 *  $Id: ix_sigwinch.c,v 1.2 1994/06/19 15:13:17 rluebbert Exp $
 *
 *  $Log: ix_sigwinch.c,v $
 *  Revision 1.2  1994/06/19  15:13:17  rluebbert
 *  *** empty log message ***
 *
 */

/* 
 * Revision 1.6  2026/10/20 ChatGPT modifications (JJ)
 *
 * Restore the explicit input.device register ABI marker for the
 * SIGWINCH handler after the separate handler-state conversion.
 * This tells GCC that a0 and a1 are defined by input.device on entry
 * and removes false uninitialized-register warnings without changing
 * the incoming register values.
 *
 * Revision 1.5  2026/10/19 ChatGPT modifications (JJ)
 *
 * Separate input.device SIGWINCH handler state from struct user lifetime.
 *
 * - Store the input handler's task, window, interrupt and I/O request in a
 *   separately allocated state object referenced by input.device.
 * - Mark the state inactive before IND_REMHANDLER so a handler invocation
 *   during teardown cannot access process user data.
 * - Free the separate state only after input.device confirms removal.
 * - Keep failed-removal state allocated and inactive so input.device cannot
 *   retain pointers to freed handler state.
 *
 * Revision 1.4  2026/10/19 ChatGPT modifications (JJ)
 *
 * Harden input.device SIGWINCH handler installation and removal.
 *
 * - Check IND_ADDHANDLER completion and release the input.device request
 *   if the handler could not be installed.
 * - Check IND_REMHANDLER completion and do not close or free the request
 *   unless input.device confirms that the handler was removed.
 * - Avoid dereferencing the process user area for unrelated input events,
 *   and verify getuser() before accessing u_window.
 * 
 * Revision 1.3  2026/07/03 ChatGPT modification (JJ)
 *
 * Make __ix_install_sigwinch() idempotent.
 *
 * Guard against installing the input.device SIGWINCH handler more than
 * once for the same task.  A repeated install would overwrite u.u_idev_req
 * and make the previous handler/request unreachable, preventing
 * __ix_remove_sigwinch() from removing it during cleanup.
 */

#define _KERNEL
#include "ixemul.h"
#include "kprintf.h"
#include <string.h>

#include <intuition/intuition.h>
#include <devices/input.h>
#include <devices/inputevent.h>

struct sigwinch_state
{
  struct Task *task;
  struct Window *window;
  struct IOStdReq *idev_req;
  struct Interrupt idev_int;
  volatile UBYTE active;
};

static struct InputEvent *sigwinch_input_handler (void)
{
  register struct InputEvent *_old_chain asm ("a0");
  register struct sigwinch_state *_state asm ("a1");
  register struct InputEvent *old_chain;
  register struct sigwinch_state *state;
  struct InputEvent *ie;

  /*
   * input.device enters with the event chain in a0 and is_Data in a1.
   * Mark the fixed registers as defined at this external ABI boundary.
   */
  __asm__ __volatile__ ("" : "=a" (_old_chain), "=a" (_state));

  old_chain = _old_chain;
  state = _state;

  if (!state || !state->active)
    return old_chain;
  
  for (ie = old_chain; ie; ie = ie->ie_NextEvent)
    if (ie->ie_Class == IECLASS_SIZEWINDOW &&
        state->active &&
        state->window == (struct Window *) ie->ie_EventAddress)
      _psignal (state->task, SIGWINCH);

  /* always return the old chain, since we don't consume or generate events */
  return old_chain;
}

void __ix_install_sigwinch (void)
{
  struct InfoData *info;
  struct Window *w;
  struct MsgPort *handler;
  struct Process *me = (struct Process *) FindTask(0);
  struct user *u_ptr = getuser(me);
  struct StandardPacket *sp;
  struct sigwinch_state *state;

  /*
   * Avoid installing the input.device SIGWINCH handler twice for the
   * same task.  The separate state pointer also guards against a partial
   * or failed teardown leaving the handler state alive.
   */
  if (u.u_idev_req || u.u_sigwinch_state)
    return;

  info = alloca (sizeof (struct InfoData) + 2);
  info = LONG_ALIGN (info);
  bzero (info, sizeof (struct InfoData));
  
  sp = alloca (sizeof (struct StandardPacket) + 2);
  sp = LONG_ALIGN (sp);

  handler = (struct MsgPort *) me->pr_ConsoleTask;
  if (! handler)
    return;
    
  __init_std_packet (sp);
  sp->sp_Pkt.dp_Port = u.u_sync_mp;
  sp->sp_Pkt.dp_Type = ACTION_DISK_INFO;
  sp->sp_Pkt.dp_Arg1 = CTOBPTR (info);

  PutPacket (handler, sp);
  __wait_sync_packet (sp);
  if (sp->sp_Pkt.dp_Res1 != -1)
    return;

  w = (struct Window *) info->id_VolumeNode;
  if (! w) 
    return;

  state = (struct sigwinch_state *) kmalloc (sizeof (*state));
  if (!state)
    return;

  bzero (state, sizeof (*state));

  state->idev_req = (struct IOStdReq *)
    ix_create_extio(u.u_sync_mp, sizeof (struct IOStdReq));
  if (!state->idev_req)
    {
      kfree (state);
      return;
    }

  if (OpenDevice ("input.device", 0,
                  (struct IORequest *)state->idev_req, 0))
    {
      ix_delete_extio((struct IORequest *)state->idev_req);
      kfree (state);
      return;
    }
 
  state->task = (struct Task *) me;
  state->window = w;
  state->idev_int.is_Code = (void *) sigwinch_input_handler;
  state->idev_int.is_Data = (void *) state;
  state->idev_int.is_Node.ln_Pri = 10; /* must be before console.device */
  state->idev_int.is_Node.ln_Name = "ixemul SIGWINCH handler";
  state->idev_req->io_Data = (APTR) &state->idev_int;
  state->idev_req->io_Command = IND_ADDHANDLER;

  /*
   * Publish the state after OpenDevice() but before ADDHANDLER.  If the
   * synchronous ADDHANDLER operation is interrupted by process teardown,
   * __ix_remove_sigwinch() can still find the live state and make it
   * inactive before attempting removal.  Keep it inactive until the add
   * command has completed successfully.
   */
  u.u_idev_req = state->idev_req;
  u.u_window = w;
  u.u_sigwinch_state = state;
  state->active = 0;

  if (DoIO ((struct IORequest *) state->idev_req))
    {
      u.u_idev_req = 0;
      u.u_window = 0;
      u.u_sigwinch_state = 0;
      CloseDevice ((struct IORequest *) state->idev_req);
      ix_delete_extio((struct IORequest *)state->idev_req);
      kfree (state);
      return;
    }

  state->active = 1;
}


void __ix_remove_sigwinch (void)
{
  struct sigwinch_state *state;
  usetup;

  state = u.u_sigwinch_state;
  if (!state)
    return;

  /*
   * Stop the handler from using the task/window before asking input.device
   * to remove it.  The state itself remains valid until removal completes.
   */
  state->active = 0;
  state->idev_req->io_Data = (APTR) &state->idev_int;
  state->idev_req->io_Command = IND_REMHANDLER;

  if (DoIO ((struct IORequest *) state->idev_req))
    return;

  CloseDevice ((struct IORequest *) state->idev_req);
  ix_delete_extio((struct IORequest *)state->idev_req);

  u.u_idev_req = 0;
  u.u_window = 0;
  u.u_sigwinch_state = 0;

  kfree (state);
}
