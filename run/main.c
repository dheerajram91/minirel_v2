#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "../include/globals.h"

extern parser();

static int UseDatabaseDirectory() {
    const char *dataDirectory = getenv("MINIREL_DATA_DIR");
    if (dataDirectory == NULL || dataDirectory[0] == '\0') {
        dataDirectory = "DB";
    }
#ifdef _WIN32
    if (mkdir(dataDirectory) != 0 && errno != EEXIST) {
#else
    if (mkdir(
                dataDirectory,
                S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0
            && errno != EEXIST) {
#endif
        return NOTOK;
    }
    return chdir(dataDirectory) == 0 ? OK : NOTOK;
}

int main() {
    if (UseDatabaseDirectory() != OK) {
        fprintf(stderr, "Unable to open the MINIREL database directory.\n");
        return 1;
    }
    printf("Welcome to MINIREL database system\n\n");
    /* Initialize the global variables */
    g_DBOpenFlag = NOTOK;
    g_PrintFlag = OK;
    /*-------------------------------- */
    parser();
    printf("\n\nGoodbye from MINIREL\n");
    return 0;
}
