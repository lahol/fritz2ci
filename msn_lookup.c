#include "msn_lookup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include "logging.h"

#define MSNL_MSN_SIZE          32
#define MSNL_ALIAS_SIZE        256

typedef struct _MSN_Lookup {
  gchar msn[MSNL_MSN_SIZE];
  gchar alias[MSNL_ALIAS_SIZE];
} MSN_Lookup;

GSList * _msn_list = NULL;
void _read_line(gchar * buffer, MSN_Lookup * data);

gint msnl_read_file(gchar * file) {
  gchar buffer[512];
  FILE * f;
  MSN_Lookup * data;
  if ((f = fopen(file, "r")) == NULL) {
    return 1;
  }
  
  while (!feof(f)) {
    if (fgets(buffer, 512, f)) {
      data = g_malloc0(sizeof(MSN_Lookup));
      _read_line(buffer, data);
      _msn_list = g_slist_prepend(_msn_list, (gpointer)data);
    }
  }
  fclose(f);
  _msn_list = g_slist_reverse(_msn_list);
  return 0;
}

gint msnl_lookup(gchar * msn, gchar * alias) {
  GSList * tmp = _msn_list;
  if (!msn) {
    return 1;
  }
  while (tmp) {
    if (tmp->data) {
      if (strcmp(((MSN_Lookup*)tmp->data)->msn, msn) == 0) {
        if (alias) {
          strcpy(alias, ((MSN_Lookup*)tmp->data)->alias);
        }
        return 0;
      }
    }
    tmp = g_slist_next(tmp);
  }
  return 1;
}

void msnl_cleanup(void) {
  while (_msn_list) {
    g_free((MSN_Lookup*)_msn_list->data);
    _msn_list = g_slist_remove(_msn_list, _msn_list->data);
  }
}

void _read_line(gchar * buffer, MSN_Lookup * data) {
  log_log("read_line: %s\n", buffer);
  gint i, j;
  if (!data) {
    return;
  }
  data->msn[0] = '\0';
  data->alias[0] = '\0';
  if (!buffer) {
    return;
  }
  i = 0;
  j = 0;
  while (buffer[i] == ' ' || buffer[i] == '\t') { i++; }
  while (buffer[i] != ' ' && buffer[i] != '\t' && buffer[i] != '\0' && buffer[i] != '\n' && j < MSNL_MSN_SIZE) {
    data->msn[j] = buffer[i];
    i++; j++;
  }
  data->msn[j] = '\0';
  j = 0;
  while (buffer[i] == ' ' || buffer[i] == '\t') { i++; }
  while (buffer[i] != '\0' && buffer[i] != '\n' && j < MSNL_ALIAS_SIZE) {
    data->alias[j] = buffer[i];
    i++; j++;
  }
  data->alias[j] = '\0';
  log_log("parsed: (%s)=(%s)\n", data->msn, data->alias);
}
