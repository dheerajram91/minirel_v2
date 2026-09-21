#include "../include/findrec.h"

/**
 * Starting at record startRid in relation relNum, find the RID of the next
 * record that meets the specification.
 *
 * @param relNum
 * @param startRid   - The search will begin with the first record after startRid.
 * @param foundRid   - The record identifier of the first record after that specified by startRid
 *                     NULL if not found
 * @param recPtr     - A pointer to a record-sized byte array into which the contents of the
 *                     matching record (if any) will be put.
 * @param attrType   - The data type (i, f or s) of the search attribute.
 * @param attrSize   - Length of search attribute in bytes
 * @param attrOffset - The offset into the record at which the search attribute is located.
 * @param valuePtr   - A pointer to a byte array which contains the search value
 * @param compOp     - Comparison operator
 * @return OK or NOTOK
 *
 * @author nithin
 *
 * GLOBAL VARIABLES MODIFIED:
 *      <none>
 *
 * ERRORS REPORTED:
 *      INVALID_ATTR_TYPE
 *
 * ALGORITHM:
 *      1. Read the page of relNum containing Rid. Call ReadPage()
 *      2. For each record from startRid
 *      3.      Compare the corresponding attribute with given value
 *      4.      Return if match is found
 *
 * IMPLEMENTATION NOTES:
 *      Uses ReadPage(), GetNextRec()
 */
int FindRec(int relNum, Rid *startRid, Rid **foundRid, char **recPtr, const char attrType,
        const int attrSize, const int attrOffset, const char *valuePtr, const int compOp,
        const bool valueIsNull) {
    unsigned short pid = startRid->pid;
    struct attrCatalog *attribute;
    if (ReadPage(relNum, pid) != OK) {
        return NOTOK;
    }
    int intAttr, intAttr2;
    float floatAttr, floatAttr2;
    char *stringAttr, *stringAttr2;
    stringAttr = (char *) calloc(attrSize + 1, sizeof(char));
    stringAttr2 = (char *) calloc(attrSize + 1, sizeof(char));
    *foundRid = NULL;
    attribute = g_CatCache[relNum].attrList;
    while (attribute != NULL && attribute->offset != (unsigned int) attrOffset) {
        attribute = attribute->next;
    }

    while (GetNextRec(relNum, startRid, foundRid, recPtr) == OK) {
        bool recordIsNull = attribute != NULL
                && RecordAttributeIsNull(
                        &g_CatCache[relNum], *recPtr, attribute) == TRUE;
        if (valueIsNull == TRUE) {
            if ((compOp == EQ && recordIsNull == TRUE)
                    || (compOp == NEQ && recordIsNull == FALSE)) {
                free(stringAttr);
                free(stringAttr2);
                return OK;
            }
            startRid = *foundRid;
            continue;
        }
        if (recordIsNull == TRUE) {
            startRid = *foundRid;
            continue;
        }
        switch (attrType) {
            case INTEGER:
                intAttr = readIntFromByteArray(valuePtr, 0);
                intAttr2 = readIntFromByteArray(*recPtr, attrOffset);
                if (compareNum((float) intAttr2, (float) intAttr, compOp)) {
                    free(stringAttr);
                    free(stringAttr2);
                    return OK;
                }
                break;
            case FLOAT:
                floatAttr = readFloatFromByteArray(valuePtr, 0);
                floatAttr2 = readFloatFromByteArray(*recPtr, attrOffset);
                if (compareNum(floatAttr2, floatAttr, compOp)) {
                    free(stringAttr);
                    free(stringAttr2);
                    return OK;
                }
                break;
            case STRING:
                readStringFromByteArray(stringAttr, valuePtr, 0, attrSize);
                readStringFromByteArray(stringAttr2, *recPtr, attrOffset, attrSize);
                if (compareStrings(stringAttr2, stringAttr, compOp)) {
                    free(stringAttr);
                    free(stringAttr2);
                    return OK;
                }
                break;
            default:
                free(stringAttr);
                free(stringAttr2);
                return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
                break;
        }
        startRid = *foundRid;
    }
    free(stringAttr);
    free(stringAttr2);
    return NOTOK;
}
