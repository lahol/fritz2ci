#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>
#include "CIData.h"
#include "config.h"
#include "ci2server.h"
#include "fritz.h"
#include "dbhandler.h"
#include "lookup.h"
#include "ci_areacodes.h"
#include "msn_lookup.h"
#include "logging.h"

void _shutdown(void);
void _handle_signal(int signum);

void handle_fritz_message(CIFritzCallMsg * cmsg);
void backup_data_write(CIDataSet * set);

typedef struct _DBInfo {
  gchar * host;
  gushort port;
} DBInfo;

gboolean _db_reconnect_cb(DBInfo * db);
void db_try_reconnect(gchar * host, gushort port);

GMainLoop *mainloop = NULL;
/*GMainContext * context = NULL;*/
GQueue * _db_data_todo = NULL;
GStaticMutex _db_data_queue_lock = G_STATIC_MUTEX_INIT;

int main(int argc, char ** argv) {
  pid_t daemon_pid;
  struct sigaction _sgn;
#if GLIB_MAJOR_VERSION >= 2 && GLIB_MINOR_VERSION >= 20
  if (!g_thread_get_initialized()) {
#else
  if (!g_thread_supported()) {
#endif
    g_thread_init(NULL);
  }
/*  context = g_main_context_default();
  g_main_context_ref(context);*/
  if (parse_cmd_line(&argc, &argv)) {
    fprintf(stderr, "Could not parse command line\n");
    return 1;
  }
  const Fritz2CIConfig * cfg = config_get_config();
  if (cfg->daemon) {
    daemon_pid = fork();
    if (daemon_pid != -1 && daemon_pid) {
      config_free();
      printf("Starting as daemon. Process id is %d\n", daemon_pid);
      return 0;
    }
  }
  log_set_verbose(cfg->verbose);
  if (config_load(cfg->configfile) != 0) {
    fprintf(stderr, "Could not read configuration\n");
    return 1;
  }
  log_log("loaded configuration\n");
  
  _db_data_todo = g_queue_new();
  
  if (fritz_init(/*context*/) != 0) {
    /*config_free();*/
    fprintf(stderr, "Could not initialize fritz\n");
    /*return 1;*/
  }
  else {
    log_log("initialized fritz\n");
  }
  if (cisrv_init() != 0) {
    config_free();
    fprintf(stderr, "Could not initialize ci2server\n");
    return 1;
  }
  log_log("initialized cisrv\n");
  
  if (dbhandler_init() != 0) {
/*    config_free();*/
    fprintf(stderr, "Could not initialize dbhandler\n");
/*    return 1;*/
  }
  else {
    log_log("initialized dbhandler\n");
  }
  if (lookup_init(cfg->lookup_sources_location, cfg->cache_location) != 0) {
    config_free();
    fprintf(stderr, "Could not initialize lookup\n");
    return 1;
  }
  log_log("initialized lookup\n");
  
  if (msnl_read_file(cfg->msn_lookup_location) != 0) {
    config_free();
    fprintf(stderr, "Could not open msn lookup file\n");
  }
  log_log("loaded msn lookup file\n");
  
  /*connect*/
  if (fritz_connect((gchar*)cfg->fritz_host, cfg->fritz_port) != 0) {
    /*_shutdown();*/
/*    return 1;*/
  }
  else {
    log_log("connected fritz\n");
  }
  
  if (cisrv_run(cfg->ci2_port) != 0) {
    _shutdown();
    return 1;
  }
  log_log("started ci srv\n");
  
  if (dbhandler_connect(cfg->db_host, cfg->db_port) != 0) {
    /*_shutdown();*/
    db_try_reconnect(cfg->db_host, cfg->db_port);
/*    return 1;*/
  }
  else {
    log_log("started dbhandler\n");
  }
  
  if (fritz_listen(handle_fritz_message) != 0) {
    fritz_init_reconnect();
/*    _shutdown();
    return 1;*/
  }
  else {
    log_log("listening to fritz\n");
  }
  
  ci_init_area_codes();
  if (ci_read_area_codes_from_file(cfg->areacodes_location) != 0) {
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
/*signal(SIGINT, (sig_t)_handle_signal);
  signal(SIGTERM, (sig_t)_handle_signal);*/
  
  g_main_loop_run(mainloop);
  /*shutdown*/
/*  g_main_context_unref(context);*/
  _shutdown();
  return 0;
}

void _shutdown(void) {
  log_log("shutdown\n");
  ci_free_area_codes();
  fritz_disconnect();
  dbhandler_disconnect();
  cisrv_disconnect();

  msnl_cleanup();
  fritz_cleanup();
  dbhandler_cleanup();
  cisrv_cleanup();
  lookup_cleanup();
  config_free();
  int cnt = 0;
  CIDataSet * set;
  while ((set = g_queue_pop_head(_db_data_todo)) != NULL) {
    g_free((CIDataSet*)set);
    cnt++;
  }
  g_queue_free(_db_data_todo);
  if (cnt) {
    log_log("There were %d sets not written to database\n", cnt);
  }
}

void _handle_signal(int signum) {
  log_log("quit main loop\n");
  g_main_loop_quit(mainloop);
}

void handle_fritz_message(CIFritzCallMsg * cmsg) {
  log_log("handle_fritz_message\n");
  int rc;
  int dbrc;
  if (!cmsg) {
    return;
  }
  if (/*cmsg->msgtype != CALLMSGTYPE_CALL &&*/ cmsg->msgtype != CALLMSGTYPE_RING) {
    return;
  }
  CIDataSet set;
  CIDataSet * todo = NULL;
  memset(&set, 0, sizeof(CIDataSet));
  if (cmsg->msgtype == CALLMSGTYPE_RING) {
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
    cisrv_broadcast_message(CI2ServerMsgMessage, &set);
    rc = lookup_get_caller_data(&set);
    g_static_mutex_lock(&_db_data_queue_lock);
    if (g_queue_is_empty(_db_data_todo)) {
      if ((dbrc = dbhandler_add_data(&set)) != 0) { /* send or receive failed, init reconnect */
        const Fritz2CIConfig * cfg = config_get_config();
        todo = g_malloc0(sizeof(CIDataSet));
        memcpy(todo, &set, sizeof(CIDataSet));
        g_queue_push_tail(_db_data_todo, (gpointer)todo);
        db_try_reconnect(cfg->db_host, cfg->db_port);
      }
    }
    else {
      todo = g_malloc0(sizeof(CIDataSet));
      memcpy(todo, &set, sizeof(CIDataSet));
      g_queue_push_tail(_db_data_todo, (gpointer)todo);
    }
    g_static_mutex_unlock(&_db_data_queue_lock);
    if (rc == 0) {
      cisrv_broadcast_message(CI2ServerMsgUpdate, &set);
    }
  }
}

void backup_data_write(CIDataSet * set) {
  FILE * f;
  const Fritz2CIConfig * cfg = config_get_config();
  if (!cfg->data_backup_location) {
    log_log("No backup file specified\n");
    return;
  }
  if ((f = fopen(cfg->data_backup_location, "a")) == NULL) {
    log_log("Error opening backup file\n");
    return;
  }
  fprintf(f, "%s§\"%s\"§\"%s\"§\"%s\"§%s§\"%s\"§\"%s\"§%s\n", set->cidsNumberComplete,
    set->cidsName, set->cidsDate, set->cidsTime, set->cidsMSN, set->cidsAlias,
    set->cidsService, set->cidsFix);
  fclose(f);
  log_log("written backup\n");
}

gboolean _db_reconnect_cb(DBInfo * db) {
  log_log("db: trying to reconnect\n");
  CIDataSet * data;
  dbhandler_disconnect();
  dbhandler_cleanup();
  int rc = 0;
  if (dbhandler_init() != 0) {
    return TRUE;
  }
  if (dbhandler_connect(db->host, db->port) != 0) {
    return TRUE;
  }
  g_static_mutex_lock(&_db_data_queue_lock);
  while ((data = (CIDataSet*)g_queue_peek_head(_db_data_todo)) != NULL) {
    rc = dbhandler_add_data(data);
    if (rc) {
      break;
    }
    g_queue_pop_head(_db_data_todo);
    g_free((CIDataSet*)data);
  }
  g_static_mutex_unlock(&_db_data_queue_lock);
  if (rc) { 
    return TRUE;
  }
  g_free(db->host);
  g_free(db);
  return FALSE;
}

void db_try_reconnect(gchar * host, gushort port) {
  DBInfo * db = g_malloc0(sizeof(DBInfo));
  db->host = g_strdup(host);
  db->port = port;
  
  g_timeout_add_seconds(2, (GSourceFunc)_db_reconnect_cb, (gpointer)db);
}
