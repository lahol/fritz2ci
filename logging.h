#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <glib.h>

void log_set_verbose(gboolean verbose);
void log_set_log_file(gchar *file);
void log_log(gchar * fmt, ...);    

#endif
