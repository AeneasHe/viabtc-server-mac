#include <stdio.h>
#include <stdlib.h>

#include "nw_log.h"

FILE *nw_log_fp;

int nw_open_log() {
  nw_log_fp = fopen("./nw_log.log", "a+");
  return 1;
};

int nw_close_log() {
  fclose(nw_log_fp);
  return 1;
}
