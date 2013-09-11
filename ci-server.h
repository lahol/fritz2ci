#ifndef __CI2SERVER_H__
#define __CI2SERVER_H__

#include <glib.h>
#include "CIData.h"

typedef enum {
    CI2ServerMsgMessage,
    CI2ServerMsgUpdate,
    CI2ServerMsgDisconnect,
    CI2ServerMsgComplete
} CI2ServerMsg;

gint cisrv_init(void);
gint cisrv_run(gushort port);
gint cisrv_broadcast_message(CI2ServerMsg msgtype, CIDataSet *data, gchar *msgid);
gint cisrv_disconnect(void);
gint cisrv_cleanup(void);

#endif