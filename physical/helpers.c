#include "../include/helpers.h"

/*
 * Contains helper functions used throughout the project
 *
 *  Created on: 29-Oct-2014
 *      Author: nithin, dheeraj
 */
int readStringFromByteArray(char * string, const char *byteArray, const int offset, const int size) {
    if (byteArray == NULL || string == NULL) {
        return ErrorMsgs(NULL_POINTER_EXCEPTION, g_PrintFlag);
    }
    strncpy(string, (byteArray + offset), size);
    return OK;
}

int readIntFromByteArray(const char *byteArray, const int offset) {
    int val;
    memcpy(&val, byteArray + offset, sizeof(int));
    return val;
}

float readFloatFromByteArray(const char* byteArray, const int offset) {
    float val;
    memcpy(&val, byteArray + offset, sizeof(float));
    return val;
}

void convertIntToByteArray(int value, char *byteArray) {
    memcpy(byteArray, (void *) &value, sizeof(int));
}

void convertFloatToByteArray(float value, char *byteArray) {
    memcpy(byteArray, (void *) &value, sizeof(int));
}

/*************************************************************
 COMPARISONS
 *************************************************************/

bool compareNum(float val1, float val2, int compOp) {
    switch (compOp) {
        case EQ:
            return val1 == val2 ? TRUE : FALSE;
        case NEQ:
            return val1 != val2 ? TRUE : FALSE;
        case GEQ:
            return val1 >= val2 ? TRUE : FALSE;
        case LEQ:
            return val1 <= val2 ? TRUE : FALSE;
        case GT:
            return val1 > val2 ? TRUE : FALSE;
        case LT:
            return val1 < val2 ? TRUE : FALSE;
        default:
            ErrorMsgs(INVALID_COMP_OP, g_PrintFlag);
            break;
    }
    return FALSE;
}

bool compareStrings(char *s1, char *s2, int compOp) {
    switch (compOp) {
        case EQ:
            return strcmp(s1, s2) == 0 ? TRUE : FALSE;
        case NEQ:
            return strcmp(s1, s2) ? TRUE : FALSE;
        case GEQ:
            return (strcmp(s1, s2) >= 0) ? TRUE : FALSE;
        case LEQ:
            return (strcmp(s1, s2) <= 0) ? TRUE : FALSE;
        case GT:
            return (strcmp(s1, s2) > 0) ? TRUE : FALSE;
        case LT:
            return (strcmp(s1, s2) < 0) ? TRUE : FALSE;
        default:
            ErrorMsgs(INVALID_COMP_OP, g_PrintFlag);
            break;
    }
    return FALSE;
}
/**
 * Gets the last valid Rid in a relation
 *
 * @param   relNum
 * @return  last valid Rid
 */
Rid getLastRid(int relNum) {
    int numRecs = g_CatCache[relNum].numRecs;
    int recsPerPg = g_CatCache[relNum].recsPerPg;
    Rid last;
    last.pid = numRecs / recsPerPg + 1;
    last.slotnum = numRecs % recsPerPg + 1;
    return last;
}

/*
 * Function: separateDbPath() 
 * ----------------------------
 * Separates dbname and path from the the string, dbname with path
 *
 *  fullDBPath : full path of database
 *  path         : path of database without dbname
 *  dbName       : database name
 *
 *  returns: OK on success
 *           NOTOK on failure
 */

int separateDBPath(char* fullDBPath, char* path, char* dbName) {
    if (fullDBPath == NULL)
        return NOTOK;

    char *separator = strrchr(fullDBPath, '/');
#ifdef _WIN32
    char *windowsSeparator = strrchr(fullDBPath, '\\');
    if (windowsSeparator != NULL && (separator == NULL || windowsSeparator > separator)) {
        separator = windowsSeparator;
    }
#endif

    if (separator == NULL) {
        strcpy(path, ".");
        strcpy(dbName, fullDBPath);
    } else {
        int pathLength = separator - fullDBPath;
        if (pathLength == 0) {
            pathLength = 1;
        }
        strncpy(path, fullDBPath, pathLength);
        path[pathLength] = '\0';
        strcpy(dbName, separator + 1);
    }

    if (strlen(dbName) > RELNAME)
        return ErrorMsgs(DBNAME_EXCEED_LIMIT, g_PrintFlag);
    return OK;
}

/*************************************************************
 HELPERS FOR SCHEMA LAYER
 *************************************************************/

/**
 * Compares two records byte by byte
 * 
 * @param record1
 * @param record2
 * @param sizeOfRecord
 * @return
 */
int compareRecords(char *record1, char *record2, int sizeOfRecord) {
    int i;
    for (i = 0; i < sizeOfRecord; ++i) {
        if (record1[i] != record2[i]) {
            break;
        }
    }
    return (i != sizeOfRecord) ? NOTOK : OK;
}

/**
 *  Gets the N from sN
 * @param sN
 * @return
 */
int getN(char *sN) {
    int i, N;
    N = atoi(sN + 1);
    return N;
}

/**
 * Checks if the string is valid
 * STRING: A character set that starts with an alphabet
 * and is followed by an arbitrary number of alphabets and digits
 *
 * @param string
 * @return OK if valid
 */
int isValidString(char *string) {
    int i;
    if (!isalpha(string[0])) {
        return NOTOK;
    } else if (strlen(string) >= RELNAME) {
        return NOTOK;
    }
    for (i = 1; string[i] != '\0'; i++) {
        if (!isalnum((unsigned char) string[i]) && string[i] != '_') {
            return NOTOK;
        }
    }
    return OK;
}

/**
 * Get size of attribute from attributeFormat
 *
 * @param attrFormat
 * @return size of the attribute
 */
int getSizeOfAttr(char *attrFormat) {
    int size = 0;
    switch (attrFormat[0]) {
        case INTEGER:
            size = sizeof(int);
            break;
        case STRING:
            size = getN(attrFormat);
            break;
        case FLOAT:
            size = sizeof(float);
            break;
        default:
            break;
    }
    return size;
}

unsigned int NullBitmapSize(unsigned int attributeCount) {
        return (attributeCount + 7) / 8;
    }

    bool IsNullValueMarker(const char *text) {
        return text != NULL && strcmp(text, NULL_VALUE_MARKER) == 0;
    }

    static unsigned int nullBitmapOffset(const CacheEntry *relation) {
        return relation->recLength - relation->nullBitmapBytes;
    }

    bool RecordAttributeIsNull(
            const CacheEntry *relation,
            const char *record,
            const struct attrCatalog *attribute) {
        unsigned int byteIndex;
        unsigned int bitIndex;
        const unsigned char *bitmap;

        if (relation == NULL || record == NULL || attribute == NULL
                || relation->hasNullBitmap == FALSE) {
            return FALSE;
        }

        byteIndex = attribute->position / 8;
        bitIndex = attribute->position % 8;
        bitmap = (const unsigned char *) (
                record + nullBitmapOffset(relation));
        return (bitmap[byteIndex] & (1U << bitIndex)) != 0;
    }

    void RecordSetAttributeNull(
            const CacheEntry *relation,
            char *record,
            const struct attrCatalog *attribute) {
        unsigned int byteIndex;
        unsigned int bitIndex;
        unsigned char *bitmap;

        memset(record + attribute->offset, 0, attribute->length);
        if (relation->hasNullBitmap == FALSE) {
            return;
        }

        byteIndex = attribute->position / 8;
        bitIndex = attribute->position % 8;
        bitmap = (unsigned char *) (record + nullBitmapOffset(relation));
        bitmap[byteIndex] |= (unsigned char) (1U << bitIndex);
    }

    void RecordClearAttributeNull(
            const CacheEntry *relation,
            char *record,
            const struct attrCatalog *attribute) {
        unsigned int byteIndex;
        unsigned int bitIndex;
        unsigned char *bitmap;

        if (relation->hasNullBitmap == FALSE) {
            return;
        }

        byteIndex = attribute->position / 8;
        bitIndex = attribute->position % 8;
        bitmap = (unsigned char *) (record + nullBitmapOffset(relation));
        bitmap[byteIndex] &= (unsigned char) ~(1U << bitIndex);
    }

    int EncodeTextValue(
            const struct attrCatalog *attribute,
            const char *text,
            char *destination,
            bool *isNull) {
        char *end;

        if (attribute == NULL || text == NULL || destination == NULL || isNull == NULL) {
            return ErrorMsgs(NULL_ARGUMENT_RECEIVED, g_PrintFlag);
        }

        memset(destination, 0, attribute->length);
        *isNull = IsNullValueMarker(text);
        if (*isNull == TRUE) {
            return OK;
        }

        errno = 0;
        switch (attribute->type) {
            case INTEGER: {
                long value = strtol(text, &end, 10);
                if (*text == '\0' || *end != '\0') {
                    return ErrorMsgs(INTEGER_EXPECTED, g_PrintFlag);
                }
                if (errno == ERANGE || value < INT_MIN || value > INT_MAX) {
                    return ErrorMsgs(NUMERIC_OUT_OF_RANGE, g_PrintFlag);
                }
                convertIntToByteArray((int) value, destination);
                return OK;
            }
            case FLOAT: {
                float value = strtof(text, &end);
                if (*text == '\0' || *end != '\0') {
                    return ErrorMsgs(FLOAT_EXPECTED, g_PrintFlag);
                }
                if (errno == ERANGE || !isfinite(value)) {
                    return ErrorMsgs(NUMERIC_OUT_OF_RANGE, g_PrintFlag);
                }
                convertFloatToByteArray(value, destination);
                return OK;
            }
            case STRING:
                if (strlen(text) > attribute->length) {
                    return ErrorMsgs(MAX_STRING_EXCEEDED, g_PrintFlag);
                }
                memcpy(destination, text, strlen(text));
                return OK;
            default:
                return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
        }
    }

    int ValidateRecordForRelation(const CacheEntry *relation, const char *record) {
        const struct attrCatalog *attribute;
        unsigned int usedBits;
        unsigned char allowedBits;
        const unsigned char *bitmap;

        if (relation == NULL || record == NULL) {
            return ErrorMsgs(NULL_ARGUMENT_RECEIVED, g_PrintFlag);
        }
        if (relation->hasNullBitmap == TRUE) {
            if (relation->nullBitmapBytes != NullBitmapSize(relation->numAttrs)
                    || relation->recLength < relation->nullBitmapBytes) {
                return ErrorMsgs(INVALID_NULL_BITMAP, g_PrintFlag);
            }
            usedBits = relation->numAttrs % 8;
            if (usedBits != 0) {
                bitmap = (const unsigned char *) (
                        record + nullBitmapOffset(relation));
                allowedBits = (unsigned char) ((1U << usedBits) - 1U);
                if ((bitmap[relation->nullBitmapBytes - 1] & ~allowedBits) != 0) {
                    return ErrorMsgs(INVALID_NULL_BITMAP, g_PrintFlag);
                }
            }
        }

        for (attribute = relation->attrList;
                attribute != NULL;
                attribute = attribute->next) {
            if (RecordAttributeIsNull(relation, record, attribute) == TRUE) {
                unsigned int i;
                if (attribute->notNull == TRUE) {
                    return ErrorMsgs(NOT_NULL_CONSTRAINT_VIOLATION, g_PrintFlag);
                }
                for (i = 0; i < attribute->length; i++) {
                    if (record[attribute->offset + i] != 0) {
                        return ErrorMsgs(INVALID_NULL_BITMAP, g_PrintFlag);
                    }
                }
            } else if (attribute->type == FLOAT
                    && !isfinite(readFloatFromByteArray(record, attribute->offset))) {
                return ErrorMsgs(NUMERIC_OUT_OF_RANGE, g_PrintFlag);
            }
        }
        return OK;
    }

    int CopyRecordAttribute(
            const CacheEntry *destinationRelation,
            char *destinationRecord,
            const struct attrCatalog *destinationAttribute,
            const CacheEntry *sourceRelation,
            const char *sourceRecord,
            const struct attrCatalog *sourceAttribute) {
        if (destinationAttribute->type != sourceAttribute->type
                || destinationAttribute->length != sourceAttribute->length) {
            return ErrorMsgs(TYPE_MISMATCH, g_PrintFlag);
        }

        if (RecordAttributeIsNull(
                sourceRelation, sourceRecord, sourceAttribute) == TRUE) {
            if (destinationRelation->hasNullBitmap == FALSE) {
                return ErrorMsgs(LEGACY_NULL_UNSUPPORTED, g_PrintFlag);
            }
            RecordSetAttributeNull(
                    destinationRelation, destinationRecord, destinationAttribute);
        } else {
            RecordClearAttributeNull(
                    destinationRelation, destinationRecord, destinationAttribute);
            memcpy(
                    destinationRecord + destinationAttribute->offset,
                    sourceRecord + sourceAttribute->offset,
                    sourceAttribute->length);
        }
        return OK;
}

/**
 * Gets the attribute catalog using the attribute name
 *
 * @param attrList
 * @param attrName
 * @return the attrCatalog struct
 */
struct attrCatalog* getAttrCatalog(struct attrCatalog* attrList, char *attrName) {
    while (attrList != NULL) {
        if (strcmp(attrName, attrList->attrName) == 0) {
            break;
        }
        attrList = attrList->next;
    }
    return attrList;
}
