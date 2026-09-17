#include "../include/closedb.h"
#include "../include/wal.h"

/**
 * Closes the currently opened database and changes
 * the working directory to the original directory from which MINIREL was invoked
 * argv[0] - "closedb"
 * argv[argc] - NIL
 *
 * @param argc
 * @param argv
 * @return OK or NOTOK
 *
 * @author nithin
 *
 * GLOBAL VARIABLES MODIFIED
 *      g_DBOpenFlag
 *
 * ERRORS REPORTED
 *      DB_NOT_OPEN
 *
 * ALGORITHM
 *      1. Check if some db is opened
 *      2. Call CloseCats() to close everything
 *      3. Change the directory to the invoked directory
 *
 * IMPLEMENTATION NOTES:
 *      Uses CloseCats()
 *
 */
int CloseDB(int argc, char **argv) {
    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }
    if (TransactionIsActive() == TRUE) {
        return ErrorMsgs(TRANSACTION_ACTIVE, g_PrintFlag);
    }
    if (CloseCats() == OK) {
        int walResult = WalCloseDatabase();
        g_DBOpenFlag = NOTOK;
        chdir(g_InvokedDirectory);
        if (walResult != OK) {
            return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
        }
        return OK;
    } else {
        return NOTOK;
    }
}
