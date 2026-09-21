#include "../include/createdb.h"

/**
 * Creates a new database. It creates the relation and attribute catalogs and loads them with
 * the appropriate initial information.
 *
 * @param argc
 * @param argv
 * @return OK or NOTOK
 */
int CreateDB(int argc, char **argv) {
    static const char ignoreContents[] =
            "# MINIREL runtime database files\n*\n";
    FILE *ignoreFile;
    size_t ignoreBytesWritten;
    int ignoreCloseResult;

    if (argc < 2) {
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);
    }
    if (g_DBOpenFlag == OK) {
        return ErrorMsgs(DB_NOT_CLOSED, g_PrintFlag);
    }

    char dbName[RELNAME], dbPath[MAXPATH];
    separateDBPath(argv[1], dbPath, dbName);

    /* Stores the current working directory to be changed once done */
    getcwd(g_InvokedDirectory, MAXPATH);

    chdir(dbPath);

#ifdef _WIN32
    if (mkdir(dbName) == -1) {
#else
    if (mkdir(dbName, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
#endif
        chdir(g_InvokedDirectory);
        if (errno == EEXIST) {
            return ErrorMsgs(DB_ALREADY_EXISTS, g_PrintFlag);
        } else {
            return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
        }
    }
    chdir(dbName);

    ignoreFile = fopen(".gitignore", "wb");
    if (ignoreFile == NULL) {
        chdir(g_InvokedDirectory);
        rmdir(argv[1]);
        return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
    }
    ignoreBytesWritten = fwrite(
            ignoreContents,
            1,
            sizeof(ignoreContents) - 1,
            ignoreFile);
    ignoreCloseResult = fclose(ignoreFile);
    if (ignoreBytesWritten != sizeof(ignoreContents) - 1
            || ignoreCloseResult != 0) {
        remove(".gitignore");
        chdir(g_InvokedDirectory);
        rmdir(argv[1]);
        return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
    }

    g_DBOpenFlag = OK;
    int returnflag = CreateCats();

    g_DBOpenFlag = NOTOK;
    chdir(g_InvokedDirectory);

    return returnflag;
}
