#ifndef INDEXER_H
#define INDEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>

#define MAX_WORD_LEN 100
#define MAX_DOCS 100
#define MAX_FILENAME_LEN 500
#define HASH_SIZE 20011   /* larger prime for hash table */
#define TOP_K 10

/* ---------------- Struct Definitions ---------------- */

typedef struct DocNode {
    int docId;
    int frequency;          /* term frequency in this doc */
    int *positions;         /* dynamic array of positions (word offsets) */
    int posCount;
    int posCap;
    struct DocNode *next;
} DocNode;

/* Word entry stored in a bucket's linked list */
typedef struct WordEntry {
    char word[MAX_WORD_LEN];
    DocNode *docList;
    int docFrequency;       /* number of documents containing this term */
    struct WordEntry *next; /* next in bucket chain */
} WordEntry;

typedef struct {
    int id;
    char filename[MAX_FILENAME_LEN];
    int searchCount;
    int totalTerms;         /* total number of tokens in doc (for normalization) */
} DocInfo;

/* ---------------- Globals & Declarations ---------------- */

extern DocInfo documents[MAX_DOCS];
extern int docCount;

/* indexer */
int isStopWord(const char *word);
void toLowerCase(char *str);
void removePunctuation(char *str);
unsigned long hashFunc(const char *str);
WordEntry *insertWordHash(WordEntry **hashTable, const char *word, int docId, int position);
void processFile(WordEntry **hashTable, const char *filepath, int docId);
void indexDocuments(WordEntry **hashTable, const char *folderPath);

/* search */
WordEntry *findWordEntry(WordEntry **hashTable, const char *word);
/* prints results (top-k) for a query (single term/phrase/multi-term boolean/TF-IDF) */
void printResultsForQuery(WordEntry **hashTable, const char *query);

/* cleanup */
void freeDocList(DocNode *head);
void freeHashTable(WordEntry **hashTable);

#endif
