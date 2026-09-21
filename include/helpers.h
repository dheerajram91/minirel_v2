/*
 * helpers.h
 *
 *  Created on: 30-Oct-2014
 *      Author: nithin
 */

#ifndef HELPERS_H_
#define HELPERS_H_

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include "globals.h"
#include "defs.h"
#include "error.h"

int readStringFromByteArray(char * string, const char *byteArray, const int offset, const int size);

int readIntFromByteArray(const char *byteArray, const int offset);

float readFloatFromByteArray(const char* byteArray, const int offset);

bool compareNum(float val1, float val2, int compOp);

bool compareStrings(char *s1, char *s2, int compOp);

void convertIntToByteArray(int value, char *byteArray);

void convertFloatToByteArray(float value, char *byteArray);

Rid getLastRid(int relNum);

int separateDBPath(char* fullDBPath, char* path, char* dbName);

struct attrCatalog* getAttrCatalog(struct attrCatalog *attrList, char *attrName);

void removeQuotes(char *quotedString);
/*  Helpers for schema/physical layer */
int compareRecords(char *record1, char *record2, int sizeOfRecord);

int getN(char *sN);

int isValidString(char *string);

int getSizeOfAttr(char *attrFormat);

unsigned int NullBitmapSize(unsigned int attributeCount);

bool IsNullValueMarker(const char *text);

bool RecordAttributeIsNull(
        const CacheEntry *relation,
        const char *record,
        const struct attrCatalog *attribute);

void RecordSetAttributeNull(
        const CacheEntry *relation,
        char *record,
        const struct attrCatalog *attribute);

void RecordClearAttributeNull(
        const CacheEntry *relation,
        char *record,
        const struct attrCatalog *attribute);

int EncodeTextValue(
        const struct attrCatalog *attribute,
        const char *text,
        char *destination,
        bool *isNull);

int ValidateRecordForRelation(const CacheEntry *relation, const char *record);

int CopyRecordAttribute(
        const CacheEntry *destinationRelation,
        char *destinationRecord,
        const struct attrCatalog *destinationAttribute,
        const CacheEntry *sourceRelation,
        const char *sourceRecord,
        const struct attrCatalog *sourceAttribute);

#endif /* HELPERS_H_ */
