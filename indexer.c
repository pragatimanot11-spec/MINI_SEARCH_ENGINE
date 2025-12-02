#include "indexer.h"

/* Stop words list */
const char *STOP_WORDS[] = {
    "the", "is", "at", "which", "on", "a", "an", "and", "or", "but",
    "in", "with", "to", "for", "of", "as", "by", "that", "this",
    "it", "from", "be", "are", "was", "were", "been", "have", "has",
    NULL
};

DocInfo documents[MAX_DOCS];
int docCount = 0;

/* Check if a word is a stop word */
int isStopWord(const char *word) {
    if (!word || word[0] == '\0') return 1;
    for (int i = 0; STOP_WORDS[i] != NULL; i++) {
        if (strcmp(word, STOP_WORDS[i]) == 0) return 1;
    }
    return 0;
}

void toLowerCase(char *str) {
    for (int i = 0; str[i]; i++) str[i] = (char)tolower((unsigned char)str[i]);
}

void removePunctuation(char *str) {
    int i = 0, j = 0;
    while (str[i]) {
        if (isalnum((unsigned char)str[i]) || isspace((unsigned char)str[i]))
            str[j++] = str[i];
        i++;
    }
    str[j] = '\0';
}

/* djb2 hash */
unsigned long hashFunc(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash % HASH_SIZE;
}

/* create DocNode */
static DocNode *createDocNode(int docId, int position) {
    DocNode *d = malloc(sizeof(DocNode));
    if (!d) { perror("malloc"); exit(1); }
    d->docId = docId;
    d->frequency = 0;
    d->positions = NULL;
    d->posCount = 0;
    d->posCap = 0;
    d->next = NULL;
    if (position >= 0) {
        d->posCap = 8;
        d->positions = malloc(d->posCap * sizeof(int));
        d->positions[0] = position;
        d->posCount = 1;
        d->frequency = 1;
    }
    return d;
}

static void appendPosition(DocNode *d, int pos) {
    if (d->posCount + 1 > d->posCap) {
        int nc = d->posCap == 0 ? 8 : d->posCap * 2;
        d->positions = realloc(d->positions, nc * sizeof(int));
        d->posCap = nc;
    }
    d->positions[d->posCount++] = pos;
    d->frequency++;
}

/* Add or update posting list for a word entry */
static void addOrUpdateDocList(WordEntry *entry, int docId, int position) {
    DocNode *cur = entry->docList;
    while (cur) {
        if (cur->docId == docId) {
            appendPosition(cur, position);
            return;
        }
        cur = cur->next;
    }
    /* not found -> create at head */
    DocNode *newD = createDocNode(docId, position);
    newD->next = entry->docList;
    entry->docList = newD;
    entry->docFrequency++;
}

/* Insert word into hash table (or update existing). */
WordEntry *insertWordHash(WordEntry **hashTable, const char *word, int docId, int position) {
    unsigned long h = hashFunc(word);
    WordEntry *cur = hashTable[h];
    while (cur) {
        if (strcmp(cur->word, word) == 0) {
            addOrUpdateDocList(cur, docId, position);
            return cur;
        }
        cur = cur->next;
    }
    /* not found -> create new entry and insert at head */
    WordEntry *newEntry = malloc(sizeof(WordEntry));
    if (!newEntry) { perror("malloc"); exit(1); }
    strncpy(newEntry->word, word, MAX_WORD_LEN);
    newEntry->word[MAX_WORD_LEN - 1] = '\0';
    newEntry->docList = NULL;
    newEntry->docFrequency = 0;
    newEntry->next = hashTable[h];
    hashTable[h] = newEntry;
    addOrUpdateDocList(newEntry, docId, position);
    return newEntry;
}

WordEntry *findWordEntry(WordEntry **hashTable, const char *word) {
    unsigned long h = hashFunc(word);
    WordEntry *cur = hashTable[h];
    while (cur) {
        if (strcmp(cur->word, word) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void processFile(WordEntry **hashTable, const char *filepath, int docId) {
    FILE *f = fopen(filepath, "r");
    if (!f) { perror(filepath); return; }

    char buf[4096];
    int position = 0;
    while (fgets(buf, sizeof(buf), f)) {
        toLowerCase(buf);
        removePunctuation(buf);
        char *tok = strtok(buf, " \t\r\n");
        while (tok) {
            if (!isStopWord(tok) && strlen(tok) > 0) {
                insertWordHash(hashTable, tok, docId, position);
                documents[docId].totalTerms++;
            }
            position++;
            tok = strtok(NULL, " \t\r\n");
        }
    }
    fclose(f);
}

void indexDocuments(WordEntry **hashTable, const char *folderPath) {
    DIR *dir = opendir(folderPath);
    if (!dir) { perror("opendir"); return; }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".txt") != 0) continue;
        if (docCount >= MAX_DOCS) break;

        documents[docCount].id = docCount;
        snprintf(documents[docCount].filename, sizeof(documents[docCount].filename),
                 "%s/%s", folderPath, entry->d_name);
        documents[docCount].searchCount = 0;
        documents[docCount].totalTerms = 0;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", folderPath, entry->d_name);
        processFile(hashTable, fullpath, docCount);
        printf("Indexed: %s (terms=%d)\n", entry->d_name, documents[docCount].totalTerms);
        docCount++;
    }
    closedir(dir);
}

/* Free posting list */
void freeDocList(DocNode *head) {
    while (head) {
        DocNode *temp = head;
        head = head->next;
        free(temp->positions);
        free(temp);
    }
}

/* Free entire hash table */
void freeHashTable(WordEntry **hashTable) {
    for (int i = 0; i < HASH_SIZE; i++) {
        WordEntry *cur = hashTable[i];
        while (cur) {
            WordEntry *temp = cur;
            cur = cur->next;
            freeDocList(temp->docList);
            free(temp);
        }
        hashTable[i] = NULL;
    }
}
