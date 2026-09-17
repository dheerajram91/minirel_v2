#ifndef UPDATE_H_
#define UPDATE_H_

#include "defs.h"
#include "error.h"
#include "findrelnum.h"
#include "getnextrec.h"
#include "globals.h"
#include "helpers.h"
#include "openrel.h"
#include "writerec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Update(int argc, char **argv);

#endif
