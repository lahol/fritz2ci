#ifndef __MSN_LOOKUP_H__
#define __MSN_LOOKUP_H__

#include <glib.h>

gint msnl_read_file(gchar * file);
gint msnl_lookup(gchar * msn, gchar * alias);
void msnl_cleanup(void);

#endif