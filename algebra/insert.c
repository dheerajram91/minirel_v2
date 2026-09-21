#include "../include/insert.h"

/**
 * Implementation of insert command.
 * See documentation for complete specs
 *
 * argv[0] - insert
 * argv[1] - relation name
 * argv[2] - attr name 1
 * argv[3] - attr value 1
 * ...
 * argv[argc] - NIL
 *
 * @param argc - Count of arguments
 * @param argv - you know..
 * @return OK if success
 * @author nithin
 *
 * GLOBAL VARIABLES MODIFIED:
 *      <NONE>
 *
 * ERRORS REPORTED:
 *      - METADATA_SECURITY
 *      - ATTR_NOT_IN_REL
 *      - INSUFFICIENT_ATTRS
 *      - DB_NOT_OPEN
 *
 * ALGORITHM:
 *      1. Check for possible errors
 *      2. Create a char Array of size recLength
 *      3. Read each value from argv and convert it to a byte array
 *      4. Check if any attribute is repeated in the arguments using "hashing"
 *      5. Copy the byte array to correct location (using offset from attrList)
 *      6. Call InsertRec()
 *
 * IMPLEMENTATION NOTES:
 *      Record duplication is checked in InsertRec()
 *      Uses: OpenRel()
 */
int Insert(int argc, char **argv) {
    int i;
    int result;
    int relNum;
    char *recPtr;
    unsigned char *provided;
    struct attrCatalog *attr;

    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }
    if (argc < 4 || argc % 2 != 0) {
        return ErrorMsgs(NO_ATTRIBUTES_TO_INSERT, g_PrintFlag);
    }

    if ((strcmp(argv[0], "_insert") != 0)
            && (strcmp(argv[1], RELCAT) == 0 || strcmp(argv[1], ATTRCAT) == 0)) {
        return ErrorMsgs(METADATA_SECURITY, g_PrintFlag);
    }

    char relName[RELNAME];
    strcpy(relName, argv[1]);

    relNum = OpenRelWithLock(relName, LOCK_EXCLUSIVE);
    if (relNum == NOTOK) {
        return NOTOK;
    }

    recPtr = (char *) calloc(g_CatCache[relNum].recLength, sizeof(char));
    provided = (unsigned char *) calloc(g_CatCache[relNum].numAttrs, 1);
    if (recPtr == NULL || provided == NULL) {
        free(recPtr);
        free(provided);
        return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
    }

    for (i = 2; i < argc; i += 2) {
        bool isNull;
        attr = getAttrCatalog(g_CatCache[relNum].attrList, argv[i]);
        if (attr == NULL) {
            result = ErrorMsgs(ATTR_NOT_IN_REL, g_PrintFlag);
            goto cleanup;
        }
        if (provided[attr->position] != 0) {
            result = ErrorMsgs(ATTR_REPEATED, g_PrintFlag);
            goto cleanup;
        }
        provided[attr->position] = 1;
        if (EncodeTextValue(
                attr,
                argv[i + 1],
                recPtr + attr->offset,
                &isNull) != OK) {
            result = NOTOK;
            goto cleanup;
        }
        if (isNull == TRUE) {
            if (g_CatCache[relNum].hasNullBitmap == FALSE) {
                result = ErrorMsgs(LEGACY_NULL_UNSUPPORTED, g_PrintFlag);
                goto cleanup;
            }
            if (attr->notNull == TRUE) {
                result = ErrorMsgs(NOT_NULL_CONSTRAINT_VIOLATION, g_PrintFlag);
                goto cleanup;
            }
            RecordSetAttributeNull(&g_CatCache[relNum], recPtr, attr);
        }
    }

    for (attr = g_CatCache[relNum].attrList; attr != NULL; attr = attr->next) {
        if (provided[attr->position] != 0) {
            continue;
        }
        if (g_CatCache[relNum].hasNullBitmap == FALSE) {
            result = ErrorMsgs(INSUFFICIENT_ATTRS, g_PrintFlag);
            goto cleanup;
        }
        if (attr->notNull == TRUE) {
            result = ErrorMsgs(NOT_NULL_CONSTRAINT_VIOLATION, g_PrintFlag);
            goto cleanup;
        }
        RecordSetAttributeNull(&g_CatCache[relNum], recPtr, attr);
    }

    result = InsertRec(relNum, recPtr);

cleanup:
    free(provided);
    free(recPtr);
    return result;
}
