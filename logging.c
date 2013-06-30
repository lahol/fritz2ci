#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdarg.h>

gboolean _log_verbose = FALSE;
gchar *_log_file = NULL;

void log_set_verbose(gboolean verbose) {
  _log_verbose = verbose;
}

void log_set_log_file(gchar *file)
{
    _log_file = file;
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
    if (_log_file && (f = fopen(_log_file, "at")) != NULL) {
      fputs(buf, f);
      va_start(args, fmt);
      vfprintf(f, fmt, args);
      va_end(args);
      fclose(f);
    }
    else {
      fputs(buf, stdout);
      va_start(args, fmt);
      vprintf(fmt, args);
      va_end(args);
    }
  }
}
