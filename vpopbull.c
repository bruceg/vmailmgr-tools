/* vpopbull - Delivers POP bulletins to vmailmgr users
 * Copyright (C) 1999-2002 Bruce Guenter <bruceg@em.ca>
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
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <systime.h>
#include <unistd.h>
#include <utime.h>
#include <misc/misc.h>
#include <str/str.h>
#include <msg/msg.h>
#include <cli/cli.h>
#include <iobuf/iobuf.h>

const char program[] = "vpopbull";
const int msg_show_pid = 0;
const char cli_help_prefix[] = "Delivers pop bulletins to virtual users\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "bulletin-dir [bulletin-dir2 ...] [-- command]";
const int cli_args_min = 1;
const int cli_args_max = -1;

static int o_quiet = 0;

cli_option cli_options[] = {
  { 0, "quiet", CLI_FLAG, 1, &o_quiet,
    "Suppress all status messages", 0 },
  {0,0,0,0,0,0,0}
};

#ifndef HAVE_GETHOSTNAME
int gethostname(char *name, size_t len);
#endif

static void make_link(const char* linkname, const char* dest)
{
  static str destname;
  char host[128];
  pid_t pid;
  struct stat buf;
  
  gethostname(host, sizeof host);
  pid = getpid();
  for (;;) {
    str_copy2s(&destname, dest, "/");
    str_catu(&destname, time(0));
    str_catc(&destname, '.');
    str_catu(&destname, pid);
    str_cat2s(&destname, ".", host);
    if (stat(destname.s, &buf) == -1 && errno == ENOENT)
      break;
    sleep(2);
  }
  if (symlink(linkname, destname.s) == -1)
    die5sys(111, "Could not make symbolic link from '", linkname,
	    "' to '", destname.s, "'");
}

static void link_file(const char* bulldir,
		      const char* filename,
		      const char* destdir)
{
  static str src;
  const char* ptr;
  str_copys(&src, "");
  if (bulldir[0] != '/') {
    ptr = destdir-1;
    while ((ptr = strchr(ptr+1, '/')) != 0)
      str_cats(&src, "../");
  }
  str_cat3s(&src, bulldir, "/", filename);
  return make_link(src.s, destdir);
}

static time_t maildir_time;

static time_t stat_mtime(const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) == -1)
    return 0;
  else
    return statbuf.st_mtime;
}

static void scan_file(const char* bulldir,
		      const char* filename,
		      const char* destdir)
{
  static str fullname;
  time_t mtime;
  
  str_copy3s(&fullname, bulldir, "/", filename);
  mtime = stat_mtime(fullname.s);
  if (!mtime)
    die3sys(111, "Can't stat bulletin '", fullname.s, "'");
  if (maildir_time < mtime)
    link_file(bulldir, filename, destdir);
}

static void scan_bulletins(const char* destdir, const char* bulldir)
{
  DIR* dir;
  direntry* entry;
  
  if ((dir = opendir(bulldir)) == 0)
    die3sys(111, "Can't open bulletin directory '", bulldir, "'");
  while ((entry = readdir(dir)) != 0) {
    if (entry->d_name[0] == '.') continue;
    scan_file(bulldir, entry->d_name, destdir);
  }
  closedir(dir);
}

static str timestamp;

static void stat_maildir(const char* maildir)
{
  str_copy2s(&timestamp, maildir, "/.timestamp");
  maildir_time = stat_mtime(timestamp.s);
}

static void  reset_timestamp(void)
{
  int fd;
  fd = open(timestamp.s, O_WRONLY | O_TRUNC | O_CREAT, 0600);
  close(fd);
  utime(timestamp.s, 0);
}

int cli_main(int argc, char** argv)
{
  static str dir;
  const char* maildir;
  int i;
  const char* home;
  
  if ((home = getenv("HOME")) == 0)
    die1(1, "$HOME is not set");
  if (chdir(home) == -1)
    die1sys(1, "Can't change to home directory");
  if ((maildir = getenv("MAILDIR")) == 0)
    die1(1, "$MAILDIR is not set");
  stat_maildir(maildir);
  str_copy2s(&dir, maildir, "/new");
  for (i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) { ++i; break; }
    scan_bulletins(dir.s, argv[i]);
  }
  reset_timestamp();
  if (i < argc) {
    execvp(argv[i], argv+i);
    die3sys(111, "Execution of '", argv[i], "' failed");
  }
  return 0;
}
