#ifndef _NW_LOG_H_
#define _NW_LOG_H_

#include <stdio.h>
#include <stdlib.h>

extern FILE *nw_log_fp;

int nw_open_log();
int nw_close_log();

#define log_info(format, args...)                                              \
  do {                                                                         \
    nw_open_log();                                                             \
    fprintf(nw_log_fp, "[info]");                                              \
    fprintf(nw_log_fp, format, ##args);                                        \
    fprintf(nw_log_fp, "\n");                                                  \
    nw_close_log();                                                            \
  } while (0)

#define log_error(format, args...)                                             \
  do {                                                                         \
    nw_open_log();                                                             \
    fprintf(nw_log_fp, "[error]");                                             \
    fprintf(nw_log_fp, format, ##args);                                        \
    fprintf(nw_log_fp, "\n");                                                  \
    nw_close_log();                                                            \
  } while (0)

#endif