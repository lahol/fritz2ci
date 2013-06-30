/* $Id: cidatabase.c 31 2010-08-23 09:22:22Z lahol $
 * $Rev: 31 $
 * $Date: 2010-08-23 11:22:22 +0200 (Mo, 23. Aug 2010) $
 * $Author: lahol $
 */
/** @ingroup revlookup
 *  @file
 *  @brief System to lookup a number in a database
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <curl/curl.h>


#include "lookup.h"
#include "logging.h"
#include "config.h"

gint cidb_init(void);
gint cidb_connect(gchar * dbpath);
void cidb_cleanup(void);

gint cidb_find_caller(CICaller * caller);
/* cache: write data to cache*/
gint cidb_insert_caller(CICaller * caller);

void cidb_clear_cache(void);

gint cirlw_init(void);
void cirlw_cleanup(void);
gint cirlw_load_sources_from_file(const gchar * filename);

gint cirlw_get_caller(gulong sourceid, CICaller * caller);


/*global db-handle*/
sqlite3 * _cidb_db = NULL;      /**< global handle to the database connection */
/*prepared statements*/
sqlite3_stmt * _cidb_find_number_cache_stmt = NULL; /**< prepared statement to find a number in the cache */

/** @brief Initialize the database reverse lookup system
 * 
 *  currently there is noting to initialize
 *  @return 0 on success
 */
gint cidb_init(void) {
  return 0;
}

/** @brief Connect to the database
 *  @param dbpath path to the database file
 *  @return 0 on success, 1 if there was an error
 */
gint cidb_connect(gchar * dbpath) {
  int rc;
  char * sql;
  
  rc = sqlite3_open(dbpath, &_cidb_db);
  if (rc) {
    sqlite3_close(_cidb_db);
    return 1;
  }
  
  /* callercache table */
  sql = "create table if not exists callercache(id integer primary key, number text, name text, city text, street text, postalcode text, comment text, category int);";
  rc = sqlite3_exec(_cidb_db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(_cidb_db);
    return 1;
  }
  
  sql = "select name, city, street, postalcode from callercache where number=? limit 1;";
  rc = sqlite3_prepare_v2(_cidb_db, sql, strlen(sql), &_cidb_find_number_cache_stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(_cidb_db);
    _cidb_find_number_cache_stmt = NULL;
    return 1;
  }
  return 0;
}

/** @brief cleanup the database reverse lookup system
 */
void cidb_cleanup(void) {
  if (_cidb_find_number_cache_stmt) {
    sqlite3_finalize(_cidb_find_number_cache_stmt);
  }
  if (_cidb_db) {
    sqlite3_close(_cidb_db);
  }
}

/** @brief find a caller to a given number
 *  @param[in,out] caller a pointer to a CICaller structure holding the complete number
 *  @return 0 if something was found, 1 otherwise or if an error occured
 */
gint cidb_find_caller(CICaller * caller) {
  int rc;
  gboolean found = FALSE;
  
  char * buf;
  
  if (!caller)
    return 1;
  
  /*find in cache*/
  rc = sqlite3_bind_text(_cidb_find_number_cache_stmt, 1, caller->NumberComplete, strlen(caller->NumberComplete), SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
 
    rc = sqlite3_step(_cidb_find_number_cache_stmt);
    if (rc == SQLITE_ROW) {
      buf = (char*)sqlite3_column_text(_cidb_find_number_cache_stmt, 0);
      if (buf) {
        strcpy(caller->Name, buf);
      }
      buf = (char*)sqlite3_column_text(_cidb_find_number_cache_stmt, 1);
      if (buf) {
        strcpy(caller->City, buf);
      }
      buf = (char*)sqlite3_column_text(_cidb_find_number_cache_stmt, 2);
      if (buf) {
        strcpy(caller->Street, buf);
      }
      buf = (char*)sqlite3_column_text(_cidb_find_number_cache_stmt, 3);
      if (buf) {
        strcpy(caller->PostalCode, buf);
      }

      found = TRUE;
    }
    sqlite3_reset(_cidb_find_number_cache_stmt);
  }
  
  if (found) {
    return 0;
  }
  else {
    return 1;
  }
}

/** @brief insert a caller in the database
 *  @param[in] caller Pointer to a CICaller structure containing the informations to be stored
 *  @param[in] loc Location where data should be stored.
 *  @return 0 on success
 */
gint cidb_insert_caller(CICaller * caller) {
  char * sql;
  int rc;
  
  if (!caller) {
    return 1;
  }
  
  sql = sqlite3_mprintf("insert into %s (number, name, city, street, postalcode, comment, category) values (%Q,%Q,%Q,%Q,%Q,%Q,%d)",
    "callercache", 
    caller->NumberComplete, caller->Name, caller->City, caller->Street, caller->PostalCode, NULL, 0);
  rc = sqlite3_exec(_cidb_db, sql, NULL, NULL, NULL);
  sqlite3_free(sql);
  if (rc != SQLITE_OK) {
    return 1;
  }
  else {
    return 0;
  }
}

/** @brief Clear the caller cache.
 */
void cidb_clear_cache(void) {
  char * sql;
/*  int rc;*/
  sql = "delete from callercache";
  /*rc =*/ (void)sqlite3_exec(_cidb_db, sql, NULL, NULL, NULL);
  sqlite3_free(sql);
}

/* local parse from file */

/** @name Field types
 */
/* @{ */
#define CI_FIELD_NAME             1<<0 /**< the name */
#define CI_FIELD_POSTALCODE       1<<1 /**< the postal code */
#define CI_FIELD_CITY             1<<2 /**< the city */
#define CI_FIELD_STREET           1<<3 /**< the street */
/* @} */

/** @internal
 *  @brief Description of a field in a pattern
 */
typedef struct _CIRLPatternField {
  gint position;  /**< the position in the regular expression associated to that field */
  gulong field;   /**< the field this position belongs to */
} CIRLPatternField;

/** @internal
 *  @brief a pattern to match in the file
 */
typedef struct _CIRLPattern {
  GSList * fields;     /**< a list of field descriptors */
  gchar * expression;  /**< a regular expression */
} CIRLPattern;

/** @internal
 *  @brief description of a source
 */
typedef struct _CIRLSource {
  gulong id;             /**< identifier of the source */
  gchar * description;   /**< human readable description of the source */
  gchar * query;         /**< url with placeholder for the number */
  gboolean split_lines;  /**< TRUE if the source can be matched line by line */
  GSList * patterns;     /**< list of patterns */
} CIRLSource;

GSList * _cirl_sources = NULL;  /**< list of online sources */
CURL * _cirl_curl = NULL;       /**< handle for curl */

/** @brief initialize the internet reverse lookup system
 *  @return 0 on success, 1 if an error occured
 */
gint cirlw_init(void) {
  curl_global_init(CURL_GLOBAL_ALL);
  _cirl_curl = curl_easy_init();
  if (!_cirl_curl) {
    return 1;
  }
  
  return 0;
}

/** @brief cleanup the ressources used by the system
 */
void cirlw_cleanup(void) {
  if (_cirl_curl) {
    curl_easy_cleanup(_cirl_curl);
    _cirl_curl = NULL;
  }
  curl_global_cleanup();
  
  /* clear list */
  GSList * tmp1 = _cirl_sources;
  GSList * tmp2;
  GSList * tmp3;
  while (tmp1) {
    g_free(((CIRLSource*)tmp1->data)->description);
    g_free(((CIRLSource*)tmp1->data)->query);
    tmp2 = ((CIRLSource*)tmp1->data)->patterns;
    while (tmp2) {
      g_free(((CIRLPattern*)tmp2->data)->expression);
      tmp3 = ((CIRLPattern*)tmp2->data)->fields;
      while (tmp3) {
        g_free((CIRLPatternField*)tmp3->data);
        tmp3 = g_slist_remove(tmp3, tmp3->data);
      }
      g_free((CIRLPattern*)tmp2->data);
      tmp2 = g_slist_remove(tmp2, tmp2->data);
    }
    g_free((CIRLSource*)tmp1->data);
    tmp1 = g_slist_remove(tmp1, tmp1->data);
  }
  _cirl_sources = NULL;
}

/** @brief load the sources from file specified by filename
 * 
 *  Parses an XML-File with all sources.
 *  @param[in] filename the filename of the source file
 *  @return 0 if successful
 */
gint cirlw_load_sources_from_file(const gchar * filename) {

  xmlDocPtr doc; /* the document*/
  xmlNode *root, *node_source, *node_sub_source, *node_field;
  xmlChar *str;
  
  CIRLSource * source;
  CIRLPattern * pattern;
  CIRLPatternField * patternfield;
    
  if (_cirl_sources) {
    return 1;
  }
  
  /* parse document */
  doc = xmlParseFile(filename);
  
  if (doc == NULL) {
    return 2;
  }
  
  root = xmlDocGetRootElement(doc);
  
  if (!root || !root->name || xmlStrcmp(root->name, (const xmlChar*)"cirevlookupsources")) {
    xmlFreeDoc(doc);
    return 3;
  }
  
  for (node_source = root->children; node_source != NULL; node_source = node_source->next) {
    if (node_source->type == XML_ELEMENT_NODE &&
        !xmlStrcmp(node_source->name, (const xmlChar*)"source")) {
      /* allocate mem */
      source = g_malloc0(sizeof(CIRLSource));
      
      /* get id */
      str = xmlGetProp(node_source, (const xmlChar*)"id");
      if (str) {
        source->id = strtoul((const char*)str, NULL, 10);
        xmlFree(str);
      }
      
      for (node_sub_source = node_source->children; node_sub_source != NULL; node_sub_source = node_sub_source->next) {
        if (node_sub_source->type == XML_ELEMENT_NODE) {
          if (!xmlStrcmp(node_sub_source->name, (const xmlChar*)"description")) {
            str = xmlNodeGetContent(node_sub_source);
            if (str) {
              source->description = g_strdup((const char*)str);
              xmlFree(str);
            }
          }
          else if (!xmlStrcmp(node_sub_source->name, (const xmlChar*)"query")) {
            str = xmlGetProp(node_sub_source, (xmlChar*)"linehandling");
            source->split_lines = TRUE;
            if (str) {
              if (!xmlStrcmp(str, (xmlChar*)"nosplit")) {
                source->split_lines = FALSE;
              }
              xmlFree(str);             
            }
            str = xmlNodeGetContent(node_sub_source);
            if (str) {
              source->query = g_strdup((const char*)str);
              xmlFree(str);
            }
          }
          else if (!xmlStrcmp(node_sub_source->name, (const xmlChar*)"pattern")) {
            pattern = g_malloc0(sizeof(CIRLPattern));
            str = xmlGetProp(node_sub_source, (xmlChar*)"expression");
            if (str) {
              pattern->expression = g_strdup((const char*)str);
              xmlFree(str);
            }
            for (node_field = node_sub_source->children;
                 node_field != NULL;
                 node_field = node_field->next) {
              if (node_field->type == XML_ELEMENT_NODE &&
                  !xmlStrcmp(node_field->name, (xmlChar*)"field")) {
                patternfield = g_malloc0(sizeof(CIRLPattern));
                str = xmlGetProp(node_field, (xmlChar*)"pos");
                if (str) {
                  patternfield->position = atoi((const char*)str);
                  xmlFree(str);
                }
                str = xmlNodeGetContent(node_field);
                if (str) {
                  if (!xmlStrcmp(str, (xmlChar*)"FIELD_NAME")) {
                    patternfield->field = CI_FIELD_NAME;
                  }
                  else if (!xmlStrcmp(str, (xmlChar*)"FIELD_POSTALCODE")) {
                    patternfield->field = CI_FIELD_POSTALCODE;
                  }
                  else if (!xmlStrcmp(str, (xmlChar*)"FIELD_CITY")) {
                    patternfield->field = CI_FIELD_CITY;
                  }
                  else if (!xmlStrcmp(str, (xmlChar*)"FIELD_STREET")) {
                    patternfield->field = CI_FIELD_STREET;
                  }
                  xmlFree(str);
                }
                pattern->fields = g_slist_prepend(pattern->fields, patternfield);
              }
            } /* for node_field */
            pattern->fields = g_slist_reverse(pattern->fields);
            source->patterns = g_slist_prepend(source->patterns, pattern);
          } /* if pattern */
        } 
      } /* for node_sub_source*/       
      source->patterns = g_slist_reverse(source->patterns);
      _cirl_sources = g_slist_prepend(_cirl_sources, source);
    }
  } /* for node_source */
  _cirl_sources = g_slist_reverse(_cirl_sources);
  
  xmlFreeDoc(doc);
  xmlCleanupParser();
  
  return 0;
}

/** @internal
 *  @brief callback function to find a given source in the list
 *  @param[in] src pointer to a source
 *  @param[in] id pointer to the id
 *  @return 0 if the source matches the id
 */
gint _cirlw_compare_source_cb(gconstpointer src, gconstpointer id) {
  return !(((CIRLSource*)src)->id == *((gulong*)id));
}

/** @internal
 *  @brief find a source to a given source id
 *  @param[in] sid the id
 *  @return Pointer to the source if found, NULL if the source does not exist
 */
CIRLSource * _cirlw_find_source(gulong sid) {
  GSList * entry = g_slist_find_custom(_cirl_sources, &sid, _cirlw_compare_source_cb);
  if (entry)
    return ((CIRLSource*)entry->data);
  else
    return NULL;
}

/* reading from www */

/** @internal
 *  @brief hold dynamic memory
 */
typedef struct _DynMem {
  size_t size;  /**< size of the memory */
  gchar * mem;  /**< buffer holding the data */
} DynMem;

size_t _cirlw_read_data(void * ptr, size_t size, size_t nmemb, void * stream);
gchar * _cirlw_prepare_url(gchar * url, CICaller * caller);
gint _cirlw_match_patterns(CIRLSource * source, CICaller * caller, gulong * found);

DynMem _cirlw_memory; /**< @internal handle to the memory */

/** @internal
 *  @brief callback function to read data from the web to a buffer
 *  @param[in] ptr the data
 *  @param[in] size the size of a member
 *  @param[in] nmemb the number of members
 *  @param[in] stream the stream where the data should be written
 *  @return number of bytes written
 */
size_t _cirlw_read_data(void * ptr, size_t size, size_t nmemb, void * stream) {
  DynMem * mem = (DynMem*)stream;
  size_t realsize = size*nmemb;
  mem->mem = g_try_realloc(mem->mem, mem->size+realsize+1);
  if (!mem->mem) return -1;
  
  memcpy(&mem->mem[mem->size], ptr, realsize);
  mem->size += realsize;
  mem->mem[mem->size] = 0;
  return realsize;
}

/** @internal
 *  @brief get the difference between the space for the placeholder and the substitute
 *  @param[in] str pointer to the position in the string where the substitution begins
 *  @param[in] caller structure holding the possible substitute values
 *  @return the difference between the placeholder and the substitute
 */
gint _getsubstitutediff(gchar * str, CICaller * caller) {
  gchar pattern[32];
  size_t i = 1;
  if (str[0] != '%') return 0;
  while (str[i] != '%') { pattern[i-1] = str[i]; i++;}
  pattern[i-1] = 0;
  size_t sublen = 0;
  if (!strcmp(pattern, "NUMBER")) {
    sublen = strlen(caller->NumberComplete);
  }
  else
    return 0;
  return sublen-strlen(pattern)-2;
}

/** @internal
 *  @brief prepare the url for the query
 * 
 *  substitutes the placeholders with the values from caller, i.e. the complete number
 *  @param[in]  url the raw url
 *  @param[in]  caller the caller data
 *  @return the url for the query
 */
gchar * _cirlw_prepare_url(gchar * url, CICaller * caller) {
  gchar pattern[32];
  size_t length;
  size_t i, j, k;
  i = 0; j = 0;
  length = strlen(url);
  gint diff = 0;
  while (url[i] != '\0') {
    if (url[i] == '%' && !g_ascii_isdigit(url[i+1])) {
      diff += _getsubstitutediff(&url[i], caller);
      do { i++; } while (url[i] != '%');
    }
    i++;
  }
  gchar * out = g_try_malloc(length+1+diff);
  if (!out) return NULL;
  
  i = 0;
  while (url[i] != '\0') {
    if (url[i] == '%' && !g_ascii_isdigit(url[i+1])) {
      k = 0;
      while (url[i+k+1] != '%') { pattern[k] = url[i+k+1]; k++; }
      pattern[k] = '\0';
      i += k+2;
      if (!strcmp(pattern, "NUMBER")) {
        strcpy(&out[j], caller->NumberComplete);
        j += strlen(caller->NumberComplete);
      }
      else {
        i -= k+2;
      }
    }
    else {
      out[j] = url[i];
      i++; j++;
    }
  }
  out[j] = '\0';
  return out;
}

/** @internal
 *  @brief remove url escape sequences (currently only space) and substitute them by the appropriate character
 *  @param[in,out] str the string from which the escape sequence should be removed
 */
void _cirlw_remove_escapes(gchar * str) {
  gint i = 0;
  gint d = 0;

  while (str[i+d] != '\0') {
    if (str[i+d] == '%') {
      if (str[i+d+1] == '2' && str[i+d+2] == '0') {  /*space*/
        str[i] = ' ';
        d += 2;
      }
      else if (d > 0){
        str[i] = str[i+d];
      }
    }
    else if (d > 0) {
      str[i] = str[i+d];
    }
    i++;
  }
  str[i] = '\0';
}

/** @internal
 *  @brief match the patterns in given source and fill in the caller data
 *  @param[in] source the source where the data should be searched
 *  @param[out] caller structure containing all data
 *  @param[out] found bitfield describing the fields filled in
 *  @return 0 on success
 */
gint _cirlw_match_patterns(CIRLSource * source, CICaller * caller, gulong * found) {
  guint npat = g_slist_length(source->patterns);
  gint err = 0;
  if (npat == 0)
    return 1;
  gint i;
  guint j;
  gulong _fnd = 0;
  GMatchInfo * matchinfo;
  gchar * word;
  gchar * charset = NULL;
  gchar ** lines = NULL;
  GRegex ** reg = g_try_malloc(sizeof(GRegex*)*(npat+1));
  if (!reg)
    return 1;
  CIRLPattern ** pats = g_try_malloc(sizeof(GRegex*)*npat);
  if (!pats) {
    g_free(reg);
    return 1;
  }    
  GSList * tmp_pat = source->patterns;
  GSList * tmp_field;
  j = 0;
  while (tmp_pat) {
    reg[j] = g_regex_new(((CIRLPattern*)tmp_pat->data)->expression, G_REGEX_RAW, 0, NULL);
    pats[j] = (CIRLPattern*)tmp_pat->data;
    j++;
    tmp_pat = g_slist_next(tmp_pat);
  }
  reg[npat] = g_regex_new("[C|c][H|h][A|a][R|r][S|s][E|e][T|t]\\s*=\\s*([A-Za-z0-9-]+)", G_REGEX_RAW, 0, NULL);
  if (source->split_lines) {
    lines = g_strsplit(_cirlw_memory.mem, "\n", 0);
    if (!lines) {
      err = 1;
    }
    else {
      i = 0;
      while (lines[i]) {
        for (j = 0; j < npat+1; j++) {
          g_regex_match(reg[j], lines[i], 0, &matchinfo);
          if (g_match_info_matches(matchinfo)) {
            if (j < npat) {
              tmp_field = pats[j]->fields;
              while (tmp_field) {
                word = g_match_info_fetch(matchinfo, ((CIRLPatternField*)tmp_field->data)->position);
                switch (((CIRLPatternField*)tmp_field->data)->field) {
                  case CI_FIELD_NAME:
                    strcpy(caller->Name, word);
/*                    _cirlw_remove_escapes(caller->Name);*/
                    /*if (found) *found |= CI_FIELD_NAME;*/
                    _fnd |= CI_FIELD_NAME;
                    break;
                  case CI_FIELD_CITY:
                    strcpy(caller->City, word); 
/*                    _cirlw_remove_escapes(caller->City);*/
                    /*if (found) *found |= CI_FIELD_CITY;*/
                    _fnd |= CI_FIELD_CITY;
                    break;
                  case CI_FIELD_POSTALCODE:
                    strcpy(caller->PostalCode, word);
/*                    _cirlw_remove_escapes(caller->PostalCode);*/
                    /*if (found) *found |= CI_FIELD_POSTALCODE;*/
                    _fnd |= CI_FIELD_POSTALCODE;
                    break;
                  case CI_FIELD_STREET:
                    strcpy(caller->Street, word); 
/*                    _cirlw_remove_escapes(caller->Street);*/
                    /*if (found) *found |= CI_FIELD_STREET;*/
                    _fnd |= CI_FIELD_STREET;
                    break;
                }
                g_free(word);
                tmp_field = g_slist_next(tmp_field);
              }
            }
            else {
              charset = g_match_info_fetch(matchinfo, 1);   
            }
          }
          g_match_info_free(matchinfo);
        }
        i++;
      }
      g_strfreev(lines);
    }
    if (found) *found = _fnd;
    if (charset) {
      log_log("charset: %s\n", charset);
      
      if (_fnd & CI_FIELD_NAME) {
        word = g_convert(caller->Name, -1, "utf-8", charset, NULL, NULL, NULL);
        if (word) strcpy(caller->Name, word);
        _cirlw_remove_escapes(caller->Name);
        g_free(word);
      }
      if (_fnd & CI_FIELD_CITY) {
        word = g_convert(caller->City, -1, "utf-8", charset, NULL, NULL, NULL);
        if (word) strcpy(caller->City, word);
        _cirlw_remove_escapes(caller->City);
        g_free(word);
      }
      if (_fnd & CI_FIELD_POSTALCODE) {
        _cirlw_remove_escapes(caller->PostalCode);
      }
      if (_fnd & CI_FIELD_STREET) {
        word = g_convert(caller->Street, -1, "utf-8", charset, NULL, NULL, NULL);
        if (word) strcpy(caller->Street, word);
        _cirlw_remove_escapes(caller->Street);
        g_free(word);
      }
      g_free(charset);
    }
  }
  else {
    /* TODO pattern match without splitting */
  }
  
  for (j = 0; j < npat+1; j++)
  {
    g_regex_unref(reg[j]);
  }
  g_free(reg);
  g_free(pats);
  return err;
}

/** @brief find a caller in the web
 *  @param[in] sourceid the id of the source where the caller should be searched
 *  @param[in,out] caller the caller information
 *  @return 0 if something was found
 */
gint cirlw_get_caller(gulong sourceid, CICaller * caller) {
  CIRLSource * source = _cirlw_find_source(sourceid);
  gint err = 0;
  if (!source)
    return 1;
  gchar * url = _cirlw_prepare_url(source->query, caller);
  if (!url)
    return 1;
  
  CURLcode res;
  
  if (!_cirl_curl) {
    g_free(url);
    return 1;
  }
  
  /* init mem */
  memset(&_cirlw_memory, 0, sizeof(DynMem));
  
  curl_easy_setopt(_cirl_curl, CURLOPT_URL, url);
  curl_easy_setopt(_cirl_curl, CURLOPT_WRITEFUNCTION, _cirlw_read_data);
  curl_easy_setopt(_cirl_curl, CURLOPT_WRITEDATA, &_cirlw_memory);
  curl_easy_setopt(_cirl_curl, CURLOPT_USERAGENT, "Mozilla/5.0");
  curl_easy_setopt(_cirl_curl, CURLOPT_CONV_FROM_NETWORK_FUNCTION, NULL);
  curl_easy_setopt(_cirl_curl, CURLOPT_CONV_TO_NETWORK_FUNCTION, NULL);
/*  printf("CURL_ICONV_CODESET_OF_HOST: %s\nCURL_ICONV_CODESET_OF_NETWORK: %s\nCURL_ICONV_CODESET_FOR_UTF8: %s\n",
    CURL_ICONV_CODESET_OF_HOST, CURL_ICONV_CODESET_OF_NETWORK, CURL_ICONV_CODESET_FOR_UTF8);*/
  res = curl_easy_perform(_cirl_curl);
  
  
  if (res == CURLE_OK) {
    gulong found = 0;
    err = _cirlw_match_patterns(source, caller, &found);
    if (found == 0)
      err = 4;
  }
  else {
    err = 1;
  }
 
  g_free(_cirlw_memory.mem);
  g_free(url);
  return err;
}

gint lookup_init(gchar * lookup_sources, gchar * lookup_cache) {
  gint ret = 0;
  if (cidb_init() != 0) ret = 1;
  if (cirlw_init() != 0) ret = 1;
  if (ret == 0) {
    if (cidb_connect(lookup_cache) != 0) {
      ret = 1;
    }
    if (cirlw_load_sources_from_file(lookup_sources) != 0) {
      ret = 1;
    }
  }
  return ret;
}

gint lookup_get_caller_data(CIDataSet * cidata) {
  log_log("lookup_get_caller_data\n");
  const Fritz2CIConfig * cfg = config_get_config();
  if (!cidata)
    return 1;
  CICaller * caller = g_malloc0(sizeof(CICaller));
  strcpy(caller->NumberComplete, cidata->cidsNumberComplete);
  gboolean found = FALSE;
  if (cidb_find_caller(caller) != 0) {
    log_log("not in cache\n");
    /* TODO: use lookup_source_id from config */
    if (cirlw_get_caller(cfg->lookup_source_id, caller) == 0) {
      log_log("found online\n");
      found = TRUE;
      cidb_insert_caller(caller);
    }
  }
  else found = TRUE;
  if (found) {
    log_log("found: %s\n", caller->Name);
    strcpy(cidata->cidsName, caller->Name);
  }
  return !(found);
}

void lookup_cleanup(void) {
  cidb_cleanup();
  cirlw_cleanup();
}
