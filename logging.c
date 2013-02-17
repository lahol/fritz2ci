#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdarg.h>

gboolean _log_verbose = FALSE;

void log_set_verbose(gboolean verbose) {
  _log_verbose = verbose;
}

void log_log(gchar * fmt, ...) {
  va_list args;
  FILE *f;
  
  if (_log_verbose) {
    time_t t;
    static char buf[64];
    static struct tm bdt;

    time(&t);
    localtime_r(&t, &bdt);
    strftime(buf, 63, "[%Y%m%d-%H%M%S] ", &bdt);
    f = fopen("/var/log/fritz2ci.log", "at");
    if (f != NULL) {
      fputs(buf, f);
      va_start(args, fmt);
      vfprintf(f, fmt, args);
      va_end(args);
      fclose(f);
    }
    else {
      fputs(buf, stdin);
      va_start(args, fmt);
      vprintf(fmt, args);
      va_end(args);
    }
  }
}
