#include "../include/createcats.h"

static void InitializeRelationRecord(
        RelCatalogRecord *record,
        const char *name,
        unsigned int recordSize,
        unsigned int attributeCount,
        unsigned int recordCount,
        unsigned int pageCount) {
    memset(record, 0, sizeof(RelCatalogRecord));
    strncpy(record->relName, name, RELNAME);
    record->recLength = recordSize;
    record->recsPerPg = MAXRECORD / recordSize;
    record->numAttrs = attributeCount;
    record->numRecs = recordCount;
    record->numPgs = pageCount;
}

static void InitializeAttributeRecord(
        AttrCatalogRecord *record,
        const CatalogAttributeDefinition *definition,
        const char *relationName) {
    memset(record, 0, sizeof(AttrCatalogRecord));
    record->offset = definition->offset;
    record->length = definition->length;
    record->type = definition->type;
    record->unique = definition->unique;
    record->notNull = definition->notNull;
    record->primaryKey = definition->primaryKey;
    strncpy(record->attrName, definition->name, RELNAME);
    strncpy(record->relName, relationName, RELNAME);
}

int CreateCats() {
    if (CreateRelCat() == OK && CreateAttrCat() == OK)
        return OK;
    return NOTOK;
}

int CreateRelCat() {
    FILE *filePointer;
    char page[PAGESIZE];
    RelCatalogRecord records[2];
    int i;

    if (access(RELCAT, F_OK) != -1)
        return ErrorMsgs(CAT_FILE_ALREADY_EXIST, g_PrintFlag);

    memset(page, 0, sizeof(page));
    convertIntToByteArray(CatalogSlotMap(2), page);

    InitializeRelationRecord(
            &records[0],
            RELCAT,
            RELCAT_RECORD_SIZE,
            RELCAT_ATTRIBUTE_COUNT,
            2,
            1);
    InitializeRelationRecord(
            &records[1],
            ATTRCAT,
            ATTRCAT_RECORD_SIZE,
            ATTRCAT_ATTRIBUTE_COUNT,
            SYSTEM_ATTRIBUTE_COUNT,
            2);

    for (i = 0; i < 2; i++) {
        EncodeRelCatalogRecord(
                page + PAGE_HEADER_SIZE + i * RELCAT_RECORD_SIZE,
                &records[i]);
    }

    filePointer = fopen(RELCAT, "wb");
    if (filePointer == NULL)
        return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
    fwrite(page, 1, sizeof(page), filePointer);
    fclose(filePointer);
    return OK;
}

int CreateAttrCat() {
    FILE *filePointer;
    char pages[PAGESIZE * 2];
    int recordsPerPage = MAXRECORD / ATTRCAT_RECORD_SIZE;
    int pageCount = (SYSTEM_ATTRIBUTE_COUNT + recordsPerPage - 1) / recordsPerPage;
    int pageIndex, recordIndex, recordsOnPage;

    if (access(ATTRCAT, F_OK) != -1)
        return ErrorMsgs(CAT_FILE_ALREADY_EXIST, g_PrintFlag);

    memset(pages, 0, sizeof(pages));

    for (pageIndex = 0; pageIndex < pageCount; pageIndex++) {
        int remaining = SYSTEM_ATTRIBUTE_COUNT - pageIndex * recordsPerPage;
        recordsOnPage = remaining < recordsPerPage ? remaining : recordsPerPage;
        convertIntToByteArray(
                CatalogSlotMap(recordsOnPage),
                pages + pageIndex * PAGESIZE);
    }

    for (recordIndex = 0; recordIndex < SYSTEM_ATTRIBUTE_COUNT; recordIndex++) {
        AttrCatalogRecord record;
        const CatalogAttributeDefinition *definition;
        const char *relationName;
        int slotIndex;

        if (recordIndex < RELCAT_ATTRIBUTE_COUNT) {
            definition = &RELCAT_SCHEMA[recordIndex];
            relationName = RELCAT;
        } else {
            definition = &ATTRCAT_SCHEMA[recordIndex - RELCAT_ATTRIBUTE_COUNT];
            relationName = ATTRCAT;
        }

        InitializeAttributeRecord(&record, definition, relationName);
        pageIndex = recordIndex / recordsPerPage;
        slotIndex = recordIndex % recordsPerPage;
        EncodeAttrCatalogRecord(
                pages + pageIndex * PAGESIZE + PAGE_HEADER_SIZE
                        + slotIndex * ATTRCAT_RECORD_SIZE,
                &record);
    }

    filePointer = fopen(ATTRCAT, "wb");
    if (filePointer == NULL)
        return ErrorMsgs(FILE_SYSTEM_ERROR, g_PrintFlag);
    fwrite(pages, 1, PAGESIZE * pageCount, filePointer);
    fclose(filePointer);
    return OK;
}
