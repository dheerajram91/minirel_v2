/*
 * closecats.c
 *
 *  Created on: 1-Nov-2014
 *      Author: Dheeraj
 */

#include "../include/closecats.h"

/*
 * Function: CloseCats() 
 * ----------------------
 * Closes catalog files
 *
 *  returns: OK on success
 *           NOTOK on failure
 *
 * GLOBAL VARIABLES MODIFIED:
 *      <None>
 *
 * ERRORS REPORTED:
 *      <None>
 * 
 * ALGORITHM:
 *   1. If any of the relation is not closed, call CloseRel for that relation.
 *   2. Call CloseRel for Attrcat
 *   3. Call CloseRel for Relcat
 *
 * IMPLEMENTATION NOTES:
 *      Uses CloseRel from physical layer.
 * 
 */

int CloseCats() {
    int i, found;

    if (g_CacheInUse[0] == FALSE || g_CacheInUse[1] == FALSE)
        return NOTOK;

    do {
        found = FALSE;
        for (i = 2; i < MAXOPEN; i++) {
            if (g_CacheInUse[i] == TRUE) {
                if (CloseRel(i) != OK) {
                    return NOTOK;
                }
                found = TRUE;
                break;
            }
        }
    } while (found == TRUE);
    /* Closing Relation attrcat */
    if (CloseRel(ATTRCAT_CACHE) != OK) {
        return NOTOK;
    }
    /* Closing Relation relcat */
    if (CloseRel(RELCAT_CACHE) != OK) {
        return NOTOK;
    }

    return OK;
}
