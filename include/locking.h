#ifndef LOCKING_H_
#define LOCKING_H_

#include "defs.h"
#include "error.h"

typedef enum lockMode {
    LOCK_NONE = 0,
    LOCK_SHARED = 1,
    LOCK_EXCLUSIVE = 2
} LockMode;

int AcquireTableLock(const char *relName, LockMode mode);
int AcquireCatalogLock(LockMode mode);
int UpgradeLock(int lockId, LockMode mode);
int ReleaseLock(int lockId);
bool LockTimedOut();
int ReportLockFailure();

#endif
