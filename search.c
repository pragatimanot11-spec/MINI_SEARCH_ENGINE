#include "indexer.h"
#include <string.h>   // For strncpy, strlen, strtok, and the explicit strdup prototype
#include <stdlib.h>   // For strdup (which performs dynamic memory allocation)
#include <strings.h>  // For strcasecmp (case-insensitive comparison)
/* Utility: collect docIds from a posting list into a dynamically allocated array.
    Returns number of docs in *nResults and an allocated int* (caller frees). */
static int *collectDocIds(DocNode *list, int *nResults) {
    int count = 0;
    DocNode *cur = list;
    while (cur) { count++; cur = cur->next; }
    int *arr = malloc(sizeof(int) * count);
    cur = list;
    for (int i = 0; i < count; i++) { arr[i] = cur->docId; cur = cur->next; }
    *nResults = count;
    return arr;
}

/* set operations on sorted arrays of ints (docs) */
/* helper: sort small arrays of ints ascending */
static void sortIntArray(int *a, int n) {
    for (int i = 0; i < n - 1; ++i)
        for (int j = i + 1; j < n; ++j)
            if (a[j] < a[i]) { int t = a[i]; a[i] = a[j]; a[j] = t; }
}

/* intersection */
static int *intersectArrays(int *A, int nA, int *B, int nB, int *nOut) {
    sortIntArray(A, nA); sortIntArray(B, nB);
    int *res = malloc(sizeof(int) * (nA < nB ? nA : nB));
    int i=0,j=0,k=0;
    while (i<nA && j<nB) {
        if (A[i]==B[j]) { res[k++]=A[i]; i++; j++; }
        else if (A[i]<B[j]) i++; else j++;
    }
    while (i<nA) res[k++]=A[i++];
    while (j<nB) res[k++]=B[j++];
    *nOut = k; return res;
}

/* union */
static int *unionArrays(int *A, int nA, int *B, int nB, int *nOut) {
    sortIntArray(A, nA); sortIntArray(B, nB);
    int *res = malloc(sizeof(int) * (nA + nB));
    int i=0,j=0,k=0;
    while (i<nA && j<nB) {
        if (A[i]==B[j]) { res[k++]=A[i]; i++; j++; }
        else if (A[i]<B[j]) res[k++]=A[i++]; else res[k++]=B[j++];
    }
    while (i<nA) res[k++]=A[i++];
    while (j<nB) res[k++]=B[j++];
    *nOut = k; return res;
}

/* difference A \ B */
static int *differenceArrays(int *A, int nA, int *B, int nB, int *nOut) {
    sortIntArray(A, nA); sortIntArray(B, nB);
    int *res = malloc(sizeof(int) * nA);
    int i=0,j=0,k=0;
    while (i<nA && j<nB) {
        if (A[i]==B[j]) { i++; j++; }
        else if (A[i]<B[j]) res[k++]=A[i++];
        else j++;
    }
    while (i<nA) res[k++]=A[i++];
    *nOut = k; return res;
}

/* phrase search: check if phrase (words[]) occurs in docId using position lists */
static int phraseInDoc(WordEntry **hashTable, char **words, int wcount, int docId) {
    if (wcount == 0) return 0;
    WordEntry *first = findWordEntry(hashTable, words[0]);
    if (!first) return 0;
    /* find posting for docId in first */
    DocNode *d0 = first->docList;
    while (d0 && d0->docId != docId) d0 = d0->next;
    if (!d0) return 0;
    /* for each position p in d0, check if subsequent words have p+1, p+2 ... */
    for (int pi = 0; pi < d0->posCount; pi++) {
        int base = d0->positions[pi];
        int ok = 1;
        for (int k = 1; k < wcount; k++) {
            WordEntry *we = findWordEntry(hashTable, words[k]);
            if (!we) { ok = 0; break; }
            DocNode *dk = we->docList;
            while (dk && dk->docId != docId) dk = dk->next;
            if (!dk) { ok = 0; break; }
            /* check if dk->positions contains base + k */
            int found = 0;
            for (int m = 0; m < dk->posCount; m++)
                if (dk->positions[m] == base + k) { found = 1; break; }
            if (!found) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

/* compute TF-IDF scores for provided doc list (docs[]) for terms in queryWords[] */
typedef struct Score {
    int docId;
    double score;
} Score;

static Score *computeTfIdfScores(WordEntry **hashTable, char **queryWords, int qwCount, int *docs, int docCountLocal, int *outCount) {
    /* allocate scores */
    Score *arr = malloc(sizeof(Score) * docCountLocal);
    for (int i = 0; i < docCountLocal; i++) arr[i].docId = docs[i], arr[i].score = 0.0;
    int N = docCount; /* global number of docs indexed */
    for (int t = 0; t < qwCount; t++) {
        WordEntry *we = findWordEntry(hashTable, queryWords[t]);
        if (!we) continue;
        int df = we->docFrequency;
        if (df == 0) continue;
        double idf = log((double)N / (double)df);
        /* for each doc in docs[], find tf (term frequency in that doc) */
        for (int i = 0; i < docCountLocal; i++) {
            int did = docs[i];
            DocNode *d = we->docList;
            while (d && d->docId != did) d = d->next;
            if (d) {
                double tf = (double)d->frequency;
                /* optional normalization by doc length */
                double norm = documents[did].totalTerms > 0 ? (double)documents[did].totalTerms : 1.0;
                arr[i].score += (tf / norm) * idf;
            }
        }
    }
    *outCount = docCountLocal;
    return arr;
}

/* sort scores descending */
static void sortScores(Score *scores, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (scores[j].score > scores[i].score) {
                Score tmp = scores[i]; scores[i] = scores[j]; scores[j] = tmp;
            }
}

/* parse a simple query supporting:
    - quoted phrase: "this is phrase"
    - boolean operators: AND, OR, NOT (left-to-right, NOT applied to terms)
    process tokens left-to-right with AND/OR combining. */
void printResultsForQuery(WordEntry **hashTable, const char *rawQuery) {
    if (!rawQuery || strlen(rawQuery) == 0) { printf("Empty query\n"); return; }

    /* Copy query and tokenize while preserving quoted phrases */
    char qcopy[1024] = {0}; 
    strncpy(qcopy, rawQuery, sizeof(qcopy) - 1);
    qcopy[sizeof(qcopy)-1]='\0';

    /* vector of intermediate docId sets */
    int *currentDocs = NULL;
    int currentCount = 0;
    // Removed unused variable 'expectOp'

    char *p = qcopy;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        if (*p == '"') {
            /* phrase */
            p++;
            char phrase[512] = {0};
            int idx = 0;
            while (*p && *p != '"' && idx + 1 < (int)sizeof(phrase)) phrase[idx++] = *p++;
            phrase[idx] = '\0';
            if (*p == '"') p++;
            /* split phrase into words */
            char *words[64]; int wcount = 0;
            char phcopy[512] = {0}; 
            strncpy(phcopy, phrase, sizeof(phcopy) - 1);
            phcopy[sizeof(phcopy)-1] = '\0';
            toLowerCase(phcopy); removePunctuation(phcopy);
            char *tk = strtok(phcopy, " \t\r\n");
            while (tk && wcount < 64) {
                if (!isStopWord(tk)) {
                    words[wcount++] = strdup(tk);
                }
                tk = strtok(NULL, " \t\r\n");
            }
            /* collect documents that contain entire phrase */
            int *phraseDocs = malloc(sizeof(int) * docCount);
            int pd = 0;
            for (int d = 0; d < docCount; d++) {
                if (phraseInDoc(hashTable, words, wcount, d)) phraseDocs[pd++] = d;
            }
            /* free words */
            for (int k = 0; k < wcount; k++) free(words[k]);

            /* combine with currentDocs (if present) - default initial */
            if (!currentDocs) { currentDocs = phraseDocs; currentCount = pd; }
            else {
                int nOut;
                int *res = intersectArrays(currentDocs, currentCount, phraseDocs, pd, &nOut);
                free(currentDocs); free(phraseDocs);
                currentDocs = res; currentCount = nOut;
            }
            // expectOp = 1;
        } else {
            /* read next token (up to space) */
            char tok[256] = {0}; 
            int ti = 0;
            while (*p && !isspace((unsigned char)*p)) { if (ti+1 < (int)sizeof(tok)) tok[ti++]=*p; p++; }
            tok[ti]='\0';
            /* check if operator */
            if (strcasecmp(tok, "AND")==0 || strcasecmp(tok, "OR")==0) {
                char op[8]; strncpy(op, tok, sizeof(op)); op[7]='\0';
                while (isspace((unsigned char)*p)) p++;
                if (*p == '"') {
                    p++; char phrase[512] = {0}; int idx=0;
                    while (*p && *p != '"' && idx+1 < (int)sizeof(phrase)) phrase[idx++] = *p++;
                    phrase[idx]='\0'; if (*p=='"') p++;
                    char *words[64]; int wcount=0;
                    char phcopy[512] = {0}; strncpy(phcopy, phrase, sizeof(phcopy)-1); phcopy[sizeof(phcopy)-1]=0;
                    toLowerCase(phcopy); removePunctuation(phcopy);
                    char *tk = strtok(phcopy, " \t\r\n");
                    while (tk && wcount < 64) { if (!isStopWord(tk)) words[wcount++]=strdup(tk); tk = strtok(NULL, " \t\r\n"); }
                    int *phraseDocs = malloc(sizeof(int) * docCount); int pd=0;
                    for (int d=0; d<docCount; d++) if (phraseInDoc(hashTable, words, wcount, d)) phraseDocs[pd++]=d;
                    for (int k=0;k<wcount;k++) free(words[k]);
                    if (!currentDocs) { currentDocs = phraseDocs; currentCount = pd; }
                    else {
                        int nOut; int *res = NULL;
                        if (strcasecmp(op,"AND")==0) res = intersectArrays(currentDocs, currentCount, phraseDocs, pd, &nOut);
                        else res = unionArrays(currentDocs, currentCount, phraseDocs, pd, &nOut);
                        free(currentDocs); free(phraseDocs);
                        currentDocs = res; currentCount = nOut;
                    }
                    // expectOp = 1;
                } else {
                    char nextTok[256] = {0}; int nti=0;
                    while (*p && !isspace((unsigned char)*p)) { if (nti+1 < (int)sizeof(nextTok)) nextTok[nti++]=*p; p++; }
                    nextTok[nti]=0;
                    toLowerCase(nextTok); removePunctuation(nextTok);
                    if (isStopWord(nextTok) || strlen(nextTok)==0) {
                        continue;
                    }
                    WordEntry *we = findWordEntry(hashTable, nextTok);
                    int *nextDocs; int nd=0;
                    if (!we) { nextDocs = malloc(sizeof(int)*0); nd=0; }
                    else nextDocs = collectDocIds(we->docList, &nd);
                    if (!currentDocs) { currentDocs = nextDocs; currentCount = nd; }
                    else {
                        int nOut; int *res;
                        if (strcasecmp(op,"AND")==0) res = intersectArrays(currentDocs, currentCount, nextDocs, nd, &nOut);
                        else res = unionArrays(currentDocs, currentCount, nextDocs, nd, &nOut);
                        free(currentDocs); free(nextDocs);
                        currentDocs = res; currentCount = nOut;
                    }
                    // expectOp = 1;
                }
            } else if (strcasecmp(tok, "NOT")==0) {
                while (isspace((unsigned char)*p)) p++;
                if (*p == '"') {
                    p++; char phrase[512] = {0}; int idx=0;
                    while (*p && *p != '"' && idx+1 < (int)sizeof(phrase)) phrase[idx++] = *p++;
                    phrase[idx]='\0'; if (*p=='"') p++;
                    char *words[64]; int wcount=0;
                    char phcopy[512] = {0}; strncpy(phcopy, phrase, sizeof(phcopy)-1); phcopy[sizeof(phcopy)-1]=0;
                    toLowerCase(phcopy); removePunctuation(phcopy);
                    char *tk = strtok(phcopy, " \t\r\n");
                    while (tk && wcount < 64) { if (!isStopWord(tk)) words[wcount++]=strdup(tk); tk = strtok(NULL, " \t\r\n"); }
                    int *phraseDocs = malloc(sizeof(int) * docCount); int pd=0;
                    for (int d=0; d<docCount; d++) if (phraseInDoc(hashTable, words, wcount, d)) phraseDocs[pd++]=d;
                    for (int k=0;k<wcount;k++) free(words[k]);
                    int *allDocs = malloc(sizeof(int) * docCount);
                    for (int i=0;i<docCount;i++) allDocs[i]=i;
                    int *newCur; int nOut;
                    if (!currentDocs) {
                        newCur = differenceArrays(allDocs, docCount, phraseDocs, pd, &nOut);
                        free(allDocs); free(phraseDocs);
                        currentDocs = newCur; currentCount = nOut;
                    } else {
                        newCur = differenceArrays(currentDocs, currentCount, phraseDocs, pd, &nOut);
                        free(currentDocs); free(phraseDocs);
                        currentDocs = newCur; currentCount = nOut;
                    }
                    // expectOp = 1;
                } else {
                    char nextTok[256] = {0}; int nti=0;
                    while (*p && !isspace((unsigned char)*p)) { if (nti+1 < (int)sizeof(nextTok)) nextTok[nti++]=*p; p++; }
                    nextTok[nti]=0;
                    toLowerCase(nextTok); removePunctuation(nextTok);
                    if (isStopWord(nextTok) || strlen(nextTok)==0) continue;
                    WordEntry *we = findWordEntry(hashTable, nextTok);
                    int *excludeDocs; int ed=0;
                    if (!we) { excludeDocs = malloc(sizeof(int)*0); ed=0; }
                    else excludeDocs = collectDocIds(we->docList, &ed);
                    int *allDocs = malloc(sizeof(int) * docCount);
                    for (int i=0;i<docCount;i++) allDocs[i]=i;
                    int *newCur; int nOut;
                    if (!currentDocs) {
                        newCur = differenceArrays(allDocs, docCount, excludeDocs, ed, &nOut);
                        free(allDocs); free(excludeDocs);
                        currentDocs = newCur; currentCount = nOut;
                    } else {
                        newCur = differenceArrays(currentDocs, currentCount, excludeDocs, ed, &nOut);
                        free(currentDocs); free(excludeDocs);
                        currentDocs = newCur; currentCount = nOut;
                    }
                    // expectOp = 1;
                }
            } else {
                /* Normal word token */
                char w[256]; strncpy(w, tok, sizeof(w)); w[sizeof(w)-1]=0;
                toLowerCase(w); removePunctuation(w);
                if (isStopWord(w) || strlen(w)==0) continue;
                WordEntry *we = findWordEntry(hashTable, w);
                int *nextDocs; int nd=0;
                if (!we) { nextDocs = malloc(sizeof(int)*0); nd=0; }
                else nextDocs = collectDocIds(we->docList, &nd);
                if (!currentDocs) { currentDocs = nextDocs; currentCount = nd; }
                else {
                    /* default combine is AND */
                    int nOut; int *res = intersectArrays(currentDocs, currentCount, nextDocs, nd, &nOut);
                    free(currentDocs); free(nextDocs);
                    currentDocs = res; currentCount = nOut;
                }
                // expectOp = 1;
            }
        }
    } /* end parsing tokens */

    if (!currentDocs || currentCount == 0) {
        printf("No results for '%s'\n", rawQuery);
        if (currentDocs) free(currentDocs);
        return;
    }

    /* Prepare query words for TF-IDF scoring: extract words (non-stop) from rawQuery */
    char qcopy2[1024] = {0}; 
    strncpy(qcopy2, rawQuery, sizeof(qcopy2) - 1); 
    qcopy2[sizeof(qcopy2)-1]=0;
    toLowerCase(qcopy2); removePunctuation(qcopy2);
    char *qtk = strtok(qcopy2, " \t\r\n");
    char *qwords[128]; int qwCount = 0;
    while (qtk && qwCount < 128) {
        if (!isStopWord(qtk)) qwords[qwCount++] = strdup(qtk);
        qtk = strtok(NULL, " \t\r\n");
    }

    /* compute tf-idf scores for currentDocs */
    int outCount;
    Score *scores = computeTfIdfScores(hashTable, qwords, qwCount, currentDocs, currentCount, &outCount);

    /* sort scores */
    sortScores(scores, outCount);

    /* print top-k */
    int k = TOP_K < outCount ? TOP_K : outCount;
    printf("Top %d results for '%s':\n", k, rawQuery);
    for (int i = 0; i < k; i++) {
        int id = scores[i].docId;
        printf("  %s (score=%.6f)\n", documents[id].filename, scores[i].score);
        documents[id].searchCount++;
    }

    /* clean up */
    for (int i = 0; i < qwCount; i++) free(qwords[i]);
    free(scores); free(currentDocs);
}