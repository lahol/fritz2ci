#ifndef __CI2SERVER_H__
#define __CI2SERVER_H__

#include <glib.h>
#include "CIData.h"

typedef enum {
    CIServerMsgMessage,
    CIServerMsgUpdate,
    CIServerMsgDisconnect,
    CIServerMsgComplete,
    CIServerMsgCall
} CIServerMsg;

gint cisrv_init(void);
gint cisrv_run(gushort port);
gint cisrv_broadcast_message(CIServerMsg msgtype, CIDataSet *data, gchar *msgid);
gint cisrv_disconnect(void);
gint cisrv_cleanup(void);

#endif
