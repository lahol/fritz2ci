#include <glib.h>
#include <glib-object.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>
#include "CIData.h"
#include "config.h"
#include "ci-server.h"
#include "fritz.h"
#include "dbhandler.h"
#include "lookup.h"
#include "ci_areacodes.h"
#include "msn_lookup.h"
#include "logging.h"
#include "daemon.h"
#include <sys/time.h>

void _shutdown(void);
void _handle_signal(int signum);

void handle_fritz_message(CIFritzCallMsg *cmsg);
void backup_data_write(CIDataSet *set);

GMainLoop *mainloop = NULL;
/*GMainContext * context = NULL;*/
GQueue *_db_data_todo = NULL;
static GMutex _db_data_queue_lock;

int main(int argc, char **argv)
{
    pid_t daemon_pid;
    struct sigaction _sgn;

#if !GLIB_CHECK_VERSION(2,36,0)
    g_type_init();
#endif

    if (parse_cmd_line(&argc, &argv)) {
        fprintf(stderr, "Could not parse command line\n");
        return 1;
    }

    const Fritz2CIConfig *cfg = config_get_config();
    log_set_verbose(cfg->verbose);
    if (config_load(cfg->configfile) != 0) {
        fprintf(stderr, "Could not read configuration\n");
        return 1;
    }
    log_set_log_file(cfg->log_file);
    log_log("loaded configuration\n");

    if (cfg->daemon) {
        daemon_pid = start_daemon(cfg->pid_file);
        if (daemon_pid == -1) {
            log_log("Could not start daemon\n");
            return 1;
        }
        if (daemon_pid) {
            log_log("Starting as daemon. Process id is %d\n", daemon_pid);
            config_free();
            return 0;
        }
    }

    _db_data_todo = g_queue_new();

    if (fritz_init((gchar *)cfg->fritz_host, cfg->fritz_port) != 0) {
        log_log("Could not initialize fritz\n");
        _shutdown();
        return 1;
    }
    else {
        log_log("initialized fritz\n");
    }
    if (cisrv_init() != 0) {
        log_log("Could not initialize ci-server\n");
        _shutdown();
        return 1;
    }
    log_log("initialized cisrv\n");

    if (dbhandler_init(cfg->db_location) != 0) {
        log_log("Could not initialize dbhandler\n");
    }
    else {
        log_log("initialized dbhandler\n");
    }
    if (lookup_init(cfg->lookup_sources_location, cfg->cache_location) != 0) {
        log_log("Could not initialize lookup\n");
        _shutdown();
        return 1;
    }
    log_log("initialized lookup\n");

    if (msnl_read_file(cfg->msn_lookup_location) != 0) {
        log_log("Could not open msn lookup file\n");
/*        _shutdown();
        return 1;*/
    }
    else
        log_log("loaded msn lookup file\n");

    /*connect*/
    if (fritz_connect(NULL) == 0) {
        log_log("connected fritz\n");
    }

    if (cisrv_run(cfg->ci2_port) != 0) {
        log_log("call shutdown after failed cisrv_run\n");
        _shutdown();
        return 1;
    }
    log_log("started ci srv\n");

    if (fritz_listen(handle_fritz_message) != 0) {
        log_log("failed to listen (fritz)\n");
        return 1;
    }
    else {
        log_log("listening to fritz\n");
    }

    ci_init_area_codes();
    if (ci_read_area_codes_from_file(cfg->areacodes_location) != 0) {
        log_log("call shutdown after failed ci_read_area_codes_from_file\n");
        _shutdown();
        return 1;
    }
    log_log("initialized area codes\n");

    /*mainloop*/
    mainloop = g_main_loop_new(NULL, FALSE);

    memset(&_sgn, 0, sizeof(struct sigaction));
    _sgn.sa_handler = _handle_signal;
    sigaction(SIGINT, &_sgn, NULL);
    sigaction(SIGTERM, &_sgn, NULL);

    g_main_loop_run(mainloop);
    log_log("terminating, call shutdown\n");
    _shutdown();
    return 0;
}

void _shutdown(void)
{
    ci_free_area_codes();
    fritz_disconnect();
    cisrv_disconnect();

    msnl_cleanup();
    fritz_cleanup();
    dbhandler_cleanup();
    cisrv_cleanup();
    lookup_cleanup();
    int cnt = 0;
    CIDataSet *set;
    while ((set = g_queue_pop_head(_db_data_todo)) != NULL) {
        g_free((CIDataSet *)set);
        ++cnt;
    }
    g_queue_free(_db_data_todo);
    if (cnt) {
        log_log("There were %d sets not written to database\n", cnt);
    }

    config_free();

    stop_daemon();
}

void _handle_signal(int signum)
{
    log_log("quit main loop\n");
    g_main_loop_quit(mainloop);
}

void generate_msg_id(gchar *id)
{
    if (id == NULL)
        return;
    struct timeval tv;
    struct tm *tm;

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);

    strftime(id, 16, "%y%m%d%H%M%S", tm);
    snprintf(&id[12], 4, "%03d", (int)(((long)tv.tv_usec)/1000)%1000);
}

void handle_fritz_message(CIFritzCallMsg *cmsg)
{
    log_log("handle_fritz_message\n");
    int rc;
    int dbrc;
    if (!cmsg) {
        return;
    }
    if (cmsg->msgtype != CALLMSGTYPE_CALL && cmsg->msgtype != CALLMSGTYPE_RING) {
        return;
    }

    gchar msgid[16];

    CIDataSet set;
    CIDataSet *todo = NULL;
    memset(&set, 0, sizeof(CIDataSet));

    if (cmsg->msgtype == CALLMSGTYPE_RING) {
        generate_msg_id(msgid);
        log_log("generate msg id: %s\n", msgid);

        strftime(set.cidsTime, 16, "%H:%M:%S", &cmsg->datetime);
        strcpy(set.cidsNumberComplete, cmsg->calling_number);
        strcpy(set.cidsMSN, cmsg->called_number);
        strcpy(set.cidsService, "Telefonie");
        strcpy(set.cidsFix, "Fix");
        ci_get_area_code(set.cidsNumberComplete, set.cidsAreaCode, set.cidsNumber, set.cidsArea);
        strcpy(set.cidsName, set.cidsArea);
        msnl_lookup(set.cidsMSN, set.cidsAlias);
        strftime(set.cidsDate, 16, "%d.%m.%Y", &cmsg->datetime);
        backup_data_write(&set);
        strftime(set.cidsDate, 16, "%Y-%m-%d", &cmsg->datetime);
        cisrv_broadcast_message(CIServerMsgMessage, &set, msgid);

        rc = lookup_get_caller_data(&set);
        g_mutex_lock(&_db_data_queue_lock);
        if (g_queue_is_empty(_db_data_todo)) {
            if ((dbrc = dbhandler_add_data(&set)) != 0) { /* send or receive failed, init reconnect */
                log_log("add data failed\n");
                todo = g_malloc0(sizeof(CIDataSet));
                memcpy(todo, &set, sizeof(CIDataSet));
                g_queue_push_tail(_db_data_todo, (gpointer)todo);
            }
        }
        else {
            todo = g_malloc0(sizeof(CIDataSet));
            memcpy(todo, &set, sizeof(CIDataSet));
            g_queue_push_tail(_db_data_todo, (gpointer)todo);
        }
        g_mutex_unlock(&_db_data_queue_lock);

        if (rc == 0) {
            cisrv_broadcast_message(CIServerMsgUpdate, &set, msgid);
        }
        cisrv_broadcast_message(CIServerMsgComplete, &set, msgid);
    }
    else if (cmsg->msgtype == CALLMSGTYPE_CALL) {
        strftime(set.cidsTime, 16, "%H:%M:%S", &cmsg->datetime);
        strftime(set.cidsDate, 16, "%Y-%m-%d", &cmsg->datetime);
        strcpy(set.cidsNumberComplete, cmsg->called_number);
        strcpy(set.cidsMSN, cmsg->calling_number);
        msnl_lookup(set.cidsMSN, set.cidsAlias);

        cisrv_broadcast_message(CIServerMsgCall, &set, NULL);
    }
}

void backup_data_write(CIDataSet *set)
{
    FILE *f;
    const Fritz2CIConfig *cfg = config_get_config();
    if (!cfg->data_backup_location) {
        log_log("No backup file specified\n");
        return;
    }
    if ((f = fopen(cfg->data_backup_location, "a")) == NULL) {
        log_log("Error opening backup file `%s'\n", cfg->data_backup_location);
        return;
    }
    fprintf(f, "%s§\"%s\"§\"%s\"§\"%s\"§%s§\"%s\"§\"%s\"§%s\n", set->cidsNumberComplete,
            set->cidsName, set->cidsDate, set->cidsTime, set->cidsMSN, set->cidsAlias,
            set->cidsService, set->cidsFix);
    fclose(f);
    log_log("written backup\n");
}
