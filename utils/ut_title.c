/*
 * Description:
 *     History: yang@haipo.me, 2016/03/26, create
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ut_title.h"

extern char **environ;

static char *title_base; //进程名
static char *title_tail; //进程后缀

void process_title_init(int argc, char *argv[]) {

  if (title_base) {
    return;
  }

  char *base = argv[0];
  // argc参数个数，argv参数表
  char *tail = argv[argc - 1] + strlen(argv[argc - 1]) + 1;

  char **envp = environ;
  for (int i = 0; envp[i]; ++i) {
    if (envp[i] < tail)
      continue;
    tail = envp[i] + strlen(envp[i]) + 1;
  }

  //   printf("base: %c\n", *base);
  //   printf("tail: %s\n", *tail);

  /* dup program name */
  //   char *program_invocation_name = strdup(appname);
  //   char *program_invocation_short_name = strdup(appname);

  /* dup argv */
  for (int i = 0; i < argc; ++i) {
    argv[i] = strdup(argv[i]);
  }

  /* dup environ */
  clearenv();
  for (int i = 0; envp[i]; ++i) {
    char *eq = strchr(envp[i], '=');
    if (eq == NULL)
      continue;

    *eq = '\0';
    setenv(envp[i], eq + 1, 1);
    *eq = '=';
  }

  title_base = base;
  title_tail = tail;

  memset(title_base, 0, title_tail - title_base);
}

void process_title_set(const char *fmt, ...) {
  if (!title_base)
    return;

  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  int title_len =
      len < (title_tail - title_base) ? len : (title_tail - title_base - 1);
  memcpy(title_base, buf, title_len);
  title_base[title_len] = '\0';

  return;
}
