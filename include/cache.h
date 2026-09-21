/*
 * cache.h
 *
 *  Created on: 27-Oct-2014
 *      Author: nithin
 */

#ifndef CACHE_H_
#define CACHE_H_

#include "defs.h"
#include "locking.h"

/* Attribute catalog structure*/
struct attrCatalog {
    unsigned int offset;      //offset of attribute within record
    unsigned int length;      //length of attribute
    datatype type;              //attribute type: i, f, or s
    bool unique;                //whether values must be unique in the relation
    bool notNull;               //whether NULL values are disallowed
    bool primaryKey;            //whether this attribute is the primary key
    unsigned int position;      //zero-based position used by the null bitmap
    char attrName[RELNAME];     //name of attribute
    char relName[RELNAME];      //name of relation

    struct attrCatalog *next;   //Pointer to next
};

/* Relation cache structure */
typedef struct relCache {
    char relName[RELNAME];      //Relation name
    unsigned int recLength;     //record length
    unsigned int recsPerPg;     //records per page
    unsigned int numAttrs;      //number of attributes
    unsigned int numRecs;       //total number of records
    unsigned int numPgs;        //total number of pages
    unsigned int nullBitmapBytes; //bytes appended to each versioned record
    bool hasNullBitmap;         //whether records use the versioned NULL format

    Rid relcatRid;                  //RID
    int relFile;                    //File descriptor for the open relation
    int lockId;                     //Cross-process table lock
    LockMode lockMode;              //Shared for reads, exclusive for writes
    bool dirty;                     //True if record on disk is out-dated
    struct attrCatalog* attrList;   //Linked list of attribute descriptors
} CacheEntry;

#endif /* CACHE_H_ */
