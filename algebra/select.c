/*
 * select.c
 *
 *  Created on: 7-Oct-2014
 *      Author: Dheeraj
 */

#include "../include/select.h"

/*
 * Function:  Select() 
 * ------------------------
 * implements relational selection command
 *
 * argv[0] = “select”
 * argv[1] = result relation
 * argv[2] = source relation
 * argv[3] = attribute name
 * argv[4] = operator
 * argv[5] = value
 * argv[argc] = NIL
 *  
 * returns: OK upon successfully performed selection
 *           NOTOK: otherwise
 *
 * GLOBAL VARIABLES MODIFIED:
 *     <None>
 *
 * ERRORS REPORTED:
 *     ARGC_INSUFFICIENT
 *     RELNOEXIST
 *     ATTRNOEXIST
 *
 * ALGORITHM:
 *   1. Checks for Errors.
 *   2. Opens Source Relation and target relation
 *   3. Finds records satisfying given condition using FindRec
 *   4. Inserts into target relation, if such record is found
 *   5. Repeats 3-4 till last matching record of source relation.
 *
 * IMPLEMENTATION NOTES:
 *     Uses OpenRel, FindRelNum, FindRec, InsertRec from physical layer
 *     Uses Create from schema layer.
 *
 */

int Select(int argc, char **argv) {
    int relNum;
    int newRelNum;
    int numAttrs;
    int count;
    int retVal;
    int createArgCount;
    struct attrCatalog *head;
    struct attrCatalog *conditionAttribute = NULL;
    struct attrCatalog *sourceAttribute;
    struct attrCatalog *destinationAttribute;
    Rid startRid = { 1, 0 };
    Rid *foundRid;
    char **createArgumentList;
    char *recPtr;
    char *conditionValue;
    bool conditionIsNull;

    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }

    if (argc < 6)
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);

    relNum = OpenRel(argv[2]);
    if (relNum == NOTOK)
        return NOTOK;

    head = g_CatCache[relNum].attrList;
    numAttrs = g_CatCache[relNum].numAttrs;
    conditionAttribute = getAttrCatalog(head, argv[3]);
    if (conditionAttribute == NULL)
        return ErrorMsgs(ATTRNOEXIST, g_PrintFlag);

    createArgCount = (numAttrs + 1) * 2;
    createArgumentList = (char **) calloc(createArgCount, sizeof(char *));
    for (count = 0; count < createArgCount; count++)
        createArgumentList[count] = (char *) calloc(RELNAME, 1);

    strcpy(createArgumentList[0], "create");
    strcpy(createArgumentList[1], argv[1]);
    count = 2;
    while (head != NULL) {
        strcpy(createArgumentList[count], head->attrName);
        switch (head->type) {
            case INTEGER:
                strcpy(createArgumentList[count + 1], "i");
                break;
            case STRING:
                sprintf(createArgumentList[count + 1], "s%d", head->length);
                break;
            case FLOAT:
                strcpy(createArgumentList[count + 1], "f");
                break;
        }
        head = head->next;
        count = count + 2;
    }

    retVal = Create(createArgCount, createArgumentList);
    freeAllottedMem(createArgumentList, createArgCount);

    if (retVal == NOTOK)
        return NOTOK;

    newRelNum = OpenRelWithLock(argv[1], LOCK_EXCLUSIVE);
    if (newRelNum == NOTOK)
        return NOTOK;

    conditionValue = (char *) calloc(conditionAttribute->length, 1);
    if (EncodeTextValue(
            conditionAttribute,
            argv[5],
            conditionValue,
            &conditionIsNull) != OK) {
        free(conditionValue);
        return NOTOK;
    }

    while (FindRec(
            relNum,
            &startRid,
            &foundRid,
            &recPtr,
            conditionAttribute->type,
            conditionAttribute->length,
            conditionAttribute->offset,
            conditionValue,
            readIntFromByteArray(argv[4], 0),
            conditionIsNull) == OK) {
        char *destinationRecord = (char *) calloc(
                g_CatCache[newRelNum].recLength, 1);
        sourceAttribute = g_CatCache[relNum].attrList;
        destinationAttribute = g_CatCache[newRelNum].attrList;
        while (sourceAttribute != NULL && destinationAttribute != NULL) {
            if (CopyRecordAttribute(
                    &g_CatCache[newRelNum],
                    destinationRecord,
                    destinationAttribute,
                    &g_CatCache[relNum],
                    recPtr,
                    sourceAttribute) != OK) {
                free(destinationRecord);
                free(foundRid);
                free(conditionValue);
                return NOTOK;
            }
            sourceAttribute = sourceAttribute->next;
            destinationAttribute = destinationAttribute->next;
        }
        if (InsertRec(newRelNum, destinationRecord) != OK) {
            free(destinationRecord);
            free(foundRid);
            free(conditionValue);
            return NOTOK;
        }
        free(destinationRecord);
        startRid = (*foundRid);
        free(foundRid);
    }
    free(conditionValue);
    return OK;
}
