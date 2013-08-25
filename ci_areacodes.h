/* $Id: ci_areacodes.h 20 2010-08-17 20:23:43Z lahol $
 * $Rev: 20 $
 * $Author: lahol $
 * $Date: 2010-08-17 22:23:43 +0200 (Di, 17. Aug 2010) $
 */
/** @file
 *  @brief Header file for area codes tree
 */
#ifndef __CI_AREACODES_H__
#define __CI_AREACODES_H__

/** @brief Areacode with area
 */
typedef struct _CIAreaCode {
    char acCode[16];  /**< the area code */
    char acArea[64];  /**< the area name */
} CIAreaCode;


/** @brief Tree node for the area codes
 *
 *  Each node has 10 children or contains a area if there is an assignment for that number
 */
typedef struct _CIAreaCodeTree CIAreaCodeTree;
struct _CIAreaCodeTree {
    CIAreaCodeTree *child[10];   /**< the children of this node */
    CIAreaCode *data;             /**< the data or NULL */
};

void ci_init_area_codes(void);
int ci_read_area_codes_from_file(char *filename);
void ci_free_area_codes(void);
int ci_get_area_code(char *numComplete, char *numAreaCode, char *numNumber, char *strArea);

#endif
