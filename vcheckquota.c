/* vcheckquota.c - Check a vmailmgr account's quota on delivery.
 * Copyright (C) 2002  Bruce Guenter <bruceg@em.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sysdeps.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <systime.h>
#include <cli/cli.h>
#include <iobuf/iobuf.h>
#include <str/str.h>
#include <misc/misc.h>
#include <msg/msg.h>

const char program[] = "vcheckquota";
const int msg_show_pid = 0;
const char cli_help_prefix[] = "vmailmgr quota enforcement program\n";
const char cli_help_suffix[] = "
Warning: the soft-message is linked into the users maildir once for each
message that is received while the account is over its soft quota.  This may
result in multiple warning messages.\n";
const char cli_args_usage[] = "";
const int cli_args_min = 0;
const int cli_args_max = 0;
static unsigned soft_maxsize = 4096;
static const char* soft_message = 0;

cli_option cli_options[] = {
  { 'a', "soft-maxsize", CLI_UINTEGER, 0, &soft_maxsize,
    "The maximum message size after soft quota is reached", "4096" },
  { 'm', "soft-message", CLI_STRING, 0, &soft_message,
    "The path to the soft quota warning message", "no message" },
  {0,0,0,0,0,0,0}
};

static void oom(void) { die1(111, "Out of memory"); }

/* ========================================================================= */
/* Determine the size of a maildir, recursively */
static struct stat st;
static void wrap_stat(const char* path)
{
  if (stat(path, &st) == -1)
    die3sys(111, "Cannot stat '", path, "'");
}

static unsigned long stat_size(const char* path)
{
  wrap_stat(path);
  return st.st_blocks * 512;
}

static void scan_dir(const char* path,
		     unsigned* count, unsigned long* size,
		     void (*fn)(const char*, const str*,
				unsigned*, unsigned long*))
{
  str fullname = {0,0,0};
  DIR* dir;
  direntry* entry;
  
  if ((dir = opendir(path)) == 0)
    die3sys(111, "Could not open directory '", path, "'");

  while((entry = readdir(dir)) != 0) {
    const char* name = entry->d_name;
    if (name[0] == '.' &&
	(name[1] == 0 ||
	 (name[1] == '.' && name[2] == 0)))
      continue;
    if (!str_copys(&fullname, path) ||
	!str_catc(&fullname, '/') ||
	!str_cats(&fullname, name)) oom();
    fn(name, &fullname, count, size);
  }
  closedir(dir);
  str_free(&fullname);
}

static void stat_file(const char* name, const str* path,
		      unsigned* count, unsigned long* size)
{
  wrap_stat(path->s);
  if (S_ISREG(st.st_mode)) {
    ++*count;
    *size += st.st_blocks * 512;
  }
  /* Shut up warnings */
  name = 0;
}

/* Scan the cur/new subdirectories of a maildir */
static void stat_maildir(const str* path,
			 unsigned* count, unsigned long* size)
{
  str tmp = {0,0,0};
  if (!str_ready(&tmp, path->len + 4)) oom();

  str_copy(&tmp, path); str_cats(&tmp, "/cur"); *size += stat_size(tmp.s);
  scan_dir(tmp.s, count, size, stat_file);

  str_copy(&tmp, path); str_cats(&tmp, "/new"); *size += stat_size(tmp.s);
  scan_dir(tmp.s, count, size, stat_file);

  str_copy(&tmp, path); str_cats(&tmp, "/tmp"); *size += stat_size(tmp.s);

  str_free(&tmp);
}

/* Stat a file (don't count it), or recurse into maildirs */
static void stat_toplevel(const char* entry, const str* path,
			  unsigned* count, unsigned long* size)
{
  wrap_stat(path->s);
  *size += st.st_blocks * 512;
  if (S_ISDIR(st.st_mode)) {
    if (strcmp(entry, "cur") == 0 ||
	strcmp(entry, "new") == 0)
      scan_dir(path->s, count, size, stat_file);
    else if (strcmp(entry, "tmp") != 0)
      stat_maildir(path, count, size);
  }
}

static void size_mailbox(const char* path,
			 unsigned* count, unsigned long* size)
{
  *size = stat_size(path);
  *count = 0;
  scan_dir(path, count, size, stat_toplevel);
}

/* ========================================================================= */
/* Process quota values */
static const char* maildir;
static unsigned maxsize;
static unsigned maxcount;
static unsigned hardquota;
static unsigned softquota;

static void link_softquota_message(void)
{
  static str newdir;
  static str path;
  pid_t pid;
  
  str_copys(&newdir, maildir);
  str_cats(&newdir, "/new/");
  pid = getpid();
  for (;;) {
    str_copy(&path, &newdir);
    str_catu(&path, time(0));
    str_catc(&path, '.');
    str_catu(&path, pid);
    str_cats(&path, ".softquota-warning");
    if (symlink(soft_message, path.s) == 0)
      return;
    if (errno != EEXIST)
      die1(111, "Could not create symlink to soft quota warning message");
    sleep(1);
  }
}

static void check_quota(void)
{
  /*
   * There are 4 cases to consider when comparing disk useage (du)
   * agains hard and soft quotas:
   *
   * Case 1: soft = 0, hard = 0: user has no quota set
   * Case 2: soft = 0, hard > 0: use hard quota
   * Case 3: soft > 0, hard = 0: treat soft quota as hard
   * Case 4: soft > 0, hard > 0: if du becomes larger
   *         then soft quota, allow message in if 
   *         a) it is small (<2048 bytes), 
   *         b) it would not put du over hard quota.
   */
  unsigned long msgsize;
  unsigned count;
  unsigned long size;
  
  if (fstat(0, &st) == -1)
    die1(111, "Failed to stat message");
 
  msgsize = st.st_blocks * 512;

  if (maxsize != UINT_MAX && msgsize > maxsize)
    die1(100, "Sorry, this message is larger than the current maximum message size limit.\n");

  /* Case 1: no quotas set */
  if (softquota == UINT_MAX && hardquota == UINT_MAX && maxcount == UINT_MAX)
    return;

  size_mailbox(maildir, &count, &size);

  size += msgsize;
  ++count;

  /* too many messages in the mbox */
  if (maxcount != UINT_MAX && count > maxcount)
    die1(100, "Sorry, the person you sent this message has too many messages stored in the mailbox\n");

  /* No total size quotas are set */
  if (hardquota == UINT_MAX) {
    if (softquota == UINT_MAX)
      return;
    /* Take care of Cases 2 and 3, and make everything look like Case 4 */
    else
      hardquota = softquota;
  }
  
  /* Check hard quota before soft quota, as it has priority */
  if (size > hardquota)
    die1(100, "Message would exceed virtual user's disk quota.\n");
  
  /* Soft quota allows small (4K default) messages
   * In other words, it only blocks large messages */
  if (size > softquota) {
    if (soft_message)
      link_softquota_message();
    if (msgsize > soft_maxsize)
      die1(100, "Sorry, your message cannot be delivered.\n"
	   "User's disk quota exceeded.\n"
	   "A small message will be delivered should you wish "
	   "to inform this person.\n");
  }
}

unsigned my_strtou(const char* str, const char** end)
{
  if (*str == '-') {
    *end = str + 1;
    return UINT_MAX;
  }
  return strtou(str, end);
}

int cli_main(int argc, char** argv)
{
  const char* tmp;
#define ENV_VAR_REQ(VAR,ENV) VAR = getenv(#ENV); if (!VAR) die1(111, #ENV " is not set");
#define ENV_VAR_UINT(VAR,ENV) ENV_VAR_REQ(tmp,ENV) VAR = my_strtou(tmp, &tmp); if (*tmp != 0) die1(111, #ENV " is not a valid number");

  ENV_VAR_REQ(maildir,  MAILDIR);
  /* Always succeed for aliases. */
  if (maildir[0] == 0) return 0;

  ENV_VAR_UINT(maxsize,   VUSER_MSGSIZE);
  ENV_VAR_UINT(maxcount,  VUSER_MSGCOUNT);
  ENV_VAR_UINT(hardquota, VUSER_HARDQUOTA);
  ENV_VAR_UINT(softquota, VUSER_SOFTQUOTA);

  check_quota();
  return 0;
  /* Shut up warnings */
  argc = 0;
  argv = 0;
}
