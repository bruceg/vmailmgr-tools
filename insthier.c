#include "conf_bin.c"
#include "installer.h"

void insthier(void)
{
  int bin = opendir(conf_bin);
  c(bin, "vcheckquota", -1, -1, 0755);
  c(bin, "vpopbull",    -1, -1, 0755);
}
