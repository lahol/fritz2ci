#ifndef __FRITZ_H__
#define __FRITZ_H__

#include <glib.h>

#define CALLMSGTYPE_CALL                0
#define CALLMSGTYPE_RING                1
#define CALLMSGTYPE_CONNECT             2
#define CALLMSGTYPE_DISCONNECT          3

typedef struct _CIFritzCallMsg {
  gushort msgtype;
  gushort connectionid;
  gushort nst;
  char calling_number[32];
  char called_number[32];
  char number[32];
  gulong duration;
  struct tm datetime;
} CIFritzCallMsg;

gint fritz_init(gchar * host, gushort port);
gint fritz_connect(void);
gint fritz_listen(void (*fritz_listen_cb)(CIFritzCallMsg *));
gint fritz_disconnect(void);
gint fritz_cleanup(void);
void fritz_init_reconnect(void);

#endif
