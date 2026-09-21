#include "../include/update.h"

typedef struct updateAssignment {
    struct attrCatalog *attribute;
    char *value;
    bool isNull;
} UpdateAssignment;

typedef struct updateCondition {
    struct attrCatalog *attribute;
    int comparison;
    char *value;
    bool isNull;
} UpdateCondition;

typedef struct updateRow {
    Rid rid;
    char *original;
    char *updated;
    bool matches;
} UpdateRow;

static bool matchesCondition(
        const CacheEntry *relation,
        const char *record,
        const UpdateCondition *condition) {
    const struct attrCatalog *attribute = condition->attribute;
    bool recordIsNull = RecordAttributeIsNull(relation, record, attribute);

    if (condition->isNull == TRUE) {
        if (condition->comparison == EQ)
            return recordIsNull;
        if (condition->comparison == NEQ)
            return recordIsNull == TRUE ? FALSE : TRUE;
        return FALSE;
    }
    if (recordIsNull == TRUE)
        return FALSE;

    switch (attribute->type) {
        case INTEGER:
            return compareNum(
                    (float) readIntFromByteArray(record, attribute->offset),
                    (float) readIntFromByteArray(condition->value, 0),
                    condition->comparison);
        case FLOAT:
            return compareNum(
                    readFloatFromByteArray(record, attribute->offset),
                    readFloatFromByteArray(condition->value, 0),
                    condition->comparison);
        case STRING: {
            char *left = (char *) calloc(attribute->length + 1, 1);
            char *right = (char *) calloc(attribute->length + 1, 1);
            bool matches;
            readStringFromByteArray(left, record, attribute->offset, attribute->length);
            readStringFromByteArray(right, condition->value, 0, attribute->length);
            matches = compareStrings(left, right, condition->comparison);
            free(left);
            free(right);
            return matches;
        }
        default:
            return FALSE;
    }
}

static const char *finalRecord(const UpdateRow *row) {
    return row->matches == TRUE ? row->updated : row->original;
}

static void freeUpdateState(
        UpdateAssignment *assignments,
        int assignmentCount,
        UpdateCondition *conditions,
        int conditionCount,
        UpdateRow *rows,
        int rowCount) {
    int i;

    for (i = 0; i < assignmentCount; i++) {
        free(assignments[i].value);
    }
    for (i = 0; i < conditionCount; i++) {
        free(conditions[i].value);
    }
    for (i = 0; i < rowCount; i++) {
        free(rows[i].original);
        free(rows[i].updated);
    }
    free(assignments);
    free(conditions);
    free(rows);
}

int Update(int argc, char **argv) {
    int whereIndex = NOTOK;
    int assignmentCount;
    int conditionCount;
    int relNum;
    int recordLength;
    int rowCapacity;
    int rowCount = 0;
    int updateCount = 0;
    int i;
    int j;
    int result = NOTOK;
    int offsetMap[MAXRECORD] = { 0 };
    UpdateAssignment *assignments = NULL;
    UpdateCondition *conditions = NULL;
    UpdateRow *rows = NULL;
    Rid startRid = { 0, 0 };
    Rid *foundRid = NULL;
    char *record = NULL;

    if (g_DBOpenFlag != OK) {
        return ErrorMsgs(DB_NOT_OPEN, g_PrintFlag);
    }
    if (argc < 8) {
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);
    }
    if (strcmp(argv[1], RELCAT) == 0 || strcmp(argv[1], ATTRCAT) == 0) {
        return ErrorMsgs(METADATA_SECURITY, g_PrintFlag);
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "where") == 0) {
            whereIndex = i;
            break;
        }
    }
    if (whereIndex == NOTOK || (whereIndex - 2) % 2 != 0
            || (argc - whereIndex - 1) % 3 != 0) {
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);
    }

    assignmentCount = (whereIndex - 2) / 2;
    conditionCount = (argc - whereIndex - 1) / 3;
    if (assignmentCount == 0 || conditionCount == 0) {
        return ErrorMsgs(ARGC_INSUFFICIENT, g_PrintFlag);
    }

    relNum = OpenRelWithLock(argv[1], LOCK_EXCLUSIVE);
    if (relNum == NOTOK) {
        return NOTOK;
    }
    recordLength = g_CatCache[relNum].recLength;

    assignments = (UpdateAssignment *) calloc(
            assignmentCount, sizeof(UpdateAssignment));
    conditions = (UpdateCondition *) calloc(
            conditionCount, sizeof(UpdateCondition));
    rowCapacity = g_CatCache[relNum].numRecs;
    rows = (UpdateRow *) calloc(rowCapacity > 0 ? rowCapacity : 1, sizeof(UpdateRow));

    for (i = 0; i < assignmentCount; i++) {
        struct attrCatalog *attribute = getAttrCatalog(
                g_CatCache[relNum].attrList, argv[2 + i * 2]);
        if (attribute == NULL) {
            ErrorMsgs(ATTR_NOT_IN_REL, g_PrintFlag);
            goto cleanup;
        }
        if (offsetMap[attribute->offset] == 1) {
            ErrorMsgs(ATTR_REPEATED, g_PrintFlag);
            goto cleanup;
        }
        offsetMap[attribute->offset] = 1;
        assignments[i].attribute = attribute;
        assignments[i].value = (char *) calloc(attribute->length, 1);
        if (EncodeTextValue(
                attribute,
                argv[3 + i * 2],
                assignments[i].value,
                &assignments[i].isNull) != OK) {
            goto cleanup;
        }
        if (assignments[i].isNull == TRUE) {
            if (g_CatCache[relNum].hasNullBitmap == FALSE) {
                ErrorMsgs(LEGACY_NULL_UNSUPPORTED, g_PrintFlag);
                goto cleanup;
            }
            if (attribute->notNull == TRUE) {
                ErrorMsgs(NOT_NULL_CONSTRAINT_VIOLATION, g_PrintFlag);
                goto cleanup;
            }
        }
    }

    for (i = 0; i < conditionCount; i++) {
        int base = whereIndex + 1 + i * 3;
        struct attrCatalog *attribute = getAttrCatalog(
                g_CatCache[relNum].attrList, argv[base]);
        if (attribute == NULL) {
            ErrorMsgs(ATTR_NOT_IN_REL, g_PrintFlag);
            goto cleanup;
        }
        conditions[i].attribute = attribute;
        conditions[i].comparison = readIntFromByteArray(argv[base + 1], 0);
        conditions[i].value = (char *) calloc(attribute->length, 1);
        if (EncodeTextValue(
                attribute,
                argv[base + 2],
                conditions[i].value,
                &conditions[i].isNull) != OK) {
            goto cleanup;
        }
        if (conditions[i].isNull == TRUE
                && conditions[i].comparison != EQ
                && conditions[i].comparison != NEQ) {
            ErrorMsgs(INVALID_COMP_OP, g_PrintFlag);
            goto cleanup;
        }
    }

    while (GetNextRec(relNum, &startRid, &foundRid, &record) == OK) {
        bool matches = TRUE;
        UpdateRow *row = &rows[rowCount];
        row->rid = *foundRid;
        row->original = (char *) malloc(recordLength);
        row->updated = (char *) malloc(recordLength);
        memcpy(row->original, record, recordLength);
        memcpy(row->updated, record, recordLength);

        for (i = 0; i < conditionCount; i++) {
            if (matchesCondition(
                    &g_CatCache[relNum],
                    row->original,
                    &conditions[i]) == FALSE) {
                matches = FALSE;
                break;
            }
        }
        row->matches = matches;
        if (matches == TRUE) {
            for (i = 0; i < assignmentCount; i++) {
                if (assignments[i].isNull == TRUE) {
                    RecordSetAttributeNull(
                            &g_CatCache[relNum],
                            row->updated,
                            assignments[i].attribute);
                } else {
                    RecordClearAttributeNull(
                            &g_CatCache[relNum],
                            row->updated,
                            assignments[i].attribute);
                    memcpy(
                            row->updated + assignments[i].attribute->offset,
                            assignments[i].value,
                            assignments[i].attribute->length);
                }
            }
            updateCount++;
        }

        rowCount++;
        startRid = *foundRid;
        free(foundRid);
        foundRid = NULL;
    }

    for (i = 0; i < rowCount; i++) {
        const char *left = finalRecord(&rows[i]);
        if (ValidateRecordForRelation(&g_CatCache[relNum], left) != OK) {
            goto cleanup;
        }
        for (j = i + 1; j < rowCount; j++) {
            const char *right = finalRecord(&rows[j]);
            struct attrCatalog *attribute;

            for (attribute = g_CatCache[relNum].attrList;
                    attribute != NULL;
                    attribute = attribute->next) {
                if (attribute->unique == TRUE
                        && RecordAttributeIsNull(
                                &g_CatCache[relNum], left, attribute) == FALSE
                        && RecordAttributeIsNull(
                                &g_CatCache[relNum], right, attribute) == FALSE
                        && memcmp(
                                left + attribute->offset,
                                right + attribute->offset,
                                attribute->length) == 0) {
                    ErrorMsgs(
                            attribute->primaryKey == TRUE
                                    ? PRIMARY_KEY_VIOLATION
                                    : UNIQUE_CONSTRAINT_VIOLATION,
                            g_PrintFlag);
                    goto cleanup;
                }
            }
            if (compareRecords((char *) left, (char *) right, recordLength) == OK) {
                ErrorMsgs(DUPLICATE_TUPLE, g_PrintFlag);
                goto cleanup;
            }
        }
    }

    for (i = 0; i < rowCount; i++) {
        if (rows[i].matches == TRUE
                && WriteRec(relNum, rows[i].updated, &rows[i].rid) != OK) {
            goto cleanup;
        }
    }

    printf("Updated %d record(s).\n", updateCount);
    result = OK;

cleanup:
    if (foundRid != NULL) {
        free(foundRid);
    }
    freeUpdateState(
            assignments,
            assignmentCount,
            conditions,
            conditionCount,
            rows,
            rowCount);
    return result;
}
