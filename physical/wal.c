#include "../include/wal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define WAL_BINARY_FLAG O_BINARY
#define FLOAT WINDOWS_FLOAT
#define LOCK_EXCLUSIVE WINDOWS_LOCK_EXCLUSIVE
#include <windows.h>
#undef LOCK_EXCLUSIVE
#undef FLOAT
#else
#include <stdint.h>
#define WAL_BINARY_FLAG 0
#endif

#define WAL_FILE ".minirel.wal"
#define WAL_LOCK_FILE ".minirel-wal.lock"
#define DATABASE_LOCK_FILE ".minirel-database.lock"
#define WAL_MAGIC 0x4d52574cU
#define WAL_VERSION 1
#define WAL_HEADER_SIZE 68
#define WAL_MAX_RECORD_SIZE (WAL_HEADER_SIZE + PAGESIZE * 2)
#define WAL_LOCK_TIMEOUT_MS 5000
#define WAL_LOCK_RETRY_MS 10

#define WAL_BEGIN 1
#define WAL_PAGE 2
#define WAL_BACKUP 3
#define WAL_COMMIT 4
#define WAL_ABORT 5
#define WAL_DROP 6

typedef struct walRecord {
    unsigned short type;
    unsigned long long lsn;
    unsigned long long transactionId;
    unsigned long long previousLsn;
    char relation[RELNAME];
    unsigned int pageId;
    unsigned int payloadLength;
    unsigned char *payload;
} WalRecord;

static int g_DatabaseLockFd = NOTOK;
static unsigned long long g_TransactionId = 0;
static unsigned long long g_PreviousLsn = 0;
static bool g_WalTransactionActive = FALSE;

static void put16(unsigned char *target, unsigned short value) {
    target[0] = (unsigned char) value;
    target[1] = (unsigned char) (value >> 8);
}

static void put32(unsigned char *target, unsigned int value) {
    int i;
    for (i = 0; i < 4; i++) {
        target[i] = (unsigned char) (value >> (i * 8));
    }
}

static void put64(unsigned char *target, unsigned long long value) {
    int i;
    for (i = 0; i < 8; i++) {
        target[i] = (unsigned char) (value >> (i * 8));
    }
}

static unsigned short get16(const unsigned char *source) {
    return (unsigned short) source[0]
            | ((unsigned short) source[1] << 8);
}

static unsigned int get32(const unsigned char *source) {
    unsigned int value = 0;
    int i;
    for (i = 0; i < 4; i++) {
        value |= ((unsigned int) source[i]) << (i * 8);
    }
    return value;
}

static unsigned long long get64(const unsigned char *source) {
    unsigned long long value = 0;
    int i;
    for (i = 0; i < 8; i++) {
        value |= ((unsigned long long) source[i]) << (i * 8);
    }
    return value;
}

static unsigned int checksumRecord(unsigned char *record, unsigned int length) {
    unsigned int checksum = 2166136261U;
    unsigned int stored = get32(record + 12);
    unsigned int i;
    put32(record + 12, 0);
    for (i = 0; i < length; i++) {
        checksum ^= record[i];
        checksum *= 16777619U;
    }
    put32(record + 12, stored);
    return checksum;
}

static int forceFile(int fd) {
#ifdef _WIN32
    return _commit(fd) == 0 ? OK : NOTOK;
#else
    return fsync(fd) == 0 ? OK : NOTOK;
#endif
}

static int truncateFile(int fd, long length) {
#ifdef _WIN32
    return _chsize_s(fd, length) == 0 ? OK : NOTOK;
#else
    return ftruncate(fd, length) == 0 ? OK : NOTOK;
#endif
}

static int tryLock(int fd, bool exclusive) {
#ifdef _WIN32
    OVERLAPPED overlapped = { 0 };
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if (exclusive == TRUE) {
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return NOTOK;
    }
    if (LockFileEx(handle, flags, 0, 1, 0, &overlapped)) {
        return OK;
    }
    return GetLastError() == ERROR_LOCK_VIOLATION ? 1 : NOTOK;
#else
    struct flock lock = { 0 };
    lock.l_type = exclusive == TRUE ? F_WRLCK : F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 1;
    if (fcntl(fd, F_SETLK, &lock) != -1) {
        return OK;
    }
    return errno == EACCES || errno == EAGAIN ? 1 : NOTOK;
#endif
}

static int waitForLock(int fd, bool exclusive) {
    int waited = 0;
    int result;
    while ((result = tryLock(fd, exclusive)) == 1
            && waited <= WAL_LOCK_TIMEOUT_MS) {
#ifdef _WIN32
        Sleep(WAL_LOCK_RETRY_MS);
#else
        usleep(WAL_LOCK_RETRY_MS * 1000);
#endif
        waited += WAL_LOCK_RETRY_MS;
    }
    return result == OK ? OK : NOTOK;
}

static int unlock(int fd) {
#ifdef _WIN32
    OVERLAPPED overlapped = { 0 };
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    return handle != INVALID_HANDLE_VALUE
            && UnlockFileEx(handle, 0, 1, 0, &overlapped)
            ? OK
            : NOTOK;
#else
    struct flock lock = { 0 };
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 1;
    return fcntl(fd, F_SETLK, &lock) == 0 ? OK : NOTOK;
#endif
}

static int acquireWalLock() {
    int fd = open(
            WAL_LOCK_FILE,
            O_RDWR | O_CREAT | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    if (fd < 0 || waitForLock(fd, TRUE) != OK) {
        if (fd >= 0) {
            close(fd);
        }
        return NOTOK;
    }
    return fd;
}

static void releaseWalLock(int fd) {
    if (fd >= 0) {
        unlock(fd);
        close(fd);
    }
}

static int readFully(int fd, unsigned char *buffer, unsigned int length) {
    unsigned int total = 0;
    while (total < length) {
        int count = read(fd, buffer + total, length - total);
        if (count == 0) {
            return total == 0 ? 1 : NOTOK;
        }
        if (count < 0) {
            return NOTOK;
        }
        total += count;
    }
    return OK;
}

static int scanWal(
        int fd,
        WalRecord **records,
        int *recordCount,
        long *validLength,
        unsigned long long *lastLsn,
        unsigned long long *maxTransactionId) {
    long offset = 0;
    int count = 0;
    int capacity = 0;
    WalRecord *items = NULL;
    unsigned char header[WAL_HEADER_SIZE];

    lseek(fd, 0, SEEK_SET);
    while (TRUE) {
        int readResult = readFully(fd, header, WAL_HEADER_SIZE);
        if (readResult == 1) {
            break;
        }
        if (readResult != OK
                || get32(header) != WAL_MAGIC
                || get16(header + 4) != WAL_VERSION) {
            break;
        }

        unsigned int length = get32(header + 8);
        unsigned int payloadLength = get32(header + 64);
        unsigned short type = get16(header + 6);
        if (length != WAL_HEADER_SIZE + payloadLength
                || length > WAL_MAX_RECORD_SIZE
                || (type == WAL_PAGE && payloadLength != PAGESIZE * 2)
                || (type == WAL_BACKUP && payloadLength != 1 + MAXPATH)
                || ((type == WAL_BEGIN || type == WAL_COMMIT
                        || type == WAL_ABORT || type == WAL_DROP)
                        && payloadLength != 0)
                || type < WAL_BEGIN
                || type > WAL_DROP) {
            break;
        }

        unsigned char *encoded = (unsigned char *) malloc(length);
        memcpy(encoded, header, WAL_HEADER_SIZE);
        if (payloadLength > 0
                && readFully(fd, encoded + WAL_HEADER_SIZE, payloadLength) != OK) {
            free(encoded);
            break;
        }
        if (checksumRecord(encoded, length) != get32(encoded + 12)) {
            free(encoded);
            break;
        }

        if (records != NULL) {
            if (count == capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                items = (WalRecord *) realloc(items, capacity * sizeof(WalRecord));
            }
            memset(&items[count], 0, sizeof(WalRecord));
            items[count].type = get16(encoded + 6);
            items[count].lsn = get64(encoded + 16);
            items[count].transactionId = get64(encoded + 24);
            items[count].previousLsn = get64(encoded + 32);
            memcpy(items[count].relation, encoded + 40, RELNAME);
            items[count].relation[RELNAME - 1] = '\0';
            items[count].pageId = get32(encoded + 60);
            items[count].payloadLength = payloadLength;
            if (payloadLength > 0) {
                items[count].payload = (unsigned char *) malloc(payloadLength);
                memcpy(
                        items[count].payload,
                        encoded + WAL_HEADER_SIZE,
                        payloadLength);
            }
        }

        if (get64(encoded + 16) > *lastLsn) {
            *lastLsn = get64(encoded + 16);
        }
        if (get64(encoded + 24) > *maxTransactionId) {
            *maxTransactionId = get64(encoded + 24);
        }
        offset += length;
        count++;
        free(encoded);
    }

    if (records != NULL) {
        *records = items;
        *recordCount = count;
    }
    *validLength = offset;
    return OK;
}

static int appendRecord(
        unsigned short type,
        const char *relation,
        unsigned int pageId,
        const unsigned char *payload,
        unsigned int payloadLength) {
    int lockFd = acquireWalLock();
    int walFd;
    long validLength = 0;
    unsigned long long lastLsn = 0;
    unsigned long long maxTransactionId = 0;
    unsigned int length = WAL_HEADER_SIZE + payloadLength;
    unsigned char *encoded;
    int result = NOTOK;

    if (lockFd == NOTOK) {
        return NOTOK;
    }
    walFd = open(
            WAL_FILE,
            O_RDWR | O_CREAT | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    if (walFd < 0) {
        releaseWalLock(lockFd);
        return NOTOK;
    }

    scanWal(
            walFd,
            NULL,
            NULL,
            &validLength,
            &lastLsn,
            &maxTransactionId);
    if (truncateFile(walFd, validLength) != OK) {
        goto cleanup;
    }
    if (type == WAL_BEGIN) {
        g_TransactionId = maxTransactionId + 1;
        g_PreviousLsn = 0;
    }

    encoded = (unsigned char *) calloc(length, 1);
    put32(encoded, WAL_MAGIC);
    put16(encoded + 4, WAL_VERSION);
    put16(encoded + 6, type);
    put32(encoded + 8, length);
    put64(encoded + 16, lastLsn + 1);
    put64(encoded + 24, g_TransactionId);
    put64(encoded + 32, g_PreviousLsn);
    if (relation != NULL) {
        strncpy((char *) encoded + 40, relation, RELNAME - 1);
    }
    put32(encoded + 60, pageId);
    put32(encoded + 64, payloadLength);
    if (payloadLength > 0) {
        memcpy(encoded + WAL_HEADER_SIZE, payload, payloadLength);
    }
    put32(encoded + 12, checksumRecord(encoded, length));

    if (lseek(walFd, validLength, SEEK_SET) >= 0
            && write(walFd, encoded, length) == (int) length
            && forceFile(walFd) == OK) {
        g_PreviousLsn = lastLsn + 1;
        result = OK;
    }
    free(encoded);

cleanup:
    close(walFd);
    releaseWalLock(lockFd);
    return result;
}

static bool transactionHasType(
        WalRecord *records,
        int recordCount,
        unsigned long long transactionId,
        unsigned short type) {
    int i;
    for (i = 0; i < recordCount; i++) {
        if (records[i].transactionId == transactionId
                && records[i].type == type) {
            return TRUE;
        }
    }
    return FALSE;
}

static bool transactionMutatedResource(
        WalRecord *records,
        int recordCount,
        unsigned long long transactionId,
        const char *relation) {
    int i;
    for (i = 0; i < recordCount; i++) {
        if (records[i].transactionId == transactionId
                && (records[i].type == WAL_PAGE
                        || records[i].type == WAL_DROP)
                && strcmp(records[i].relation, relation) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static int writePageImage(const WalRecord *record, const unsigned char *image) {
    int fd = open(
            record->relation,
            O_RDWR | O_CREAT | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    int result = NOTOK;
    long offset = ((long) record->pageId - 1) * PAGESIZE;
    if (fd >= 0
            && lseek(fd, offset, SEEK_SET) >= 0
            && write(fd, image, PAGESIZE) == PAGESIZE
            && forceFile(fd) == OK) {
        result = OK;
    }
    if (fd >= 0) {
        close(fd);
    }
    return result;
}

static int copyFile(const char *source, const char *destination) {
    int sourceFd = open(source, O_RDONLY | WAL_BINARY_FLAG);
    int destinationFd;
    char buffer[4096];
    int result = OK;
    int count;
    if (sourceFd < 0) {
        return NOTOK;
    }
    destinationFd = open(
            destination,
            O_WRONLY | O_CREAT | O_TRUNC | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    if (destinationFd < 0) {
        close(sourceFd);
        return NOTOK;
    }
    while ((count = read(sourceFd, buffer, sizeof(buffer))) > 0) {
        int written = 0;
        while (written < count) {
            int current = write(destinationFd, buffer + written, count - written);
            if (current <= 0) {
                result = NOTOK;
                break;
            }
            written += current;
        }
        if (result != OK) {
            break;
        }
    }
    if (count < 0 || forceFile(destinationFd) != OK) {
        result = NOTOK;
    }
    close(destinationFd);
    close(sourceFd);
    return result;
}

static int recoverWal() {
    int walFd = open(
            WAL_FILE,
            O_RDWR | O_CREAT | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    WalRecord *records = NULL;
    int recordCount = 0;
    long validLength = 0;
    unsigned long long lastLsn = 0;
    unsigned long long maxTransactionId = 0;
    int result = OK;
    int i;

    if (walFd < 0) {
        return NOTOK;
    }
    scanWal(
            walFd,
            &records,
            &recordCount,
            &validLength,
            &lastLsn,
            &maxTransactionId);
    if (truncateFile(walFd, validLength) != OK) {
        result = NOTOK;
        goto cleanup;
    }

    for (i = 0; i < recordCount && result == OK; i++) {
        WalRecord *record = &records[i];
        if (record->type == WAL_PAGE
                && transactionHasType(
                        records,
                        recordCount,
                        record->transactionId,
                        WAL_COMMIT) == TRUE) {
            result = writePageImage(record, record->payload + PAGESIZE);
            if (result == OK) {
                WalCrashPoint("after_recovery_page");
            }
        }
    }

    for (i = 0; i < recordCount && result == OK; i++) {
        WalRecord *record = &records[i];
        if (record->type == WAL_DROP
                && transactionHasType(
                        records,
                        recordCount,
                        record->transactionId,
                        WAL_COMMIT) == TRUE
                && remove(record->relation) != 0
                && errno != ENOENT) {
            result = NOTOK;
        }
    }

    for (i = recordCount - 1; i >= 0 && result == OK; i--) {
        WalRecord *record = &records[i];
        bool committed = transactionHasType(
                records, recordCount, record->transactionId, WAL_COMMIT);
        bool aborted = transactionHasType(
                records, recordCount, record->transactionId, WAL_ABORT);
        if (committed == FALSE && aborted == FALSE && record->type == WAL_PAGE) {
            result = writePageImage(record, record->payload);
            if (result == OK) {
                WalCrashPoint("after_recovery_page");
            }
        }
    }

    for (i = 0; i < recordCount && result == OK; i++) {
        WalRecord *record = &records[i];
        if (record->type == WAL_BACKUP) {
            bool committed = transactionHasType(
                    records, recordCount, record->transactionId, WAL_COMMIT);
            bool aborted = transactionHasType(
                    records, recordCount, record->transactionId, WAL_ABORT);
            const char *backupPath = (const char *) record->payload + 1;
            if (committed == FALSE && aborted == FALSE) {
                if (record->payload[0] != 0) {
                    struct stat backupInfo;
                    struct stat resourceInfo;
                    if (stat(backupPath, &backupInfo) == 0) {
                        result = copyFile(backupPath, record->relation);
                    } else if (errno == ENOENT
                            && transactionMutatedResource(
                                    records,
                                    recordCount,
                                    record->transactionId,
                                    record->relation) == FALSE
                            && stat(record->relation, &resourceInfo) == 0) {
                        result = OK;
                    } else {
                        result = NOTOK;
                    }
                } else {
                    remove(record->relation);
                }
                if (result == OK) {
                    WalCrashPoint("after_recovery_resource");
                }
            }
        }
    }

    if (result == OK
            && truncateFile(walFd, 0) == OK
            && forceFile(walFd) == OK) {
        lseek(walFd, 0, SEEK_SET);
        for (i = 0; i < recordCount; i++) {
            if (records[i].type == WAL_BACKUP) {
                remove((const char *) records[i].payload + 1);
            }
        }
    } else {
        result = NOTOK;
    }

cleanup:
    for (i = 0; i < recordCount; i++) {
        free(records[i].payload);
    }
    free(records);
    close(walFd);
    return result;
}

int WalOpenDatabase() {
    int exclusiveResult;
    g_DatabaseLockFd = open(
            DATABASE_LOCK_FILE,
            O_RDWR | O_CREAT | WAL_BINARY_FLAG,
            S_IRUSR | S_IWUSR);
    if (g_DatabaseLockFd < 0) {
        return NOTOK;
    }

    exclusiveResult = tryLock(g_DatabaseLockFd, TRUE);
    if (exclusiveResult == OK) {
        if (recoverWal() != OK
                || unlock(g_DatabaseLockFd) != OK
                || waitForLock(g_DatabaseLockFd, FALSE) != OK) {
            close(g_DatabaseLockFd);
            g_DatabaseLockFd = NOTOK;
            return NOTOK;
        }
    } else if (exclusiveResult == 1) {
        if (waitForLock(g_DatabaseLockFd, FALSE) != OK) {
            close(g_DatabaseLockFd);
            g_DatabaseLockFd = NOTOK;
            return NOTOK;
        }
    } else {
        close(g_DatabaseLockFd);
        g_DatabaseLockFd = NOTOK;
        return NOTOK;
    }
    return OK;
}

int WalCloseDatabase() {
    int result = OK;
    if (g_DatabaseLockFd != NOTOK) {
        result = unlock(g_DatabaseLockFd);
        close(g_DatabaseLockFd);
        g_DatabaseLockFd = NOTOK;
    }
    return result;
}

int WalBeginTransaction() {
    if (g_WalTransactionActive == TRUE
            || appendRecord(WAL_BEGIN, NULL, 0, NULL, 0) != OK) {
        return NOTOK;
    }
    g_WalTransactionActive = TRUE;
    return OK;
}

int WalCommitTransaction() {
    if (g_WalTransactionActive == FALSE
            || appendRecord(WAL_COMMIT, NULL, 0, NULL, 0) != OK) {
        return NOTOK;
    }
    g_WalTransactionActive = FALSE;
    return OK;
}

int WalAbortTransaction() {
    if (g_WalTransactionActive == FALSE
            || appendRecord(WAL_ABORT, NULL, 0, NULL, 0) != OK) {
        return NOTOK;
    }
    g_WalTransactionActive = FALSE;
    return OK;
}

bool WalTransactionIsActive() {
    return g_WalTransactionActive;
}

int WalLogPage(
        const char *relation,
        unsigned int pageId,
        const char *beforeImage,
        const char *afterImage) {
    unsigned char payload[PAGESIZE * 2];
    if (g_WalTransactionActive == FALSE) {
        return OK;
    }
    memcpy(payload, beforeImage, PAGESIZE);
    memcpy(payload + PAGESIZE, afterImage, PAGESIZE);
    return appendRecord(WAL_PAGE, relation, pageId, payload, sizeof(payload));
}

int WalLogBackup(
        const char *resource,
        const char *backupPath,
        bool existed) {
    unsigned char payload[1 + MAXPATH];
    if (g_WalTransactionActive == FALSE) {
        return OK;
    }
    memset(payload, 0, sizeof(payload));
    payload[0] = existed == TRUE ? 1 : 0;
    strncpy((char *) payload + 1, backupPath, MAXPATH - 1);
    return appendRecord(WAL_BACKUP, resource, 0, payload, sizeof(payload));
}

int WalLogDrop(const char *resource) {
    if (g_WalTransactionActive == FALSE) {
        return OK;
    }
    return appendRecord(WAL_DROP, resource, 0, NULL, 0);
}

void WalCrashPoint(const char *name) {
    const char *requested = getenv("MINIREL_CRASH_POINT");
    if (requested != NULL && strcmp(requested, name) == 0) {
        _exit(86);
    }
}
