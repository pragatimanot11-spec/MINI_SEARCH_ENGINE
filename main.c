#include "indexer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 

/* prototypes from search.c */
void printResultsForQuery(WordEntry **hashTable, const char *query);

int main(int argc, char *argv[]) {
    // CRITICAL FIX: Check for the required directory argument
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <document_directory_path>\n", argv[0]);
        return 1;
    }

    WordEntry *hashTable[HASH_SIZE];
    for (int i = 0; i < HASH_SIZE; i++) hashTable[i] = NULL;

    printf("Building index (hash table, positions, TF-IDF support)...\n");

    // Pass the command-line argument (argv[1]) to the indexer
    const char *docPath = argv[1];
    indexDocuments(hashTable, docPath); 

    printf("Indexing complete. Total docs: %d\n", docCount);

    char query[1024];
    while (1) {
        printf("\nEnter search (single-word, phrase \"...\", boolean using AND/OR/NOT) or 'exit':\n> ");
        if (!fgets(query, sizeof(query), stdin)) break;
        query[strcspn(query, "\n")] = '\0';
        if (strcmp(query, "exit") == 0) break;
        if (strlen(query) == 0) continue;
        printResultsForQuery(hashTable, query);
    }

    freeHashTable(hashTable);
    printf("Goodbye!\n");
    return 0;
}