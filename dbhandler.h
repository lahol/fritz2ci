#ifndef __DBHANDLER_H__
#define __DBHANDLER_H__

#include <glib.h>
#include "CIData.h"

typedef struct {
    gint id;
    CIDataSet data;
} CIDbCall;

typedef struct {
    gchar *number;
    gchar *name;
} CIDbCaller;

gint dbhandler_init(gchar *db);

gint dbhandler_add_data(CIDataSet *data);
gulong dbhandler_get_num_calls(void);
GList *dbhandler_get_calls(gint user, gint offset, gint count);
gint dbhandler_get_caller(gint user, gchar *number, gchar *name);
gint dbhandler_add_caller(gint user, gchar *number, gchar *name);
gint dbhandler_remove_caller(gint user, gchar *number, gchar *name);
GList *dbhandler_get_callers(gint user, gchar *filter);

void dbhandler_cleanup(void);

#endif
