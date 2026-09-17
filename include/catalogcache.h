#ifndef CATALOGCACHE_H_
#define CATALOGCACHE_H_

#include "defs.h"
#include "globals.h"
#include "flushpage.h"
#include "helpers.h"

#include <fcntl.h>
#include <unistd.h>

int RefreshCatalogCache(int relNum);
int RefreshCatalogCaches();

#endif
