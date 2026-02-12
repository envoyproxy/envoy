/* Placeholder for a proper logging implementation */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


void bssl_compat_log(int level, const char *file, int line, const char *fmt, ...) {
  fprintf(stderr, "%s:%d ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  if (level == BSSL_COMPAT_LOG_FATAL) {
    exit(-1);
  }
}
