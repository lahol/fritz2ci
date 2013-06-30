#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <glib.h>

typedef struct _Fritz2CIConfig {
  gchar * fritz_host;
  gushort fritz_port;
  gushort ci2_port;
  gchar * db_host;
  gushort db_port;
  gchar * cache_location;
  gchar * lookup_sources_location;
  gchar * areacodes_location;
/*  gint loglevel;*/
  gboolean verbose;
  gboolean daemon;
  gchar * configfile;
  gchar *log_file;
  gchar *pid_file;
  gchar * msn_lookup_location;
  gchar * data_backup_location;
  guint lookup_source_id;
} Fritz2CIConfig;

gint parse_cmd_line(int * pargc, char *** pargv);
gint config_load(gchar * conffile);
void config_free(void);
const Fritz2CIConfig * config_get_config(void);

#endif
