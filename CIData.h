/* $Id: CIData.h 20 2010-08-17 20:23:43Z lahol $
 * $Rev: 20 $
 * $Author: lahol $
 * $Date: 2010-08-17 22:23:43 +0200 (Di, 17. Aug 2010) $
 */
/** @file
 *  @brief The main data sets of the application
 */
#ifndef __CIDATA_H__
#define __CIDATA_H__

/** @brief A set containing the basic information on a call
 */
typedef struct _CIDataSet {
    char cidsNumber[32];         /**< the number of the caller */
    char cidsNumberComplete[32]; /**< the complete number of the caller */
    char cidsName[256];          /**< the name of the caller */
    char cidsDate[16];           /**< the date of the call */
    char cidsTime[16];           /**< the time of the call */
    char cidsMSN[16];            /**< the number of the called phone */
    char cidsAlias[256];         /**< the alias of the called phone */
    char cidsService[256];       /**< the service of the call */
    char cidsFix[256];           /**< a fixed string */
    char cidsArea[256];          /**< the area of the caller */
    char cidsAreaCode[16];       /**< the area code of the caller */
} CIDataSet;

/** @brief A set containing detailed information on a caller
 */
typedef struct _CICaller {
    char NumberComplete[32];     /**< the complete number of the caller */
    char Name[256];              /**< the name of the caller */
    char Number[32];             /**< the number of the caller */
    char AreaCode[16];           /**< the area code of the caller */
    char PostalCode[8];          /**< the postalcode of the caller */
    char Street[256];            /**< the address of the caller */
    char City[256];              /**< the city/area of the caller */
} CICaller;

#endif
