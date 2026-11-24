#define _KERNEL
#include <ixemul.h>
#include "version.h"

/* Revision 1.2  2026/11/15  ChatGPT modifications (JJ)
 * Preserve adjustable physical-block count and separate 1x/2x setting
 * in tagged 28-byte preference records; keep the live I/O factor plain.
 * Recognize legacy factors and validate the 512 KiB bound before use.
 * Keep saved components across ix_get_settings() calls.
 */

/* Revision 1.1  2026/11/14  ChatGPT modifications (JJ)
 * Reuse the old settings layout: zero block size means legacy
 * default 512, and old buffer factors map to the standard 1x.
 * Validate block size and fixed 1x/2x factors at the API boundary.
 */

/* These are process-wide library settings, not part of the public ABI.
 * The ix base retains an ordinary effective factor for stat/fstat. */
static int ix_physical_blocks = IX_DEFAULT_FS_BUF_FACTOR;
static int ix_io_multiplier = 1;

long ix_get_gmt_offset(void)
{
  return ix.ix_gmt_offset;
}

void ix_set_gmt_offset(long offset)
{
  ix.ix_gmt_offset = offset;
}

struct ix_settings *ix_get_settings(void)
{
  static struct ix_settings settings;

  settings.version = IX_VERSION;
  settings.revision = IX_REVISION;
  settings.flags = ix.ix_flags;
  settings.membuf_limit = ix.ix_membuf_limit;
  settings.fs_blocksize = ix.ix_fs_blocksize;
  settings.fs_buf_factor = IX_FS_FACTOR_PACK(ix_physical_blocks,
                                               ix_io_multiplier);
  settings.network_type = ix.ix_network_type;
  return &settings;
}

struct ix_settings *ix_get_default_settings(void)
{
  static struct ix_settings default_settings =
  {
    IX_VERSION,
    IX_REVISION,
    ix_translate_slash | ix_no_insert_disk_requester | ix_allow_amiga_wildcard,
    0,			/* membuf_limit  */
    IX_DEFAULT_FS_BLOCKSIZE, /* fs_blocksize */
    IX_DEFAULT_FS_BUF_FACTOR, /* fs_buf_factor: legacy format */
    IX_NETWORK_AUTO	/* network_type */
  };

  return &default_settings;
}

void ix_set_settings(struct ix_settings *settings)
{
  int physical, multiplier, blocksize, maxblocks;
  unsigned long stored;
  if (!settings)
    return;

  /* Preserve old files: the now-reused slot was red_zone_size and
   * normally contained zero.  Unmarked factors denote physical blocks
   * with multiplier 1, as in the original ixprefs. */
  stored = (unsigned long)settings->fs_buf_factor;
  if (IX_FS_FACTOR_IS_PACKED(stored))
    {
      physical = IX_FS_FACTOR_BLOCKS(stored);
      multiplier = IX_FS_FACTOR_MULT(stored);
    }
  else
    {
      physical = settings->fs_buf_factor;
      multiplier = 1;
    }
  blocksize = settings->fs_blocksize > 0 ?
              settings->fs_blocksize : IX_DEFAULT_FS_BLOCKSIZE;

  /* Divide before multiplying: even corrupted preference records cannot
   * overflow a signed 32-bit int when converted to st_blksize. */
  maxblocks = blocksize > 0 && blocksize <= IX_MAX_IO_SIZE ?
              IX_MAX_IO_SIZE / blocksize : 0;
  if (multiplier != 1 && multiplier != 2)
    maxblocks = 0;
  else
    maxblocks /= multiplier;
  if (physical < 1 || physical > IX_FS_FACTOR_MAX_BLOCKS ||
      physical > maxblocks)
    {
      physical = IX_DEFAULT_FS_BUF_FACTOR;
      multiplier = 1;
      blocksize = IX_DEFAULT_FS_BLOCKSIZE;
    }
  ix_physical_blocks = physical;
  ix_io_multiplier = multiplier;
  ix.ix_fs_blocksize = blocksize;
  ix.ix_fs_buf_factor = physical * multiplier;

  if (ix.ix_flags & ix_do_not_flush_library)
    if (!(settings->flags & ix_do_not_flush_library))
      ix.ix_lib.lib_OpenCnt--;
  if (!(ix.ix_flags & ix_do_not_flush_library))
    if (settings->flags & ix_do_not_flush_library)
      ix.ix_lib.lib_OpenCnt++;
  ix.ix_flags = settings->flags;
  if (settings->membuf_limit >= 0)
    ix.ix_membuf_limit = settings->membuf_limit;
  if (settings->network_type >= 0 &&
      settings->network_type < IX_NETWORK_END_OF_ENUM)
    ix.ix_network_type = settings->network_type;
}

long ix_get_long(unsigned long id, unsigned long extra)
{
  usetup;

  switch (id)
  {
    case IXID_VERSION:
      return ix.ix_lib.lib_Version;

    case IXID_REVISION:
      return ix.ix_lib.lib_Revision;

    case IXID_USERDATA:
      return (long)u.u_user;

    case IXID_USER:
      return (long)&u;

    case IXID_A4_PTRS:
      return u.u_a4_pointers_size;

    case IXID_HAVE_FPU:
      return has_fpu;

    case IXID_CPU:
      if (has_68060_or_up)
        return IX_CPU_68060;
      if (has_68040_or_up)
        return IX_CPU_68040;
      if (has_68030_or_up)
        return IX_CPU_68030;
      if (has_68020_or_up)
        return IX_CPU_68020;
      if (has_68010_or_up)
        return IX_CPU_68010;
      return IX_CPU_68000;

    case IXID_OS:
      return OS_IS_AMIGAOS;

    case IXID_OFILE:
      return (long)u.u_ofile;

    case IXID_ENVIRON:
      return (long)u.u_environ;
      
    case IXID_EXPAND_CMD_LINE:
      return u.u_expand_cmd_line;
  }
  return -1;
}

long ix_set_long(unsigned long id, long value)
{
  usetup;
  long result = -1;

  switch (id)
  {
    case IXID_USERDATA:
      result = (long)u.u_user;
      u.u_user = (void *)value;
      break;
   
    case IXID_ENVIRON:
      result = (long)u.u_environ;
      u.u_environ = (void *)value;
      break;
      
    case IXID_EXPAND_CMD_LINE:
      result = (long)u.u_expand_cmd_line;
      u.u_expand_cmd_line = value;
      break;
  }
  return result;
}
