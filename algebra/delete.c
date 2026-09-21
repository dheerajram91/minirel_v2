/*
 * delete.c
 *
 *  Created on: 7-Oct-2014
 *      Author: Dheeraj
 */

#include "../include/delete.h"

/*
 * Function:  Delete() 
 * ------------------------
 * implements relational deletion command
 *
 * argv[0] = “delete”
 * argv[1] = source relation
 * argv[2] = attribute name
 * argv[3] = operator
 * argv[4] = value
 * argv[argc] = NIL
 *  
 *  returns: OK upon successfully performed deletion
 *           NOTOK: otherwise
 *
 * GLOBAL VARIABLES MODIFIED:
 *     <None> 
 *
 * ERRORS REPORTED:
 *     DB_NOT_OPEN
 *     METADATA_SECURITY
 *     RELNOEXIST
 *     ATTRNOEXIST
 * 
 * ALGORITHM:
 *  1. check for errors.
 *  2. Finds the details of the attribute of source relation, specified.
 *  3. Using FindRec() finds out matching Rids, and using deleterec, we remove those Rids
 *  4. Repeat step 3 till last matching record.
 *
 * IMPLEMENTATION NOTES:
 *  Using FindRelNum, FindRec, and DeleteRec from physical layer.
 *
 */

int Delete(int argc, char **argv) {
    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }
    int relNum, numAttrs, i, offset, attrFoundFlag = 0;
    int attrSize;
    struct attrCatalog* head;
    struct attrCatalog* conditionAttribute = NULL;
    datatype type;
    Rid startRid = { 1, 0 }, *foundRid;

    char *recPtr;
    char *conditionValue;
    bool conditionIsNull;

    if (argc < 5)
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);

    if ((strcmp(argv[0], "_delete") != 0)
            && (strcmp(argv[1], RELCAT) == 0 || strcmp(argv[1], ATTRCAT) == 0)) {
        return ErrorMsgs(METADATA_SECURITY, g_PrintFlag);
    }

    if (OpenRelWithLock(argv[1], LOCK_EXCLUSIVE) == NOTOK)
        return ErrorMsgs(RELNOEXIST, g_PrintFlag);
    /* Finding the relNum of Relation */
    relNum = FindRelNum(argv[1]);

    head = g_CatCache[relNum].attrList;
    numAttrs = g_CatCache[relNum].numAttrs;

    while (head != NULL) {
        /* This is to catch the desired attribute's specifications from Source relation. 
         Expected to happen only once in this loop */
        if (strcmp(head->attrName, argv[2]) == 0) {
            attrFoundFlag = 1;
            offset = head->offset;
            type = head->type;
            attrSize = head->length;
            conditionAttribute = head;
        }
        head = head->next;
    }
    /* Given attribute name never appeared in attr linkedlist */
    if (attrFoundFlag == 0)
        return ErrorMsgs(ATTRNOEXIST, g_PrintFlag);

    conditionValue = (char *) calloc(attrSize, 1);
    if (EncodeTextValue(
            conditionAttribute,
            argv[4],
            conditionValue,
            &conditionIsNull) != OK) {
        free(conditionValue);
        return NOTOK;
    }
    /* Finding record from Relation and deleting corresponding Rid Entry */
    while (FindRec(
            relNum,
            &startRid,
            &foundRid,
            &recPtr,
            type,
            attrSize,
            offset,
            conditionValue,
            readIntFromByteArray(argv[3], 0),
            conditionIsNull) == OK) {
        DeleteRec(relNum, foundRid);
        startRid = (*foundRid);
        free(foundRid);
    }
    free(conditionValue);
    return OK;
}
