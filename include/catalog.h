#ifndef CATALOG_H_
#define CATALOG_H_

#include "cache.h"
#include "defs.h"

#define PAGE_HEADER_SIZE (PAGESIZE - MAXRECORD)

#define RELCAT_RECORD_SIZE 40
#define RELCAT_RELNAME_OFFSET 0
#define RELCAT_RECLENGTH_OFFSET 20
#define RELCAT_RECSPERPG_OFFSET 24
#define RELCAT_NUMATTRS_OFFSET 28
#define RELCAT_NUMRECS_OFFSET 32
#define RELCAT_NUMPGS_OFFSET 36

#define ATTRCAT_RECORD_SIZE 52
#define ATTRCAT_OFFSET_OFFSET 0
#define ATTRCAT_LENGTH_OFFSET 4
#define ATTRCAT_TYPE_OFFSET 8
#define ATTRCAT_ATTRNAME_OFFSET 12
#define ATTRCAT_RELNAME_OFFSET 32

/* Constraint bits share the stored type integer to preserve the legacy 52-byte record. */
#define UNIQUE_ATTRIBUTE_FLAG 0x100
#define NOT_NULL_ATTRIBUTE_FLAG 0x200
#define PRIMARY_KEY_ATTRIBUTE_FLAG 0x400
#define ATTRIBUTE_TYPE_MASK 0xFF

#define RELCAT_ATTRIBUTE_COUNT 6
#define ATTRCAT_ATTRIBUTE_COUNT 5
#define SYSTEM_ATTRIBUTE_COUNT (RELCAT_ATTRIBUTE_COUNT + ATTRCAT_ATTRIBUTE_COUNT)

typedef struct catalogAttributeDefinition {
    const char *name;
    unsigned int offset;
    unsigned int length;
    datatype type;
    bool unique;
    bool notNull;
    bool primaryKey;
} CatalogAttributeDefinition;

typedef struct relCatalogRecord {
    char relName[RELNAME];
    unsigned int recLength;
    unsigned int recsPerPg;
    unsigned int numAttrs;
    unsigned int numRecs;
    unsigned int numPgs;
} RelCatalogRecord;

typedef struct attrCatalogRecord {
    unsigned int offset;
    unsigned int length;
    datatype type;
    bool unique;
    bool notNull;
    bool primaryKey;
    char attrName[RELNAME];
    char relName[RELNAME];
} AttrCatalogRecord;

extern const CatalogAttributeDefinition RELCAT_SCHEMA[RELCAT_ATTRIBUTE_COUNT];
extern const CatalogAttributeDefinition ATTRCAT_SCHEMA[ATTRCAT_ATTRIBUTE_COUNT];

int EncodeAttributeType(datatype type, bool unique, bool notNull, bool primaryKey);
void DecodeAttributeType(
        int encodedType,
        datatype *type,
        bool *unique,
        bool *notNull,
        bool *primaryKey);

void EncodeRelCatalogRecord(char *destination, const RelCatalogRecord *record);
void DecodeRelCatalogRecord(const char *source, RelCatalogRecord *record);

void EncodeAttrCatalogRecord(char *destination, const AttrCatalogRecord *record);
void DecodeAttrCatalogRecord(const char *source, AttrCatalogRecord *record);

unsigned int CatalogSlotMap(unsigned int recordCount);

struct attrCatalog* BuildAttributeCatalog(
        const CatalogAttributeDefinition *schema,
        unsigned int attributeCount,
        const char *relationName);

#endif /* CATALOG_H_ */
