#ifndef TRANSACTION_H_
#define TRANSACTION_H_

#include "defs.h"
#include "locking.h"

bool TransactionIsActive();
bool TransactionOwnsLock(int lockId);
void TransactionMarkFailure();
void TransactionMarkLockFailure();

int AcquireManagedTableLock(const char *relName, LockMode mode);
int AcquireManagedCatalogLock(LockMode mode);
int ReleaseManagedLock(int lockId);

int BeginTransaction(int argc, char **argv);
int CommitTransaction(int argc, char **argv);
int RollbackTransaction(int argc, char **argv);

#endif
