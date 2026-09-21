#include "../include/create.h"

#define _DESTROY  "_destroy"

static int CreateUnlocked(int argc, char **argv);

static int ParseAttributeFormat(
        const char *format,
        char *type,
        int *length,
        int *isUnique,
        int *isNotNull,
        int *isPrimaryKey) {
    int index = 1;
    int stringLength = 0;
    bool sawUnique = FALSE;
    bool sawNotNull = FALSE;
    bool sawPrimary = FALSE;

    *type = format[0];
    if (*type == STRING) {
        int digitStart = index;
        while (isdigit((unsigned char) format[index])) {
            if (stringLength > (INT_MAX - 9) / 10) {
                return ErrorMsgs(MAX_STRING_EXCEEDED, g_PrintFlag);
            }
            stringLength = stringLength * 10 + format[index] - '0';
            index++;
        }
        if (index == digitStart || stringLength < 1) {
            return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
        }
        if (stringLength > MAX_STRING_SIZE) {
            return ErrorMsgs(MAX_STRING_EXCEEDED, g_PrintFlag);
        }
        *length = stringLength;
    } else if (*type == INTEGER) {
        *length = sizeof(int);
    } else if (*type == FLOAT) {
        *length = sizeof(float);
    } else {
        return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
    }

    while (format[index] != '\0') {
        if (format[index] == '!') {
            if (sawUnique == TRUE)
                return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
            sawUnique = TRUE;
        } else if (format[index] == '^') {
            if (sawNotNull == TRUE)
                return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
            sawNotNull = TRUE;
        } else if (format[index] == '#') {
            if (sawPrimary == TRUE)
                return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
            sawPrimary = TRUE;
        } else {
            return ErrorMsgs(INVALID_ATTR_TYPE, g_PrintFlag);
        }
        index++;
    }

    *isPrimaryKey = sawPrimary;
    *isUnique = sawUnique || sawPrimary;
    *isNotNull = sawNotNull || sawPrimary;
    return OK;
}

/**
 * This routine creates a new relation with the specified name and attributes
 *
 * argv[0] - "create"
 * argv[1] - relation name
 * argv[2] - 1st attribute name
 * argv[3] - 1st attribute format (i,f or sN)
 * ...
 * argv[argc] - NIL
 *
 * @param argc
 * @param argv
 * @return OK if success
 *
 * GLOBAL VARIABLES MODIFIED
 *      g_PrintFlag (temporarily only)
 *
 * ERRORS RETURNED
 *      DB_NOT_OPEN
 *      INVALID_ATTR_NAME
 *      REL_ALREADY_EXISTS
 *      MAX_STRING_EXCEEDED
 *      PAGE_OVERFLOW
 *
 * ALGORITHM:
 *      1. Check for any errors in arguments
 *      2. Create an array of strings to be passed to Insert()
 *      3. Populate the array of strings with the 'fixed' arguments
 *          of insert (to attrcat)
 *      4. For each attribute
 *      5.      Check if the attribute type and name are valid
 *      6.      Populate the 'variable' arguments in insert
 *                  (attrName, attrType, attrLength, relNam)
 *      7.      Check if the recordsize exceeds MAXRECORD
 *      8.      Call Insert() to insert into attrcat relation
 *              [At any point if error occurs, delete all the inserted records]
 *      9. Similarly, insert a new record in relcat
 *      10. Create a new file for the relation and write an empty page into it.
 *
 * IMPLEMENTATION NOTES:
 *      Uses OpenRel(), Insert(), Destroy()
 */
int Create(int argc, char **argv) {
    if (argc < 2 || g_DBOpenFlag != OK) {
        return CreateUnlocked(argc, argv);
    }

    int tableLock = AcquireManagedTableLock(argv[1], LOCK_EXCLUSIVE);
    if (tableLock == NOTOK) {
        return ReportLockFailure();
    }
    int catalogLock = AcquireManagedCatalogLock(LOCK_EXCLUSIVE);
    if (catalogLock == NOTOK) {
        ReleaseManagedLock(tableLock);
        return ReportLockFailure();
    }

    if (RefreshCatalogCaches() != OK) {
        ReleaseManagedLock(catalogLock);
        ReleaseManagedLock(tableLock);
        return NOTOK;
    }
    int result = CreateUnlocked(argc, argv);
    FlushPage(ATTRCAT_CACHE);
    FlushPage(RELCAT_CACHE);
    ReleaseManagedLock(catalogLock);
    ReleaseManagedLock(tableLock);
    return result;
}

static int CreateUnlocked(int argc, char **argv) {

    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }

    int offset, length, i, isUnique, isNotNull, isPrimaryKey, primaryKeyCount;
    int bitmapBytes, recordLength;
    int projectedDataLength = 0;
    char type, attrName[RELNAME], relName[RELNAME];

    if (isValidString(argv[1]) == NOTOK) {
        return ErrorMsgs(INVALID_ATTR_NAME, g_PrintFlag);
    }
    if (argc < 4 || argc % 2 != 0) {
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);
    }
    strcpy(relName, argv[1]);

    primaryKeyCount = 0;
    for (i = 2; i < argc; i += 2) {
        int j;
        if (isValidString(argv[i]) == NOTOK) {
            return ErrorMsgs(INVALID_ATTR_NAME, g_PrintFlag);
        }
        for (j = 2; j < i; j += 2) {
            if (strcmp(argv[i], argv[j]) == 0) {
                return ErrorMsgs(ATTR_REPEATED, g_PrintFlag);
            }
        }
        if (ParseAttributeFormat(
                argv[i + 1],
                &type,
                &length,
                &isUnique,
                &isNotNull,
                &isPrimaryKey) != OK) {
            return NOTOK;
        }
        projectedDataLength += length;
        if (isPrimaryKey)
            primaryKeyCount++;
    }
    if (primaryKeyCount > 1)
        return ErrorMsgs(MULTIPLE_PRIMARY_KEYS, g_PrintFlag);
    bitmapBytes = NullBitmapSize((argc - 2) / 2);
    if (projectedDataLength + bitmapBytes > MAXRECORD)
        return ErrorMsgs(PAGE_OVERFLOW, g_PrintFlag);

    /* To check if relation exists, we will call openRel
     * after turning off the error print flag temporarily. */
    int printFlag = g_PrintFlag;
    g_PrintFlag = 1;
    if (OpenRel(relName) != NOTOK) {
        g_PrintFlag = printFlag;
        return ErrorMsgs(REL_ALREADY_EXISTS, g_PrintFlag);
    } else {
        g_PrintFlag = printFlag;
    }
    /* Creating the template attribute catalog records */
    char **attrCatArgs;
    int attrCatArraySize;
    createTemplate(ATTRCAT_CACHE, &attrCatArgs, ATTRCAT, &attrCatArraySize);

    int numAttrs;
    /* Iterate through each attribute and insert it to attrcat table */
    for (i = 2, numAttrs = 0, offset = 0; i < argc; i = i + 2, numAttrs++) {
        memset(attrName, 0, sizeof(attrName));
        strncpy(attrName, argv[i], RELNAME - 1);
        ParseAttributeFormat(
                argv[i + 1],
                &type,
                &length,
                &isUnique,
                &isNotNull,
                &isPrimaryKey);
        int j;

        /* Filling the attribute catalog record template */
        for (j = 2; j < attrCatArraySize; j += 2) {
            if (strcmp(attrCatArgs[j], OFFSET) == 0) {
                sprintf(attrCatArgs[j + 1], "%d", offset);
            } else if (strcmp(attrCatArgs[j], TYPE) == 0) {
                sprintf(
                        attrCatArgs[j + 1],
                        "%d",
                        EncodeAttributeType(type, isUnique, isNotNull, isPrimaryKey)
                                | NULL_BITMAP_STORAGE_FLAG);
            } else if (strcmp(attrCatArgs[j], LENGTH) == 0) {
                sprintf(attrCatArgs[j + 1], "%d", length);
            } else if (strcmp(attrCatArgs[j], ATTRNAME) == 0) {
                strcpy(attrCatArgs[j + 1], attrName);
            } else {
                sprintf(attrCatArgs[j + 1], "%s", relName);
            }
        }
        offset += length;
        if (offset + bitmapBytes > MAXRECORD) {
            char **destroy_args = (char **) malloc(sizeof(char*) * 2);
            destroy_args[0] = (char *) malloc(sizeof(char) * (strlen(_DESTROY) + 1));
            destroy_args[1] = (char *) malloc(sizeof(char) * (strlen(relName) + 1));
            sprintf(destroy_args[0], "%s", _DESTROY);
            sprintf(destroy_args[1], "%s", relName);
            Destroy(2, (char **) destroy_args);
            free(destroy_args[0]);
            free(destroy_args[1]);
            free(destroy_args);
            return ErrorMsgs(PAGE_OVERFLOW, g_PrintFlag);
        }
        Insert(attrCatArraySize, attrCatArgs);
    }

    /* Free the attrCatArgs*/
    freeAllottedMem(attrCatArgs, attrCatArraySize);
    recordLength = offset + bitmapBytes;

    /* Insert the data into relcat */
    char **relcatArgs;
    int relcatArraySize;
    createTemplate(RELCAT_CACHE, &relcatArgs, RELCAT, &relcatArraySize);

    //Fill the template
    int j, recsPerPg;
    recsPerPg = (MAXRECORD / recordLength) < 32
            ? (MAXRECORD / recordLength)
            : 32;

    for (j = 2; j < relcatArraySize; j += 2) {
        if (strcmp(relcatArgs[j], RECLENGTH) == 0) {
            sprintf(relcatArgs[j + 1], "%d", recordLength);
        } else if (strcmp(relcatArgs[j], RECSPERPG) == 0) {
            sprintf(relcatArgs[j + 1], "%d", recsPerPg);
        } else if (strcmp(relcatArgs[j], NUMATTRS) == 0) {
            sprintf(relcatArgs[j + 1], "%d", numAttrs);
        } else if (strcmp(relcatArgs[j], NUMRECS) == 0) {
            sprintf(relcatArgs[j + 1], "%d", 0);
        } else if (strcmp(relcatArgs[j], NUMPGS) == 0) {
            sprintf(relcatArgs[j + 1], "%d", 1);
        } else {
            sprintf(relcatArgs[j + 1], "%s", relName);
        }
    }
    Insert(relcatArraySize, relcatArgs);

    freeAllottedMem(relcatArgs, relcatArraySize);

    //Creating the file for relation and adding a page
    int fd = open(
            relName,
            O_RDWR | O_CREAT | O_TRUNC | MINIREL_BINARY_FLAG,
            S_IRWXU);
    char *emptyPage = (char *) calloc(PAGESIZE, 1);
    write(fd, emptyPage, PAGESIZE);
#ifdef _WIN32
    _commit(fd);
#else
    fsync(fd);
#endif
    close(fd);
    free(emptyPage);

    return OK; /* all's fine */
}
/**
 * Creates the arguments 'template' that can be passed to Insert
 *
 * @param cacheIndex
 * @param args
 * @param relName
 * @param arraySize
 */
void createTemplate(int cacheIndex, char ***args, char *relName, int *arraySize) {
    int i;
    struct attrCatalog *attrList = g_CatCache[cacheIndex].attrList;
    *arraySize = (2 * g_CatCache[cacheIndex].numAttrs + 2);

    *args = (char **) malloc(*arraySize * sizeof(char *));
    (*args)[0] = (char *) malloc((strlen("_insert") + 1) * sizeof(char));
    strcpy((*args)[0], "_insert");
    (*args)[1] = (char *) malloc((strlen(relName) + 1) * sizeof(char));
    strcpy((*args)[1], relName);

    i = 2;
    while (attrList != NULL) {
        (*args)[i] = (char *) malloc((strlen(attrList->attrName) + 1) * sizeof(char));
        (*args)[i + 1] = (char *) malloc((attrList->length + 1) * sizeof(char));
        strcpy((*args)[i], attrList->attrName);
        i += 2;
        attrList = attrList->next;
    }
}

/**
 * Free all the allotted memory for args
 *
 * @param args
 * @param arraySize
 */
void freeAllottedMem(char **args, int arraySize) {
    int i;
    for (i = 0; i < arraySize; ++i) {
        free(args[i]);
    }
    free(args);
}
/**
 * Deletes the attrcat entries related to relName
 *
 * @param relName
 */
void deleteAttrCatEntries(char *relName) {
    int deleteArgCount = 5;
    char **deleteArgs;
    strcpy(deleteArgs[0], "_delete");
    strcpy(deleteArgs[1], ATTRCAT);
    strcpy(deleteArgs[2], "relName");
    convertIntToByteArray(EQ, deleteArgs[3]);
    strcpy(deleteArgs[4], relName);

    Delete(deleteArgCount, deleteArgs);
}
