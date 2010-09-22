#ifndef __LOOKUP_H__
#define __LOOKUP_H__

#include <glib.h>
#include "CIData.h"

gint lookup_init(gchar * lookup_sources, gchar * lookup_cache);
gint lookup_get_caller_data(CIDataSet * cidata);
void lookup_cleanup(void);

#endif