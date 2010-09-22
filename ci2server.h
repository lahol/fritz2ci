#ifndef __CI2SERVER_H__
#define __CI2SERVER_H__

#include <glib.h>
#include "CIData.h"

typedef enum {
  CI2ServerMsgMessage,
  CI2ServerMsgUpdate,
  CI2ServerMsgDisconnect
} CI2ServerMsg;

gint cisrv_init(void);
gint cisrv_run(gushort port);
gint cisrv_broadcast_message(CI2ServerMsg msgtype, CIDataSet * data);
gint cisrv_disconnect(void);
gint cisrv_cleanup(void);

#endif