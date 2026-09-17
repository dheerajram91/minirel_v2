#ifndef WAL_H_
#define WAL_H_

#include "defs.h"

int WalOpenDatabase();
int WalCloseDatabase();

int WalBeginTransaction();
int WalCommitTransaction();
int WalAbortTransaction();
bool WalTransactionIsActive();

int WalLogPage(
        const char *relation,
        unsigned int pageId,
        const char *beforeImage,
        const char *afterImage);
int WalLogBackup(
        const char *resource,
        const char *backupPath,
        bool existed);
int WalLogDrop(const char *resource);

void WalCrashPoint(const char *name);

#endif
