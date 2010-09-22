#ifndef __DBHANDLER_H__
#define __DBHANDLER_H__

#include <glib.h>
#include "CIData.h"

gint dbhandler_init(void);
gint dbhandler_connect(gchar * host, gushort port);
gint dbhandler_add_data(CIDataSet * data);
void dbhandler_disconnect(void);
void dbhandler_cleanup(void);

#endif