/*
 * ixprefscheck.c - offline ixemul preferences inspector/converter.
 *
 * Revision 1.1  2026/11/24  ChatGPT modifications (JJ)
 *
 *   Use ssize_t for write() results and retry interrupted writes.
 *   Preserve write/close errors during output-file cleanup.
 *
 * Revision 1.0  2026/11/16  ChatGPT modifications (JJ)
 *
 *   Inspect the historical 28-byte, big-endian ix_settings record.
 *   Validate fields and the 80.3 tagged block-factor encoding.
 *   Convert legacy settings to 80.3 and optionally export to 80.2.
 *   Never overwrite an existing output file or alter live settings.
 *   Warn that MEMBUF > 0 is currently unreliable in ixemul 80.3.
 *
 * This utility uses only ordinary C/POSIX file I/O.  It does not call
 * ix_get_settings() or ix_set_settings().  C89 / GCC 2.95.3 compatible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define PREF_SIZE                 28
#define PREF_MIN_VERSION         47
#define PREF_VERSION             80
#define PREF_REVISION            3
#define LEGACY_REVISION          2
#define DEFAULT_BLOCK_SIZE       512L
#define MAX_IO_SIZE              524288L
#define MAX_PHYSICAL_BLOCKS      65535L

#define FACTOR_MAGIC             0x4d000000UL
#define FACTOR_MULT_BIT          0x00010000UL
#define FACTOR_MASK              0xfffe0000UL

#define FLAG_TRANSLATE_SLASH     0x00000001UL
#define FLAG_IGNORE_ENV          0x00000040UL
#define FLAG_CASE_SENSITIVE      0x00000200UL
#define FLAG_SUPPRESS_REQUESTER  0x00000400UL
#define FLAG_AMIGA_WILDCARDS     0x00000800UL
#define FLAG_NOFLUSH             0x00001000UL
#define FLAG_ENFORCER            0x00002000UL
#define FLAG_PROFILE             0x0000c000UL
#define FLAG_STACK              0x00010000UL
#define FLAG_MUFS               0x00020000UL
#define KNOWN_FLAGS (FLAG_TRANSLATE_SLASH | FLAG_IGNORE_ENV | \
  FLAG_CASE_SENSITIVE | FLAG_SUPPRESS_REQUESTER | FLAG_AMIGA_WILDCARDS | \
  FLAG_NOFLUSH | FLAG_ENFORCER | FLAG_PROFILE | FLAG_STACK | FLAG_MUFS)

#define RC_OK 0
#define RC_WARN 5
#define RC_ERROR 10
#define RC_USAGE 20

struct prefs
{
  long version;
  long revision;
  unsigned long flags;
  long membuf;
  long old_slot;
  long factor;
  long network;
  long blocksize;
  long physical;
  long multiplier;
  long io_size;
  int packed;
  int invalid;
  int warnings;
};

static unsigned long
get_u32(const unsigned char *p)
{
  return ((unsigned long)p[0] << 24) |
         ((unsigned long)p[1] << 16) |
         ((unsigned long)p[2] << 8) |
         (unsigned long)p[3];
}

/* Convert an on-disk signed 32-bit integer without relying on the
 * host's sizeof(long) or implementation-defined unsigned casts. */
static long
get_s32(const unsigned char *p)
{
  unsigned long u = get_u32(p);
  if (u & 0x80000000UL)
    return -1L - (long)((~u) & 0x7fffffffUL);
  return (long)u;
}

static void
put_u32(unsigned char *p, unsigned long v)
{
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void
put_s32(unsigned char *p, long v)
{
  put_u32(p, (unsigned long)v & 0xffffffffUL);
}

static int
load_prefs(const char *path, struct prefs *r)
{
  FILE *f;
  unsigned char b[PREF_SIZE];
  size_t n;
  int extra;

  f = fopen(path, "rb");
  if (!f)
    {
      fprintf(stderr, "ERROR: cannot open '%s': %s\n",
              path, strerror(errno));
      return 0;
    }
  n = fread(b, 1, sizeof(b), f);
  extra = fgetc(f);
  if (ferror(f))
    {
      fprintf(stderr, "ERROR: reading '%s' failed.\n", path);
      fclose(f);
      return 0;
    }
  fclose(f);
  if (n != PREF_SIZE || extra != EOF)
    {
      fprintf(stderr,
              "ERROR: '%s' must contain exactly %d bytes.\n",
              path, PREF_SIZE);
      return 0;
    }

  memset(r, 0, sizeof(*r));
  r->version = get_s32(b + 0);
  r->revision = get_s32(b + 4);
  r->flags = get_u32(b + 8);
  r->membuf = get_s32(b + 12);
  r->old_slot = get_s32(b + 16);
  r->factor = get_s32(b + 20);
  r->network = get_s32(b + 24);
  return 1;
}

static void
check_prefs(struct prefs *r)
{
  unsigned long factor = (unsigned long)r->factor & 0xffffffffUL;
  unsigned long unknown = r->flags & ~KNOWN_FLAGS & 0xffffffffUL;

  if (r->version < PREF_MIN_VERSION || r->version > PREF_VERSION ||
      r->revision < 0 ||
      (r->version == PREF_VERSION && r->revision > PREF_REVISION))
    {
      printf("ERROR: unsupported record version/revision.\n");
      r->invalid++;
    }

  r->blocksize = (r->version == PREF_VERSION &&
                  r->revision >= PREF_REVISION && r->old_slot > 0)
                 ? r->old_slot : DEFAULT_BLOCK_SIZE;
  if (r->version == PREF_VERSION &&
      r->revision >= PREF_REVISION && r->old_slot < 0)
    {
      printf("WARNING: negative 80.3 block-size slot; "
             "ixemul defaults this to 512.\n");
      r->warnings++;
    }

  /* A tagged factor is identified by the same mask as 80.3 ix_settings.c. */
  r->packed = (factor & FACTOR_MASK) == FACTOR_MAGIC;
  if (r->packed)
    {
      r->physical = (long)(factor & 0xffffUL);
      r->multiplier = (factor & FACTOR_MULT_BIT) ? 2L : 1L;
      /* The tagged 80.3 format also used this slot in revision 3. */
      if (r->version == PREF_VERSION && r->revision >= PREF_REVISION)
        r->blocksize = r->old_slot > 0 ? r->old_slot : DEFAULT_BLOCK_SIZE;
      else
        {
          printf("WARNING: tagged factor with legacy version label; "
                 "assuming 512-byte blocks.\n");
          r->warnings++;
        }
    }
  else
    {
      r->physical = r->factor;
      r->multiplier = 1;
      if (r->version == PREF_VERSION && r->revision >= PREF_REVISION)
        {
          printf("WARNING: untagged 80.3 factor: interpreting as "
                 "physical blocks x 1. Earlier draft settings cannot "
                 "reconstruct the original multiplier.\n");
          r->warnings++;
        }
    }

  if (r->blocksize < 1 || r->blocksize > MAX_IO_SIZE ||
      r->physical < 1 || r->physical > MAX_PHYSICAL_BLOCKS ||
      (r->multiplier != 1 && r->multiplier != 2) ||
      (r->blocksize > 0 && r->blocksize <= MAX_IO_SIZE &&
       (r->physical > MAX_IO_SIZE / r->blocksize / r->multiplier)))
    {
      printf("ERROR: invalid block size, block count or multiplier "
             "(max 512 KiB).\n");
      r->invalid++;
    }
  else
    r->io_size = r->blocksize * r->physical * r->multiplier;

  if (r->membuf < 0)
    {
      printf("ERROR: negative MEMBUF limit.\n");
      r->invalid++;
    }
  else if (r->membuf > 0)
    {
      printf("WARNING: MEMBUF > 0 is currently unreliable in "
             "ixemul 80.3; use MEMBUF = 0.\n");
      r->warnings++;
    }

  if (r->network < 0 || r->network > 3)
    {
      printf("ERROR: invalid network type (expected 0..3).\n");
      r->invalid++;
    }

  if (unknown != 0)
    {
      printf("ERROR: unknown flag bits 0x%08lx.\n", unknown);
      r->invalid++;
    }
  if ((r->flags & FLAG_PROFILE) == FLAG_PROFILE)
    {
      printf("WARNING: profiling mode 3 is reserved/unused.\n");
      r->warnings++;
    }
}

static void
print_flag(unsigned long flags, unsigned long flag, const char *name)
{
  if (flags & flag)
    printf("  %s\n", name);
}

static void
show_prefs(const char *path, const struct prefs *r)
{
  printf("File:              %s\n", path);
  printf("Record:            %ld.%ld (%d bytes)\n",
         r->version, r->revision, PREF_SIZE);
  printf("Encoding:          %s\n",
         r->packed ? "80.3 tagged factor" : "legacy/untagged factor");
  printf("Flags:             0x%08lx\n", r->flags);
  printf("Enabled flags:\n");
  print_flag(r->flags, FLAG_TRANSLATE_SLASH, "Translate slash");
  print_flag(r->flags, FLAG_IGNORE_ENV, "Ignore global environment");
  print_flag(r->flags, FLAG_CASE_SENSITIVE, "Case-sensitive matching");
  print_flag(r->flags, FLAG_SUPPRESS_REQUESTER, "Suppress disk requester");
  print_flag(r->flags, FLAG_AMIGA_WILDCARDS, "Allow Amiga wildcards");
  print_flag(r->flags, FLAG_NOFLUSH, "Do not flush library");
  print_flag(r->flags, FLAG_ENFORCER, "Create Enforcer hit");
  print_flag(r->flags, FLAG_STACK, "Show stack usage");
  print_flag(r->flags, FLAG_MUFS, "Multiuser support");
  printf("Profiling mode:    %lu\n", (r->flags & FLAG_PROFILE) >> 14);
  printf("MEMBUF limit:      %ld bytes\n", r->membuf);
  if (r->membuf == 0)
    puts("MEMBUF note:       0 (recommended; >0 currently not usable reliably)");
  printf("Block size:        %ld bytes\n", r->blocksize);
  printf("Physical blocks:   %ld\n", r->physical);
  printf("I/O multiplier:    %ldx\n", r->multiplier);
  if (r->io_size)
    printf("Total I/O buffer:  %ld bytes\n", r->io_size);
  else
    puts("Total I/O buffer:  INVALID");
  printf("Stored factor:     %ld (0x%08lx)\n",
         r->factor, (unsigned long)r->factor & 0xffffffffUL);
  if (!r->packed && (r->version < PREF_VERSION ||
                     (r->version == PREF_VERSION &&
                      r->revision < PREF_REVISION)))
    printf("Old red zone:      %ld (obsolete; ignored)\n", r->old_slot);
  else
    printf("Block-size slot:   %ld\n", r->old_slot);
  printf("Network:           %ld (0=Auto, 1=None, "
         "2=AS225, 3=AmiTCP)\n", r->network);
  if (r->invalid)
    printf("RESULT: INVALID (%d error(s), %d warning(s)).\n",
           r->invalid, r->warnings);
  else if (r->warnings)
    printf("RESULT: WARNINGS (%d).\n", r->warnings);
  else
    puts("RESULT: OK.");
}

static void
make_record(unsigned char b[PREF_SIZE], const struct prefs *r,
            int to_legacy, int zero_membuf)
{
  long factor;
  memset(b, 0, PREF_SIZE);
  put_s32(b + 0, PREF_VERSION);
  put_s32(b + 4, to_legacy ? LEGACY_REVISION : PREF_REVISION);
  put_u32(b + 8, r->flags);
  put_s32(b + 12, zero_membuf ? 0 : r->membuf);
  put_s32(b + 16, to_legacy ? 0 : r->blocksize);
  if (to_legacy)
    factor = r->physical * r->multiplier;
  else
    factor = (long)(FACTOR_MAGIC |
             (r->multiplier == 2 ? FACTOR_MULT_BIT : 0) |
             (unsigned long)r->physical);
  put_s32(b + 20, factor);
  put_s32(b + 24, r->network);
}

static int
write_new_file(const char *path, const unsigned char *b)
{
  int fd;
  ssize_t n;
  int written = 0;
  int saved_errno;

  fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0)
    {
      fprintf(stderr, "ERROR: cannot create new file '%s': %s\n",
              path, strerror(errno));
      return 0;
    }
  while (written < PREF_SIZE)
    {
      n = write(fd, b + written, (unsigned int)(PREF_SIZE - written));
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        {
          saved_errno = n == 0 ? EIO : errno;
          fprintf(stderr, "ERROR: writing '%s' failed: %s\n",
                  path, strerror(saved_errno));
          close(fd);
          unlink(path);
          errno = saved_errno;
          return 0;
        }
      written += (int)n;
    }
  if (close(fd) != 0)
    {
      saved_errno = errno;
      fprintf(stderr, "ERROR: closing '%s' failed: %s\n",
              path, strerror(saved_errno));
      unlink(path);
      errno = saved_errno;
      return 0;
    }
  return 1;
}

static void
usage(const char *name)
{
  printf("Usage:\n");
  printf("  %s <prefs-file>\n", name);
  printf("  %s --convert [--zero-membuf] <input> <new-output>\n", name);
  printf("  %s --to-legacy [--zero-membuf] <input> <new-output>\n", name);
  puts("\nA separate offline checker for ixemul 28-byte preferences.");
  puts("Never overwrites an existing output file.");
  puts("--convert   Write ixemul 80.3 tagged-format preferences.");
  puts("--to-legacy Write ixemul 80.2-compatible preferences.");
  puts("--zero-membuf Explicitly set MEMBUF=0 in the converted copy.");
  puts("Output is never written during inspection.");
}

int
main(int argc, char **argv)
{
  struct prefs p;
  int to_legacy = 0;
  int convert = 0;
  int zero_membuf = 0;
  int nfiles = 0;
  const char *src = NULL;
  const char *dst = NULL;
  unsigned char out[PREF_SIZE];
  int i;

  for (i = 1; i < argc; i++)
    {
      if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
          usage(argv[0]);
          return RC_OK;
        }
      else if (!strcmp(argv[i], "--convert"))
        convert++;
      else if (!strcmp(argv[i], "--to-legacy"))
        { convert++; to_legacy++; }
      else if (!strcmp(argv[i], "--zero-membuf"))
        zero_membuf++;
      else if (argv[i][0] == '-')
        {
          fprintf(stderr, "ERROR: unknown option '%s'.\n", argv[i]);
          usage(argv[0]);
          return RC_USAGE;
        }
      else if (nfiles++ == 0)
        src = argv[i];
      else if (nfiles == 2)
        dst = argv[i];
    }

  if (!src || nfiles != (convert ? 2 : 1) ||
      convert > 1 || to_legacy > 1 || zero_membuf > 1 ||
      (!convert && zero_membuf))
    {
      usage(argv[0]);
      return RC_USAGE;
    }

  if (convert && !strcmp(src, dst))
    {
      fprintf(stderr, "ERROR: input and output must differ.\n");
      return RC_USAGE;
    }

  if (!load_prefs(src, &p))
    return RC_ERROR;
  check_prefs(&p);
  show_prefs(src, &p);

  if (!convert)
    return p.invalid ? RC_ERROR : (p.warnings ? RC_WARN : RC_OK);

  if (p.invalid)
    {
      fputs("ERROR: refusing conversion of invalid preferences.\n", stderr);
      return RC_ERROR;
    }
  if (p.membuf > 0 && !zero_membuf)
    {
      fputs("ERROR: MEMBUF > 0; rerun with --zero-membuf "
            "to reset it in the new copy.\n", stderr);
      return RC_ERROR;
    }
  if (to_legacy && p.blocksize != DEFAULT_BLOCK_SIZE)
    {
      fputs("ERROR: cannot export a non-512-byte block size to "
            "the 80.2 format without changing its meaning.\n", stderr);
      return RC_ERROR;
    }
  if (to_legacy && p.multiplier != 1)
    puts("NOTE: legacy format has no multiplier; doubling stored "
         "block factor to retain total I/O size.");
  if (zero_membuf && p.membuf)
    puts("NOTE: MEMBUF will be reset to 0 in the output only.");

  make_record(out, &p, to_legacy, zero_membuf);
  if (!write_new_file(dst, out))
    return RC_ERROR;
  printf("Created: %s (%d bytes, ixemul %d.%d format).\n",
         dst, PREF_SIZE, PREF_VERSION,
         to_legacy ? LEGACY_REVISION : PREF_REVISION);
  puts("Original preferences were not modified.");
  return RC_OK;
}
