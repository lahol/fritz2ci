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
  
  if (_log_verbose) {
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
}