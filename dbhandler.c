#include "dbhandler.h"
#include "cidbmessages.h"
#include "cidbconnection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/types.h>
#include <unistd.h>
/*#include <pthread.h>*/
#include "netutils.h"
#include "logging.h"

/*typedef struct _DBHandlerData {
  gchar * host;
  gushort port;
} DBHandlerData;

gboolean _dbhandler_try_reconnect(DBHandlerData * data);*/

enum CIDBHandlerState {
    CIDBHandlerStateUninitialized = 0,
    CIDBHandlerStateInitialized,
    CIDBHandlerStateConnected
};

int _db_sock = -1;
gushort _db_state = CIDBHandlerStateUninitialized;
/*DBHandlerData _dbhandler_data;*/

gint dbhandler_init(void)
{
    /*  memset(&_dbhandler_data, 0, sizeof(DBHandlerData));*/
    _db_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (_db_sock == -1) {
        return 1;
    }
    _db_state = CIDBHandlerStateInitialized;
    return 0;
}

gint dbhandler_connect(gchar *host, gushort port)
{
    struct sockaddr_in srv;
    /*  _dbhandler_data.host = g_strdup(host);
      _dbhandler_data.port = port;*/
    if (inet_aton(host, &srv.sin_addr) == 0) {
        return 1;
    }
    srv.sin_port = htons(port);
    srv.sin_family = AF_INET;

    if (connect(_db_sock, (struct sockaddr *)&srv, sizeof(srv)) == -1) {
        log_log("connect failed\n");
        return 2;
    }
    _db_state = CIDBHandlerStateConnected;
    log_log("db connected\n");
    return 0;
}

gint dbhandler_add_data(CIDataSet *data)
{
    log_log("dbhandler_add_data: %p, state: %d\n", data, _db_state);
    if (_db_state != CIDBHandlerStateConnected) {
        return 1;
    }
    CIDBMessage dbmsg_out;
    CIDBMessage dbmsg_in;
    memset(&dbmsg_out, 0, sizeof(CIDBMessage));
    memset(&dbmsg_in, 0, sizeof(CIDBMessage));

    dbmsg_out.cmd = CIDBMSG_CMD_WRITE_CALL;
    dbmsg_out.subcmd = CIDBMSG_SUBCMD_QUERY;

    dbmsg_out.table.ncols = 8;
    dbmsg_out.table.nrows = 1;
    dbmsg_out.table.column_names = malloc(sizeof(unsigned char *)*8);
    dbmsg_out.table.fields = malloc(sizeof(unsigned char *)*8);
    dbmsg_out.table.column_names[0] = (unsigned char *)strdup("number");
    dbmsg_out.table.column_names[1] = (unsigned char *)strdup("name");
    dbmsg_out.table.column_names[2] = (unsigned char *)strdup("msn");
    dbmsg_out.table.column_names[3] = (unsigned char *)strdup("msn_alias");
    dbmsg_out.table.column_names[4] = (unsigned char *)strdup("service");
    dbmsg_out.table.column_names[5] = (unsigned char *)strdup("fix");
    dbmsg_out.table.column_names[6] = (unsigned char *)strdup("date");
    dbmsg_out.table.column_names[7] = (unsigned char *)strdup("time");

    dbmsg_out.table.fields[0] = (unsigned char *)strdup(data->cidsNumberComplete);
    dbmsg_out.table.fields[1] = (unsigned char *)strdup(data->cidsName);
    dbmsg_out.table.fields[2] = (unsigned char *)strdup(data->cidsMSN);
    dbmsg_out.table.fields[3] = (unsigned char *)strdup(data->cidsAlias);
    dbmsg_out.table.fields[4] = (unsigned char *)strdup(data->cidsService);
    dbmsg_out.table.fields[5] = (unsigned char *)strdup(data->cidsFix);
    dbmsg_out.table.fields[6] = (unsigned char *)strdup(data->cidsDate);
    dbmsg_out.table.fields[7] = (unsigned char *)strdup(data->cidsTime);

    log_log("sending message\n");

    if (cidbcon_send_message(_db_sock, &dbmsg_out)) {
        fprintf(stderr, "failed sending message\n");
        return 3;
    }

    log_log("sent message\n");
    if (cidbcon_recv_message(_db_sock, &dbmsg_in)) {
        fprintf(stderr, "failed receiving message\n");
        return 4;
    }
    log_log("received: err: %d\n", dbmsg_in.errcode);

    cidbmsg_table_free(&dbmsg_out.table);
    return 0;
}

void dbhandler_disconnect(void)
{
    close(_db_sock);
    _db_state = CIDBHandlerStateInitialized;
}

void dbhandler_cleanup(void)
{
    _db_sock = -1;
    _db_state = CIDBHandlerStateUninitialized;
}
