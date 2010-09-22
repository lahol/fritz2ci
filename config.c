#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include "logging.h"

static Fritz2CIConfig _config;

gint config_load(gchar * conffile) {
  GKeyFile * kf = g_key_file_new();
  if (!conffile || !g_key_file_load_from_file(kf, conffile, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
    log_log("could not read config file, setting defaults\n");
    _config.fritz_host = g_strdup("127.0.0.1");
    _config.fritz_port = 1012;
    _config.ci2_port = 63690;
    _config.db_host = g_strdup("127.0.0.1");
    _config.db_port = 63691;
    _config.areacodes_location = g_strdup("/usr/share/fritz2ci/vorwahl.dat");
    _config.cache_location = g_strdup("cache.db");
    _config.lookup_sources_location = g_strdup("/usr/share/fritz2ci/revlookup.xml");;
    _config.msn_lookup_location = g_strdup("/usr/share/fritz2ci/msn.dat");
    _config.data_backup_location = g_strdup("cidata.dat");
    _config.lookup_source_id = 1;
  }
  else {
    log_log("Reading config file %s\n", conffile);
    _config.fritz_host = g_key_file_get_string(kf, "Fritz", "Host", NULL);
    _config.fritz_port = (gushort)g_key_file_get_integer(kf, "Fritz", "Port", NULL);
    _config.ci2_port = (gushort)g_key_file_get_integer(kf, "CIServer", "Port", NULL);
    _config.db_host = g_key_file_get_string(kf, "Database", "Host", NULL);
    _config.db_port = (gushort)g_key_file_get_integer(kf, "Database", "Port", NULL);
    _config.cache_location = g_key_file_get_string(kf, "Cache", "Location", NULL);
    _config.lookup_sources_location = g_key_file_get_string(kf, "Lookup", "Location", NULL);
    _config.areacodes_location = g_key_file_get_string(kf, "Areacodes", "Location", NULL);
    _config.msn_lookup_location = g_key_file_get_string(kf, "Lookup", "MSNFile", NULL);
    _config.data_backup_location = g_key_file_get_string(kf, "Database", "Backupfile", NULL);
    _config.lookup_source_id = g_key_file_get_integer(kf, "Lookup", "Source", NULL);
    g_key_file_free(kf);
  }
  
  return 0;
}

void config_free(void) {
  g_free(_config.fritz_host);
  g_free(_config.db_host);
  g_free(_config.cache_location);
  g_free(_config.lookup_sources_location);
  g_free(_config.areacodes_location);
  g_free(_config.configfile);
  g_free(_config.msn_lookup_location);
  g_free(_config.data_backup_location);
}

const Fritz2CIConfig * config_get_config(void) {
  return &_config;
}

static GOptionEntry _cmd_line_options[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &_config.verbose, "Be verbose", NULL },
  { "daemon", 'd', 0, G_OPTION_ARG_NONE, &_config.daemon, "Start as daemon", NULL },
  { NULL }
};

gint parse_cmd_line(int * pargc, char *** pargv) {
  GOptionContext * context;
  memset(&_config, 0, sizeof(Fritz2CIConfig));
  
  context = g_option_context_new("[configfile] - provide server for ci2 protocol, translate from fritz");
  g_option_context_add_main_entries(context, _cmd_line_options, "fritz2cid");
  if (!g_option_context_parse(context, pargc, pargv, NULL)) {
    g_option_context_free(context);
    return 1;
  }  
  g_option_context_free(context);
  if ((*pargc) > 1) {
    _config.configfile = g_strdup((*pargv)[1]);
  }
 
  return 0;
}