#include "../include/locking.h"
#include "../include/globals.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define FLOAT WINDOWS_FLOAT
#define LOCK_EXCLUSIVE WINDOWS_LOCK_EXCLUSIVE
#include <windows.h>
#undef LOCK_EXCLUSIVE
#undef FLOAT
#else
#include <unistd.h>
#endif

#define MAX_LOCKS (MAXOPEN + 4)
#define CATALOG_LOCK_NAME "__catalog__"
#define LOCK_TIMEOUT_MS 5000
#define LOCK_RETRY_MS 10

typedef struct lockEntry {
    bool inUse;
    char resource[RELNAME];
    int fd;
    LockMode mode;
    unsigned int references;
} LockEntry;

static LockEntry g_Locks[MAX_LOCKS];
static bool g_LockTimedOut = FALSE;

static int lockFile(int fd, LockMode mode) {
    int waitedMs = 0;
    g_LockTimedOut = FALSE;
#ifdef _WIN32
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
    if (mode == LOCK_EXCLUSIVE) {
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return NOTOK;
    }
    while (waitedMs <= LOCK_TIMEOUT_MS) {
        OVERLAPPED overlapped = { 0 };
        if (LockFileEx(handle, flags, 0, 1, 0, &overlapped)) {
            return OK;
        }
        if (GetLastError() != ERROR_LOCK_VIOLATION) {
            return NOTOK;
        }
        Sleep(LOCK_RETRY_MS);
        waitedMs += LOCK_RETRY_MS;
    }
#else
    while (waitedMs <= LOCK_TIMEOUT_MS) {
        struct flock lock = { 0 };
        lock.l_type = mode == LOCK_EXCLUSIVE ? F_WRLCK : F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 1;
        if (fcntl(fd, F_SETLK, &lock) != -1) {
            return OK;
        }
        if (errno != EACCES && errno != EAGAIN) {
            return NOTOK;
        }
        usleep(LOCK_RETRY_MS * 1000);
        waitedMs += LOCK_RETRY_MS;
    }
#endif
    g_LockTimedOut = TRUE;
    return NOTOK;
}

static int unlockFile(int fd) {
#ifdef _WIN32
    OVERLAPPED overlapped = { 0 };
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE
            || !UnlockFileEx(handle, 0, 1, 0, &overlapped)) {
        return NOTOK;
    }
#else
    struct flock lock = { 0 };
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 1;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        return NOTOK;
    }
#endif
    return OK;
}

static int acquireLock(const char *resource, const char *fileName, LockMode mode) {
    int i;
    for (i = 0; i < MAX_LOCKS; i++) {
        if (g_Locks[i].inUse == TRUE
                && strcmp(g_Locks[i].resource, resource) == 0) {
            if (g_Locks[i].mode < mode) {
                if (g_Locks[i].references != 1
                        || UpgradeLock(i, mode) != OK) {
                    return NOTOK;
                }
            }
            g_Locks[i].references++;
            return i;
        }
    }

    for (i = 0; i < MAX_LOCKS && g_Locks[i].inUse == TRUE; i++) {
    }
    if (i == MAX_LOCKS) {
        return NOTOK;
    }

    int fd = open(fileName, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0 || lockFile(fd, mode) != OK) {
        if (fd >= 0) {
            close(fd);
        }
        return NOTOK;
    }

    g_Locks[i].inUse = TRUE;
    strncpy(g_Locks[i].resource, resource, RELNAME - 1);
    g_Locks[i].resource[RELNAME - 1] = '\0';
    g_Locks[i].fd = fd;
    g_Locks[i].mode = mode;
    g_Locks[i].references = 1;
    return i;
}

static int acquireWriterGate(const char *resource) {
    char fileName[RELNAME + 25];
    snprintf(fileName, sizeof(fileName), ".minirel-writer-%s.lock", resource);
    int fd = open(fileName, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return NOTOK;
    }
    if (lockFile(fd, LOCK_EXCLUSIVE) != OK) {
        close(fd);
        return NOTOK;
    }
    return fd;
}

static void releaseWriterGate(int fd) {
    if (fd >= 0) {
        unlockFile(fd);
        close(fd);
    }
}

int AcquireTableLock(const char *relName, LockMode mode) {
    int i;
    for (i = 0; i < MAX_LOCKS; i++) {
        if (g_Locks[i].inUse == TRUE
                && strcmp(g_Locks[i].resource, relName) == 0) {
            if (g_Locks[i].mode < mode) {
                if (g_Locks[i].references != 1
                        || UpgradeLock(i, mode) != OK) {
                    return NOTOK;
                }
            }
            g_Locks[i].references++;
            return i;
        }
    }

    char fileName[RELNAME + 24];
    snprintf(fileName, sizeof(fileName), ".minirel-table-%s.lock", relName);
    int writerGate = mode == LOCK_EXCLUSIVE
            ? acquireWriterGate(relName)
            : NOTOK;
    if (mode == LOCK_EXCLUSIVE && writerGate == NOTOK) {
        return NOTOK;
    }
    int result = acquireLock(relName, fileName, mode);
    releaseWriterGate(writerGate);
    return result;
}

int AcquireCatalogLock(LockMode mode) {
    return acquireLock(CATALOG_LOCK_NAME, ".minirel-catalog.lock", mode);
}

int UpgradeLock(int lockId, LockMode mode) {
    if (lockId < 0 || lockId >= MAX_LOCKS || g_Locks[lockId].inUse == FALSE) {
        return NOTOK;
    }
    if (g_Locks[lockId].mode >= mode) {
        return OK;
    }

    LockMode previousMode = g_Locks[lockId].mode;
    int writerGate = strcmp(g_Locks[lockId].resource, CATALOG_LOCK_NAME) == 0
            ? NOTOK
            : acquireWriterGate(g_Locks[lockId].resource);
    if (strcmp(g_Locks[lockId].resource, CATALOG_LOCK_NAME) != 0
            && writerGate == NOTOK) {
        return NOTOK;
    }
    if (unlockFile(g_Locks[lockId].fd) != OK) {
        releaseWriterGate(writerGate);
        return NOTOK;
    }
    if (lockFile(g_Locks[lockId].fd, mode) != OK) {
        bool timedOut = g_LockTimedOut;
        lockFile(g_Locks[lockId].fd, previousMode);
        g_LockTimedOut = timedOut;
        releaseWriterGate(writerGate);
        return NOTOK;
    }
    g_Locks[lockId].mode = mode;
    releaseWriterGate(writerGate);
    return OK;
}

int ReleaseLock(int lockId) {
    if (lockId < 0 || lockId >= MAX_LOCKS || g_Locks[lockId].inUse == FALSE) {
        return NOTOK;
    }

    if (--g_Locks[lockId].references > 0) {
        return OK;
    }

    int result = unlockFile(g_Locks[lockId].fd);
    close(g_Locks[lockId].fd);
    memset(&g_Locks[lockId], 0, sizeof(LockEntry));
    return result;
}

bool LockTimedOut() {
    return g_LockTimedOut;
}

int ReportLockFailure() {
    return ErrorMsgs(
            g_LockTimedOut == TRUE ? LOCK_TIMEOUT : FILE_SYSTEM_ERROR,
            g_PrintFlag);
}
