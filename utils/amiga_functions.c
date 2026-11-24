/*
    Ixprefs v.2.8--ixemul.library configuration program
    Copyright © 1995-2001 Kriton Kyrimis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * Revision 2.12  2026/11/15  ChatGPT modifications (JJ)
 * - Read physical-block and block-size gadgets independently; use
 *   the validated settings path for Use/Save.
 * - Avoid repeated GUI attribute refreshes on restore/reset.
 *
 * Revision 2.11  2026/11/14  ChatGPT modifications (JJ)
 *
 * - Handle the new block-size and 1x/2x multiplier controls.
 * - Add a read-only AmigaDOS block-size diagnostic action.
 */

#ifndef NO_AMIGAOS_SUPPORT

#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include "ixemul.h"
#include "version.h"
#include "amiga_gui.h"
#include "ixprefs.h"

#define RUNNING 1
#define NOT_RUNNING 0

int
selected(int which)
{
  if (ixprefsGadgets[which]->Flags & GFLG_SELECTED) {
    return 1;
  }else{
    return 0;
  }
}

int advancedClicked( void )
{
  /* routine when gadget "Enable advanced ixprefs features" is clicked. */
  advanced = !advanced;
  if (advanced) {
    AddGadget(ixprefsWnd, ixprefsGadgets[GDX_translateslash], (UWORD)~0);
    ShowChecked(GDX_translateslash, translateslash);
    AddGadget(ixprefsWnd, ixprefsGadgets[GDX_enforcerhit], (UWORD)~0);
    ShowChecked(GDX_enforcerhit, enforcerhit);
  }else{
    EraseGadget(ixprefsWnd, ixprefsGadgets[GDX_translateslash]);
    EraseGadget(ixprefsWnd, ixprefsGadgets[GDX_enforcerhit]);
  }
  return RUNNING;
}

int savegadClicked( void )
{
  /* routine when gadget "Save" is clicked. */
  blocksClicked();
  physicalBlocksClicked();
  membufClicked();
  return Save() == 0 ? NOT_RUNNING : RUNNING;
}

int usegadClicked( void )
{
  /* routine when gadget "Use" is clicked. */
  blocksClicked();
  physicalBlocksClicked();
  membufClicked();
  return Use() == 0 ? NOT_RUNNING : RUNNING;
}

int cancelgadClicked( void )
{
  /* routine when gadget "Cancel" is clicked. */
  return NOT_RUNNING;
}

int translateslashClicked( void )
{
  /* routine when gadget "translate /" is clicked. */
  translateslash = selected(GDX_translateslash);
  return RUNNING;
}

int membufClicked( void )
{
  /* routine when gadget "membuf size" is clicked. */
  membuf = GetNumber(ixprefsGadgets[GDX_membuf]);
  return RUNNING;
}

int blocksClicked( void )
{
  fs_blocksize = GetNumber(ixprefsGadgets[GDX_blocks]);
  return RUNNING;
}

int physicalBlocksClicked(void)
{
  blocks = GetNumber(ixprefsGadgets[GDX_physical_blocks]);
  return RUNNING;
}

int ioMultiplierClicked(void)
{
  io_multiplier = ixprefsMsg.Code == 1 ? 2 : 1;
  return RUNNING;
}

int reportedClicked(void)
{
  BPTR lock;
  char msg[192];
  char raw[sizeof(struct InfoData) + 4];
  struct InfoData *data;
  long bytes;

  /* 4-byte alignment is required by some DOS handlers. */
  data = (struct InfoData *)(((ULONG)raw + 3UL) & ~3UL);
  memset(data, 0, sizeof(*data));
  lock = Lock("", ACCESS_READ);
  if (!lock)
    {
      ShowRequester("Cannot lock current directory.", 0, "OK");
      return RUNNING;
    }
  if (!Info(lock, data))
    {
      UnLock(lock);
      ShowRequester("AmigaDOS Info() failed.", 0, "OK");
      return RUNNING;
    }
  bytes = data->id_BytesPerBlock;
  UnLock(lock);
  if (bytes < 512 || bytes > 4096)
    snprintf(msg, sizeof(msg),
             "Current volume reports %ld bytes/block.\n"
             "Typical range: 512-4096. Verify if unusual.", bytes);
  else
    snprintf(msg, sizeof(msg),
             "Current volume reports %ld bytes/block.", bytes);
  ShowRequester(msg, 0, "OK");
  return RUNNING;
}

int caseClicked( void )
{
  /* routine when gadget "case sensitive)" is clicked. */
  cases = selected(GDX_case);
  return RUNNING;
}

int suppressClicked( void )
{
  /* routine when gadget "suppress the \"Insert volume in drive\" requester" is clicked. */
  suppress = selected(GDX_suppress);
  return RUNNING;
}

int networkingClicked( void )
{
  /* routine when cycle gadget "Networking support" is clicked. */
  networking = ixprefsMsg.Code;
  return RUNNING;
}

int profilemethodClicked( void )
{
  /* routine when cycle gadget "Profile method" is clicked. */
  profilemethod = ixprefsMsg.Code;
  return RUNNING;
}

int amigawildcardClicked( void )
{
  /* routine when gadget "allow Amiga wildcards" is clicked. */
  amigawildcard = selected(GDX_amigawildcard);
  return RUNNING;
}

int noflushClicked( void )
{
  /* routine when gadget "do not flush library" is clicked. */
  noflush = selected(GDX_noflush);
  return RUNNING;
}

int ignoreenvClicked( void )
{
  /* routine when gadget "ignore global environment (ENV:)" is clicked. */
  ignoreenv = selected(GDX_ignoreenv);
  return RUNNING;
}

int mufsClicked( void )
{
  /* routine when gadget "enable MuFS support" is clicked. */
  mufs = selected(GDX_mufs);
  return RUNNING;
}

int stackusageClicked( void )
{
  /* routine when gadget "show stack usage" is clicked. */
  stackusage = selected(GDX_stackusage);
  return RUNNING;
}

int enforcerhitClicked( void )
{
  /* routine when gadget "Create Enforcerhit on trap" is clicked. */
  enforcerhit = selected(GDX_enforcerhit);
  return RUNNING;
}

int ixprefssave( void )
{
  /* routine when (sub)item "Save" is selected. */
  blocksClicked();
  physicalBlocksClicked();
  membufClicked();
  return Save() == 0 ? NOT_RUNNING : RUNNING;
}

int ixprefsuse( void )
{
  /* routine when (sub)item "Use" is selected. */
  blocksClicked();
  physicalBlocksClicked();
  membufClicked();
  return Use() == 0 ? NOT_RUNNING : RUNNING;
}

int ixprefsabout( void )
{
  /* routine when (sub)item "About" is selected. */
  About();
  return RUNNING;
}

int ixprefsquit( void )
{
  /* routine when (sub)item "Quit" is selected. */
  return NOT_RUNNING;
}

int ixprefsreset(void)
{
  /* routine when (sub)item "Reset to defaults" is selected. */

  Defaults();
  DisplayPrefs();
  return RUNNING;
}

int ixprefslast( void )
{
  /* routine when (sub)item "Last Saved" is selected. */
  if (LastSaved() == 0) {
    DisplayPrefs();
    }
  return RUNNING;
}

int ixprefsrestore( void )
{
  /* routine when (sub)item "Restore" is selected. */
  LoadSettings();
  DisplayPrefs();

  return RUNNING;
}

int ixprefsCloseWindow( void )
{
  /* routine for "IDCMP_CLOSEWINDOW". */
  return NOT_RUNNING;
}

#endif NO_AMIGAOS_SUPPORT
