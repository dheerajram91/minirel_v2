#include "../include/catalog.h"
#include "../include/helpers.h"

#include <stdlib.h>
#include <string.h>

const CatalogAttributeDefinition RELCAT_SCHEMA[RELCAT_ATTRIBUTE_COUNT] = {
    { "relName", RELCAT_RELNAME_OFFSET, RELNAME, STRING, FALSE },
    { "recLength", RELCAT_RECLENGTH_OFFSET, sizeof(int), INTEGER, FALSE },
    { "recsPerPg", RELCAT_RECSPERPG_OFFSET, sizeof(int), INTEGER, FALSE },
    { "numAttrs", RELCAT_NUMATTRS_OFFSET, sizeof(int), INTEGER, FALSE },
    { "numRecs", RELCAT_NUMRECS_OFFSET, sizeof(int), INTEGER, FALSE },
    { "numPgs", RELCAT_NUMPGS_OFFSET, sizeof(int), INTEGER, FALSE }
};

const CatalogAttributeDefinition ATTRCAT_SCHEMA[ATTRCAT_ATTRIBUTE_COUNT] = {
    { "offset", ATTRCAT_OFFSET_OFFSET, sizeof(int), INTEGER, FALSE },
    { "length", ATTRCAT_LENGTH_OFFSET, sizeof(int), INTEGER, FALSE },
    { "type", ATTRCAT_TYPE_OFFSET, sizeof(int), INTEGER, FALSE },
    { "attrName", ATTRCAT_ATTRNAME_OFFSET, RELNAME, STRING, FALSE },
    { "relName", ATTRCAT_RELNAME_OFFSET, RELNAME, STRING, FALSE }
};

int EncodeAttributeType(datatype type, bool unique) {
    return type | (unique == TRUE ? UNIQUE_ATTRIBUTE_FLAG : 0);
}

void DecodeAttributeType(int encodedType, datatype *type, bool *unique) {
    *unique = (encodedType & UNIQUE_ATTRIBUTE_FLAG) != 0;
    *type = encodedType & ATTRIBUTE_TYPE_MASK;
}

void EncodeRelCatalogRecord(char *destination, const RelCatalogRecord *record) {
    memset(destination, 0, RELCAT_RECORD_SIZE);
    strncpy(destination + RELCAT_RELNAME_OFFSET, record->relName, RELNAME);
    convertIntToByteArray(record->recLength, destination + RELCAT_RECLENGTH_OFFSET);
    convertIntToByteArray(record->recsPerPg, destination + RELCAT_RECSPERPG_OFFSET);
    convertIntToByteArray(record->numAttrs, destination + RELCAT_NUMATTRS_OFFSET);
    convertIntToByteArray(record->numRecs, destination + RELCAT_NUMRECS_OFFSET);
    convertIntToByteArray(record->numPgs, destination + RELCAT_NUMPGS_OFFSET);
}

void DecodeRelCatalogRecord(const char *source, RelCatalogRecord *record) {
    memset(record, 0, sizeof(RelCatalogRecord));
    strncpy(record->relName, source + RELCAT_RELNAME_OFFSET, RELNAME);
    record->recLength = readIntFromByteArray(source, RELCAT_RECLENGTH_OFFSET);
    record->recsPerPg = readIntFromByteArray(source, RELCAT_RECSPERPG_OFFSET);
    record->numAttrs = readIntFromByteArray(source, RELCAT_NUMATTRS_OFFSET);
    record->numRecs = readIntFromByteArray(source, RELCAT_NUMRECS_OFFSET);
    record->numPgs = readIntFromByteArray(source, RELCAT_NUMPGS_OFFSET);
}

void EncodeAttrCatalogRecord(char *destination, const AttrCatalogRecord *record) {
    memset(destination, 0, ATTRCAT_RECORD_SIZE);
    convertIntToByteArray(record->offset, destination + ATTRCAT_OFFSET_OFFSET);
    convertIntToByteArray(record->length, destination + ATTRCAT_LENGTH_OFFSET);
    convertIntToByteArray(
            EncodeAttributeType(record->type, record->unique),
            destination + ATTRCAT_TYPE_OFFSET);
    strncpy(destination + ATTRCAT_ATTRNAME_OFFSET, record->attrName, RELNAME);
    strncpy(destination + ATTRCAT_RELNAME_OFFSET, record->relName, RELNAME);
}

void DecodeAttrCatalogRecord(const char *source, AttrCatalogRecord *record) {
    int encodedType;

    memset(record, 0, sizeof(AttrCatalogRecord));
    record->offset = readIntFromByteArray(source, ATTRCAT_OFFSET_OFFSET);
    record->length = readIntFromByteArray(source, ATTRCAT_LENGTH_OFFSET);
    encodedType = readIntFromByteArray(source, ATTRCAT_TYPE_OFFSET);
    DecodeAttributeType(encodedType, &record->type, &record->unique);
    strncpy(record->attrName, source + ATTRCAT_ATTRNAME_OFFSET, RELNAME);
    strncpy(record->relName, source + ATTRCAT_RELNAME_OFFSET, RELNAME);
}

unsigned int CatalogSlotMap(unsigned int recordCount) {
    if (recordCount == 0)
        return 0;
    if (recordCount >= 32)
        return 0xFFFFFFFF;
    return 0xFFFFFFFF << (32 - recordCount);
}

struct attrCatalog* BuildAttributeCatalog(
        const CatalogAttributeDefinition *schema,
        unsigned int attributeCount,
        const char *relationName) {
    struct attrCatalog *head = NULL;
    struct attrCatalog *tail = NULL;
    unsigned int i;

    for (i = 0; i < attributeCount; i++) {
        struct attrCatalog *node = calloc(1, sizeof(struct attrCatalog));
        if (node == NULL)
            return head;

        node->offset = schema[i].offset;
        node->length = schema[i].length;
        node->type = schema[i].type;
        node->unique = schema[i].unique;
        strncpy(node->attrName, schema[i].name, RELNAME);
        strncpy(node->relName, relationName, RELNAME);

        if (head == NULL)
            head = node;
        else
            tail->next = node;
        tail = node;
    }

    return head;
}
