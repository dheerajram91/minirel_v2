#include "../include/catalogcache.h"

int RefreshCatalogCache(int relNum) {
    if (FlushPage(relNum) != OK) {
        return NOTOK;
    }

    int fd = g_CatCache[relNum].relFile;
    long fileSize = lseek(fd, 0, SEEK_END);
    if (fileSize < 0) {
        return ErrorMsgs(FILE_SEEK_ERROR, g_PrintFlag);
    }

    unsigned int pageCount = (unsigned int) (fileSize / PAGESIZE);
    unsigned int recordCount = 0;
    int pid;
    for (pid = 1; pid <= pageCount; pid++) {
        char slotMapBytes[PAGESIZE - MAXRECORD];
        if (lseek(fd, (pid - 1) * PAGESIZE, SEEK_SET) < 0) {
            return ErrorMsgs(FILE_SEEK_ERROR, g_PrintFlag);
        }
        if (read(fd, slotMapBytes, sizeof(slotMapBytes))
                != sizeof(slotMapBytes)) {
            return ErrorMsgs(READ_DISK_ERROR, g_PrintFlag);
        }

        unsigned int slotMap = readIntFromByteArray(slotMapBytes, 0);
        unsigned int slot;
        for (slot = 1; slot <= g_CatCache[relNum].recsPerPg; slot++) {
            if (slotMap & (1U << (32 - slot))) {
                recordCount++;
            }
        }
    }

    g_CatCache[relNum].numPgs = pageCount;
    g_CatCache[relNum].numRecs = recordCount;
    g_Buffer[relNum].pid = 0;
    g_Buffer[relNum].dirty = FALSE;
    g_Buffer[relNum].page.slotmap = 0;
    g_Buffer[relNum].beforeImageValid = FALSE;
    return OK;
}

int RefreshCatalogCaches() {
    if (RefreshCatalogCache(RELCAT_CACHE) != OK) {
        return NOTOK;
    }
    return RefreshCatalogCache(ATTRCAT_CACHE);
}
