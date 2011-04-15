/* $Id: ci_areacodes.c 29 2010-08-21 17:21:59Z lahol $
 * $Rev: 29 $
 * $Author: lahol $
 * $Date: 2010-08-21 19:21:59 +0200 (Sa, 21. Aug 2010) $
 */
/** @file
 *  @brief Implementation of the area codes tree
 */ 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#define _(string) (string)

#include "ci_areacodes.h"

long int _ciac_indizes[100];
char * _ciac_filename = NULL;

/** @brief Init the system.
 */
void ci_init_area_codes(void) {
  memset(_ciac_indizes, 0, sizeof(long int)*100);
  _ciac_filename = NULL;
}

/** @brief Read area codes from file
 *
 *  Create an index of the file
 *  @param[in] filename The given filename from which the codes should be read.
 *  @return    0 on success, 1 otherwise
 *  @todo make indizes for multiple areacode files
 */
int ci_read_area_codes_from_file(char * filename) {
  FILE * f;
  long int pos;
  char buffer[256];
  int index;
  
  if (!filename) {
    return 1;
  }
  _ciac_filename = g_strdup(filename);
  
  if ((f = fopen(_ciac_filename, "r")) == NULL) {
    return 1;
  }
  
  while (!feof(f)) {
    pos = ftell(f);
    if (fgets(buffer, 256, f) && strlen(buffer) >= 3 && g_ascii_isdigit(buffer[0]) &&
        g_ascii_isdigit(buffer[1]) && g_ascii_isdigit(buffer[2])) {
      index = 10*g_ascii_digit_value(buffer[1])+g_ascii_digit_value(buffer[2]);
      if (_ciac_indizes[index] == 0) {
        _ciac_indizes[index] = pos;
      }
    }
  } 
  fclose(f); 

  return 0;
}

/** @brief Free all ressources used by the area codes
 */ 
void ci_free_area_codes(void) {
  g_free(_ciac_filename);
}

/** @brief Get an area code for a given number.
 *  @param[in] numComplete The complete number
 *  @param[out] numAreaCode The found area code
 *  @param[out] numNumber The rest of the number
 *  @param[out] strArea The area name
 *  @return     0 on success, non-0 otherwise
 */
int ci_get_area_code(char * numComplete, char * numAreaCode, char * numNumber, char * strArea) {
  int index;
  int i, j;
  char buffer[256];
  FILE * f;

  if (!_ciac_filename || !numComplete || !numAreaCode || !numNumber || !strArea) {
    return 1;
  }
  strcpy(numAreaCode, _("<unknown>"));
  if (numComplete != numNumber) {
    strcpy(numNumber, numComplete);
  }
  strcpy(strArea, _("<unknown>"));

  if (!g_ascii_isdigit(numComplete[0])) {
    return 1;
  }
  numAreaCode[0] = numComplete[0];  
  index = 0;
  if (g_ascii_isdigit(numComplete[1])) {
    index = g_ascii_digit_value(numComplete[1]);
  }
  if (g_ascii_isdigit(numComplete[2])) {
    index = 10*index+g_ascii_digit_value(numComplete[2]);
  }

  if (index > 0 && _ciac_indizes[index] == 0) {
    strcpy(numAreaCode, _("<unknown>"));
    return 1;
  }

  if ((f = fopen(_ciac_filename, "r")) == NULL) {
    return 1;
  }
  
  fseek(f, _ciac_indizes[index], SEEK_SET);
  
  while (!feof(f)) {
    if (fgets(buffer, 256, f)) {
      i = 0;
      while (g_ascii_isdigit(buffer[i]) &&
             g_ascii_digit_value(buffer[i]) == g_ascii_digit_value(numComplete[i])) {
        i++;        
      }
      if (i > 2 && !g_ascii_isdigit(buffer[i])) {
        /* matched all digits */
        for (j = 0; j < i; j++) {
          numAreaCode[j] = numComplete[j];
        }
        numAreaCode[i] = '\0';
        j = 0;
        while (numComplete[j+i] != '\0') {
          numNumber[j] = numComplete[j+i];
          j++;
        }
        numNumber[j] = '\0';
        
        while (g_ascii_isspace(buffer[i])) { i++; }
        
        j = 0;
        while (buffer[i] != '\n' && buffer[i] != '\0') {
          strArea[j] = buffer[i];
          i++; j++;
        }
        strArea[j] = '\0';
        fclose(f);
        return 0;
      }
      else if (g_ascii_digit_value(buffer[i]) > g_ascii_digit_value(numComplete[i])) {
        /* not found */
        strcpy(numAreaCode, _("<unknown>"));
        fclose(f);
        return 1;
      }      
    }
  }
    
  fclose(f);
  return 1;
}
