#ifndef __DBHANDLER_H__
#define __DBHANDLER_H__

#include <glib.h>
#include "CIData.h"

typedef struct {
    gint id;
    CIDataSet data;
} CIDbCall;

gint dbhandler_init(gchar *db);

gint dbhandler_add_data(CIDataSet *data);
gulong dbhandler_get_num_calls(void);
GList *dbhandler_get_calls(gint user, gint min_id, gint count);
gint dbhandler_get_caller(gint user, gchar *number, gchar *name);

void dbhandler_cleanup(void);

#endif
