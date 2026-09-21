/*
 * closerel.c
 *
 *  Created on: 31-Oct-2014
 *      Author: Dheeraj
 */

#include "../include/closerel.h"

/*
 * Function: CloseRel() 
 * ----------------------
 * Close the relation identified by relNum after writing to disk any changes in its buffer page or 
 * in its cached catalog information
 *
 *  relNum : relation number
 *
 *  returns: OK on success
 *           NOTOK on failure
 *
 * GLOBAL VARIABLES MODIFIED:
 *      g_CatCache[relNum].dirty = FALSE
 *      g_CacheInUse[relNum] = FALSE;
 *      g_CacheTimestamp[relNum] = 0;
 *
 * ERRORS REPORTED:
 *      RELNOEXIST
 *
 * ALGORITHM:
 *   1. Checks for errors.
 *   2. FlushPage current bufferpage, which will update disk if buffer is dirty
 *   3. checks if cache entry is dirty
 *   4. if yes, update relcat entries for corresponding entry.
 *
 * IMPLEMENTATION NOTES:
 *      Uses FlushPage, FindRec, WriteRec from physical layer.
 */

int CloseRel(int relNum) {
    int numPgs, numRecs;
    char *recPtr;
    RelCatalogRecord relationRecord;
    Rid startRid = { 1, 0 }, *foundRid;
    int catalogLock = NOTOK;

    FlushPage(relNum);

    if (g_CatCache[relNum].dirty == TRUE) {
        catalogLock = AcquireManagedCatalogLock(LOCK_EXCLUSIVE);
        if (catalogLock == NOTOK) {
            return ReportLockFailure();
        }
        if (relNum == ATTRCAT_CACHE
                && RefreshCatalogCache(ATTRCAT_CACHE) != OK) {
            ReleaseManagedLock(catalogLock);
            return NOTOK;
        }
        if (RefreshCatalogCache(RELCAT_CACHE) != OK) {
            ReleaseManagedLock(catalogLock);
            return NOTOK;
        }
        if (FindRec(RELCAT_CACHE, &startRid, &foundRid, &recPtr, STRING, RELNAME, 0,
                g_CatCache[relNum].relName, EQ, FALSE) == NOTOK) {
            if (catalogLock != NOTOK) {
                ReleaseManagedLock(catalogLock);
            }
            return ErrorMsgs(RELNOEXIST, g_PrintFlag);
        } else {
            numPgs = g_CatCache[relNum].numPgs;
            numRecs = g_CatCache[relNum].numRecs;
            DecodeRelCatalogRecord(recPtr, &relationRecord);
            relationRecord.numRecs = numRecs;
            relationRecord.numPgs = numPgs;
            EncodeRelCatalogRecord(recPtr, &relationRecord);

            WriteRec(RELCAT_CACHE, recPtr, foundRid);
            FlushPage(RELCAT_CACHE);
            g_CatCache[relNum].dirty = FALSE;
            if (catalogLock != NOTOK) {
                ReleaseManagedLock(catalogLock);
            }
        }
    }

    struct attrCatalog *temp, *attrListHead = g_CatCache[relNum].attrList;
    g_CacheInUse[relNum] = FALSE;
    close(g_CatCache[relNum].relFile);

    temp = attrListHead;
    while (temp != NULL) {
        attrListHead = temp->next;
        free(temp);
        temp = attrListHead;
    }
    g_CatCache[relNum].attrList = NULL;

    if (relNum >= 2 && g_CatCache[relNum].lockId != NOTOK) {
        ReleaseManagedLock(g_CatCache[relNum].lockId);
        g_CatCache[relNum].lockId = NOTOK;
        g_CatCache[relNum].lockMode = LOCK_NONE;
    }
    return OK;
}
