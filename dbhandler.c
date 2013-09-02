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
#include <sqlite3.h>
#include <time.h>

#define DBHANDLER_STMT_INSERT_CALL              0
#define DBHANDLER_STMT_GET_CALLER               1
#define DBHANDLER_STMT_GET_CALLS                2
#define DBHANDLER_STMT_GET_NUM_CALLS            3
#define DBHANDLER_STMT_NUM_STMTS                4

sqlite3 *dbhandler_db = NULL;
sqlite3_stmt *dbhandler_stmts[DBHANDLER_STMT_NUM_STMTS];

gulong parse_datetime(gchar *date, gchar *time);

gint dbhandler_init(gchar *db)
{
    int rc;
    char *sql;

    log_log("dbhandler_init: open\n");
    rc = sqlite3_open(db, &dbhandler_db);
    if (rc != 0) 
        goto out;

    sql = "create table if not exists cidata(id integer primary key, number varchar(31),\
           name varchar(255), timestamp integer, msn varchar(15), msn_alias varchar(20),\
           service varchar(20), fix varchar(20))";
    log_log("dbhandler_init: init cidata\n");
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        goto out;

    sql = "create table if not exists cicaller(clientid integer, number varchar(31), name varchar(255))";
    log_log("dbhandler_init: init cicaller\n");
    rc = sqlite3_exec(dbhandler_db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK)
        goto out;

#define PREPARE_STMT(stmt, id) do {\
    sql = (stmt);\
    rc = sqlite3_prepare_v2(dbhandler_db, sql, strlen(sql), &dbhandler_stmts[(id)], NULL);\
    log_log("dbhandler_init: init %s\n", #id);\
    if (rc != SQLITE_OK)\
        goto out;\
} while (0)

    PREPARE_STMT("insert into cidata (number, name, timestamp, msn, msn_alias, service, fix) values (?,?,?,?,?,?,?);",
            DBHANDLER_STMT_INSERT_CALL);
    PREPARE_STMT("select number, name from cicaller where number=? and clientid=?;",
            DBHANDLER_STMT_GET_CALLER);
    PREPARE_STMT("select number,name,timestamp,msn,msn_alias,service,fix,id from cidata order by timestamp desc limit ?,?;",
            DBHANDLER_STMT_GET_CALLS);
    PREPARE_STMT("select count(*) from cidata;",
            DBHANDLER_STMT_GET_NUM_CALLS);

#undef PREPARE_STMT

    log_log("dbhandler: initialized\n");
    return 0;

out:
    dbhandler_cleanup();
    log_log("error init dbhandler\n");
    return 1;
}

void dbhandler_cleanup(void)
{
    int i;
    for (i = 0; i < DBHANDLER_STMT_NUM_STMTS; ++i) {
        if (dbhandler_stmts[i] != NULL) {
            sqlite3_finalize(dbhandler_stmts[i]);
            dbhandler_stmts[i] = NULL;
        }
    }
    if (dbhandler_db) {
        sqlite3_close(dbhandler_db);
        dbhandler_db = NULL;
    }
}

gint dbhandler_add_data(CIDataSet *data)
{
    if (data == NULL)
        return 1;

    int rc;

#define BIND_TEXT(pos, field) do {\
    rc = sqlite3_bind_text(dbhandler_stmts[DBHANDLER_STMT_INSERT_CALL], (pos),\
            (field), strlen((field)), SQLITE_TRANSIENT);\
    if (rc != SQLITE_OK)\
        return 1;\
    } while (0)

    BIND_TEXT(1, data->cidsNumberComplete);
    BIND_TEXT(2, data->cidsName);
    rc = sqlite3_bind_int(dbhandler_stmts[DBHANDLER_STMT_INSERT_CALL], 3,
            parse_datetime(data->cidsDate, data->cidsTime));
    if (rc != SQLITE_OK)
        return 1;
    BIND_TEXT(4, data->cidsMSN);
    BIND_TEXT(5, data->cidsAlias);
    BIND_TEXT(6, data->cidsService);
    BIND_TEXT(7, data->cidsFix);
#undef BIND_TEXT

    rc = sqlite3_step(dbhandler_stmts[DBHANDLER_STMT_INSERT_CALL]);
    if (rc != SQLITE_OK) {
        log_log("dbhandler: insertcall failed (step: %d)\n", rc);
        return 1;
    }

    sqlite3_reset(dbhandler_stmts[DBHANDLER_STMT_INSERT_CALL]);
    return 0;
}

gulong dbhandler_get_num_calls(void)
{
    int rc;
    rc = sqlite3_step(dbhandler_stmts[DBHANDLER_STMT_GET_NUM_CALLS]);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        log_log("dbhandler_get_num_calls: rc = %d\n", rc);
        sqlite3_reset(dbhandler_stmts[DBHANDLER_STMT_GET_NUM_CALLS]);
        return 0;
    }

    gulong count = (gulong)sqlite3_column_int(dbhandler_stmts[DBHANDLER_STMT_GET_NUM_CALLS], 0);
    sqlite3_reset(dbhandler_stmts[DBHANDLER_STMT_GET_NUM_CALLS]);

    log_log("dbhandler_get_num_calls: count: %lu\n", count);
    return count;
}

/* return list of CIDbCall */
GList *dbhandler_get_calls(gint user, gint min_id, gint count)
{
    GList *list = NULL;
    CIDbCall *call;
    int rc;
    char *buf;
    gulong timestamp;

    sqlite3_bind_int(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 1, min_id);
    sqlite3_bind_int(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 2, count);

    while ((rc = sqlite3_step(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS])) == SQLITE_ROW) {
        call = g_malloc0(sizeof(CIDbCall));

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 0);
        if (buf)
            strncpy(call->data.cidsNumberComplete, buf, 31);

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 1);
        if (buf)
            strncpy(call->data.cidsName, buf, 255);

        timestamp = sqlite3_column_int(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 2);
        strftime(call->data.cidsDate, 16, "%Y-%m-%d", localtime((time_t*)&timestamp));
        strftime(call->data.cidsTime, 16, "%H:%M:%S", localtime((time_t*)&timestamp));

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 3);
        if (buf)
            strncpy(call->data.cidsMSN, buf, 15);

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 4);
        if (buf)
            strncpy(call->data.cidsAlias, buf, 255);

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 5);
        if (buf)
            strncpy(call->data.cidsService, buf, 255);

        buf = (char*)sqlite3_column_text(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 6);
        if (buf)
            strncpy(call->data.cidsFix, buf, 255);

        call->id = sqlite3_column_int(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS], 7);

        list = g_list_prepend(list, (gpointer)call);
    }

    sqlite3_reset(dbhandler_stmts[DBHANDLER_STMT_GET_CALLS]);

    if (rc != SQLITE_DONE) {
        g_list_free_full(list, g_free);
        return NULL;
    }

    /* get callers */

    return list;
}

gint dbhandler_get_caller(gint user, gchar *number, gchar *name)
{
    return 1;
}

gulong parse_datetime(gchar *date, gchar *time)
{
    struct tm t;
    gchar buf[64];
    memset(&t, 0, sizeof(struct tm));

    int j, i;

    if (date) {
        j = 0; i = 0;
        while (date[j] != '-') { buf[i++] = date[j++]; }
        buf[i] = '\0'; j++;
        t.tm_year = atoi(buf)-1900;
        i = 0;
        while (date[j] != '-') { buf[i++] = date[j++]; }
        buf[i] = '\0'; j++;
        t.tm_mon = atoi(buf)-1;
        i = 0;
        while (date[j] != '\0') { buf[i++] = date[j++]; }
        buf[i] = '\0';
        t.tm_mday = atoi(buf);
    }

    if (time) {
        j = 0; i = 0;
        while (time[j] != ':') { buf[i++] = time[j++]; }
        buf[i] = '\0'; j++;
        t.tm_hour = atoi(buf);
        i = 0;
        while (time[j] != ':') { buf[i++] = time[j++]; }
        buf[i] = '\0'; j++;
        t.tm_min = atoi(buf);
        i = 0;
        while (time[j] != '\0') { buf[i++] = time[j++]; }
        buf[i] = '\0';
        t.tm_sec = atoi(buf);
    }

    t.tm_isdst = -1; /* no information available */
    return (gulong)mktime(&t);
}
