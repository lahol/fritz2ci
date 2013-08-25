#ifndef __CIDBCONNECTION_H__
#define __CIDBCONNECTION_H__

#include "cidbmessages.h"

int cidbcon_send_message(int sock, CIDBMessage *dbmsg);
int cidbcon_recv_message(int sock, CIDBMessage *dbmsg);

#endif