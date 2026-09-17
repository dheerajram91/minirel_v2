#include "../include/transaction.h"

#include "../include/closecats.h"
#include "../include/error.h"
#include "../include/globals.h"
#include "../include/opencats.h"
#include "../include/wal.h"

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#endif

#define MAX_TRANSACTION_LOCKS (MAXOPEN + 4)
#define MAX_TRANSACTION_BACKUPS (MAXOPEN + 2)

typedef struct transactionLock {
    bool inUse;
    bool catalog;
    char resource[RELNAME];
    int lockId;
    LockMode mode;
} TransactionLock;

typedef struct transactionBackup {
    bool inUse;
    bool existed;
    char resource[RELNAME];
    char backupPath[MAXPATH];
} TransactionBackup;

static bool g_TransactionActive = FALSE;
static bool g_TransactionFailed = FALSE;
static TransactionLock g_TransactionLocks[MAX_TRANSACTION_LOCKS];
static TransactionBackup g_TransactionBackups[MAX_TRANSACTION_BACKUPS];

static int copyFile(const char *source, const char *destination) {
    int sourceFd = open(source, O_RDONLY);
    if (sourceFd < 0) {
        return NOTOK;
    }
    int destinationFd = open(
            destination,
            O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR);
    if (destinationFd < 0) {
        close(sourceFd);
        return NOTOK;
    }

    char buffer[4096];
    int result = OK;
    ssize_t bytesRead;
    while ((bytesRead = read(sourceFd, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < bytesRead) {
            ssize_t count = write(
                    destinationFd,
                    buffer + written,
                    bytesRead - written);
            if (count <= 0) {
                result = NOTOK;
                break;
            }
            written += count;
        }
        if (result != OK) {
            break;
        }
    }
    if (bytesRead < 0) {
        result = NOTOK;
    }

    if (result == OK) {
#ifdef _WIN32
        if (_commit(destinationFd) != 0) {
            result = NOTOK;
        }
#else
        if (fsync(destinationFd) != 0) {
            result = NOTOK;
        }
#endif
    }
    close(destinationFd);
    close(sourceFd);
    return result;
}

static TransactionLock *findTransactionLock(
        const char *resource,
        bool catalog) {
    int i;
    for (i = 0; i < MAX_TRANSACTION_LOCKS; i++) {
        if (g_TransactionLocks[i].inUse == TRUE
                && g_TransactionLocks[i].catalog == catalog
                && strcmp(g_TransactionLocks[i].resource, resource) == 0) {
            return &g_TransactionLocks[i];
        }
    }
    return NULL;
}

static TransactionLock *newTransactionLock() {
    int i;
    for (i = 0; i < MAX_TRANSACTION_LOCKS; i++) {
        if (g_TransactionLocks[i].inUse == FALSE) {
            return &g_TransactionLocks[i];
        }
    }
    return NULL;
}

static TransactionBackup *findBackup(const char *resource) {
    int i;
    for (i = 0; i < MAX_TRANSACTION_BACKUPS; i++) {
        if (g_TransactionBackups[i].inUse == TRUE
                && strcmp(g_TransactionBackups[i].resource, resource) == 0) {
            return &g_TransactionBackups[i];
        }
    }
    return NULL;
}

static int prepareBackup(const char *resource) {
    if (findBackup(resource) != NULL) {
        return OK;
    }

    int i;
    for (i = 0; i < MAX_TRANSACTION_BACKUPS; i++) {
        if (g_TransactionBackups[i].inUse == FALSE) {
            break;
        }
    }
    if (i == MAX_TRANSACTION_BACKUPS) {
        return NOTOK;
    }

    TransactionBackup *backup = &g_TransactionBackups[i];
    backup->inUse = TRUE;
    strncpy(backup->resource, resource, RELNAME - 1);
    backup->resource[RELNAME - 1] = '\0';
    snprintf(
            backup->backupPath,
            sizeof(backup->backupPath),
            ".minirel-tx-%d-%s.bak",
            (int) getpid(),
            resource);

    int sourceFd = open(resource, O_RDONLY);
    if (sourceFd < 0) {
        if (errno != ENOENT) {
            backup->inUse = FALSE;
            return NOTOK;
        }
        backup->existed = FALSE;
        return WalLogBackup(resource, backup->backupPath, FALSE);
    }
    close(sourceFd);
    backup->existed = TRUE;
    if (copyFile(resource, backup->backupPath) != OK) {
        return NOTOK;
    }
    return WalLogBackup(resource, backup->backupPath, TRUE);
}

static int prepareCatalogBackups() {
    if (prepareBackup(RELCAT) != OK) {
        return NOTOK;
    }
    return prepareBackup(ATTRCAT);
}

static int acquireTransactionLock(
        const char *resource,
        bool catalog,
        LockMode mode) {
    TransactionLock *existing = findTransactionLock(resource, catalog);
    if (existing != NULL) {
        if (existing->mode < mode) {
            if (UpgradeLock(existing->lockId, mode) != OK) {
                TransactionMarkLockFailure();
                return NOTOK;
            }
            existing->mode = mode;
        }
        return existing->lockId;
    }

    int lockId = catalog
            ? AcquireCatalogLock(mode)
            : AcquireTableLock(resource, mode);
    if (lockId == NOTOK) {
        TransactionMarkLockFailure();
        return NOTOK;
    }

    TransactionLock *entry = newTransactionLock();
    if (entry == NULL) {
        ReleaseLock(lockId);
        return NOTOK;
    }
    entry->inUse = TRUE;
    entry->catalog = catalog;
    strncpy(entry->resource, resource, RELNAME - 1);
    entry->resource[RELNAME - 1] = '\0';
    entry->lockId = lockId;
    entry->mode = mode;
    return lockId;
}

static void discardCaches() {
    int i;
    for (i = 0; i < MAXOPEN; i++) {
        if (g_CacheInUse[i] == TRUE) {
            close(g_CatCache[i].relFile);
            struct attrCatalog *attribute = g_CatCache[i].attrList;
            while (attribute != NULL) {
                struct attrCatalog *next = attribute->next;
                free(attribute);
                attribute = next;
            }
        }
        g_CacheInUse[i] = FALSE;
        g_CatCache[i].attrList = NULL;
        g_Buffer[i].dirty = FALSE;
        g_Buffer[i].pid = 0;
        g_Buffer[i].page.slotmap = 0;
        g_Buffer[i].beforeImageValid = FALSE;
    }
}

static void releaseTransactionLocks() {
    int i;
    for (i = MAX_TRANSACTION_LOCKS - 1; i >= 0; i--) {
        if (g_TransactionLocks[i].inUse == TRUE) {
            ReleaseLock(g_TransactionLocks[i].lockId);
        }
    }
    memset(g_TransactionLocks, 0, sizeof(g_TransactionLocks));
}

static void removeBackups() {
    int i;
    for (i = 0; i < MAX_TRANSACTION_BACKUPS; i++) {
        if (g_TransactionBackups[i].inUse == TRUE
                && g_TransactionBackups[i].existed == TRUE) {
            remove(g_TransactionBackups[i].backupPath);
        }
    }
    memset(g_TransactionBackups, 0, sizeof(g_TransactionBackups));
}

bool TransactionIsActive() {
    return g_TransactionActive;
}

bool TransactionOwnsLock(int lockId) {
    int i;
    if (g_TransactionActive == FALSE) {
        return FALSE;
    }
    for (i = 0; i < MAX_TRANSACTION_LOCKS; i++) {
        if (g_TransactionLocks[i].inUse == TRUE
                && g_TransactionLocks[i].lockId == lockId) {
            return TRUE;
        }
    }
    return FALSE;
}

void TransactionMarkFailure() {
    if (g_TransactionActive == TRUE) {
        g_TransactionFailed = TRUE;
    }
}

void TransactionMarkLockFailure() {
    if (g_TransactionActive == TRUE && LockTimedOut() == TRUE) {
        g_TransactionFailed = TRUE;
    }
}

int AcquireManagedTableLock(const char *relName, LockMode mode) {
    if (g_TransactionActive == FALSE) {
        return AcquireTableLock(relName, mode);
    }

    int lockId = acquireTransactionLock(relName, FALSE, mode);
    if (lockId != NOTOK && mode == LOCK_EXCLUSIVE
            && prepareBackup(relName) != OK) {
        g_TransactionFailed = TRUE;
        return NOTOK;
    }
    return lockId;
}

int AcquireManagedCatalogLock(LockMode mode) {
    if (g_TransactionActive == FALSE) {
        return AcquireCatalogLock(mode);
    }

    TransactionLock *existing = findTransactionLock("__catalog__", TRUE);
    if (existing != NULL) {
        if (existing->mode < mode) {
            if (UpgradeLock(existing->lockId, mode) != OK) {
                TransactionMarkLockFailure();
                return NOTOK;
            }
            existing->mode = mode;
        }
        return existing->lockId;
    }
    if (mode == LOCK_SHARED) {
        return AcquireCatalogLock(mode);
    }

    int lockId = acquireTransactionLock("__catalog__", TRUE, mode);
    if (lockId != NOTOK && mode == LOCK_EXCLUSIVE
            && prepareCatalogBackups() != OK) {
        g_TransactionFailed = TRUE;
        return NOTOK;
    }
    return lockId;
}

int ReleaseManagedLock(int lockId) {
    if (TransactionOwnsLock(lockId) == TRUE) {
        return OK;
    }
    return ReleaseLock(lockId);
}

int BeginTransaction(int argc, char **argv) {
    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }
    if (g_TransactionActive == TRUE) {
        return ErrorMsgs(TRANSACTION_ACTIVE, g_PrintFlag);
    }

    if (CloseCats() != OK || OpenCats() != OK) {
        return NOTOK;
    }
    memset(g_TransactionLocks, 0, sizeof(g_TransactionLocks));
    memset(g_TransactionBackups, 0, sizeof(g_TransactionBackups));
    g_TransactionFailed = FALSE;
    if (WalBeginTransaction() != OK) {
        return ErrorMsgs(WAL_WRITE_ERROR, g_PrintFlag);
    }
    g_TransactionActive = TRUE;
    return OK;
}

int CommitTransaction(int argc, char **argv) {
    if (g_TransactionActive == FALSE) {
        return ErrorMsgs(NO_ACTIVE_TRANSACTION, g_PrintFlag);
    }
    if (g_TransactionFailed == TRUE) {
        RollbackTransaction(0, NULL);
        return ErrorMsgs(TRANSACTION_ABORTED, g_PrintFlag);
    }

    if (CloseCats() != OK) {
        g_TransactionFailed = TRUE;
        return NOTOK;
    }
    if (WalCommitTransaction() != OK) {
        g_TransactionFailed = TRUE;
        return ErrorMsgs(WAL_WRITE_ERROR, g_PrintFlag);
    }
    WalCrashPoint("after_commit_log");

    g_TransactionActive = FALSE;
    releaseTransactionLocks();
    removeBackups();
    return OpenCats();
}

int RollbackTransaction(int argc, char **argv) {
    if (g_TransactionActive == FALSE) {
        return ErrorMsgs(NO_ACTIVE_TRANSACTION, g_PrintFlag);
    }

    discardCaches();
    int i;
    int result = OK;
    for (i = 0; i < MAX_TRANSACTION_BACKUPS; i++) {
        TransactionBackup *backup = &g_TransactionBackups[i];
        if (backup->inUse == FALSE) {
            continue;
        }
        if (backup->existed == TRUE) {
            if (copyFile(backup->backupPath, backup->resource) != OK) {
                result = NOTOK;
            }
        } else {
            remove(backup->resource);
        }
    }

    if (WalAbortTransaction() != OK) {
        result = NOTOK;
    }
    g_TransactionActive = FALSE;
    releaseTransactionLocks();
    removeBackups();
    if (OpenCats() != OK) {
        result = NOTOK;
    }
    if (result != OK) {
        return ErrorMsgs(TRANSACTION_ROLLBACK_FAILED, g_PrintFlag);
    }
    return OK;
}
