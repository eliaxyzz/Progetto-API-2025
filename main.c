#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- dizionario piatti: hash table indirizzamento aperto, nome->indice ---- */
typedef struct {
    const char *key; /* punta a dishNames[]      */
    int idx;          /* indice piatto, -1 = slot libero */
} DishSlot;

static char **dishNames = NULL;
static int numDishes = 0, dishNamesCap = 0;

static DishSlot *dishTable = NULL;
static size_t dishTableCap = 0;
static size_t dishTableCount = 0;

/* Funzione di hash per i nomi dei piatti. */
static size_t hashName(const char *s) {
    size_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}
/* Inizializza la hash table dei piatti. */
static void dishTableInit(size_t hintCap) {
    dishTableCap = 16;
    while (dishTableCap < hintCap) dishTableCap <<= 1;
    dishTable = malloc(dishTableCap * sizeof(DishSlot));
    for (size_t i = 0; i < dishTableCap; i++) dishTable[i].idx = -1;
    dishTableCount = 0;
}

/* Cresce la hash table dei piatti. */
static void dishTableGrow(void) {
    DishSlot *old = dishTable;
    size_t oldCap = dishTableCap;
    dishTableCap *= 2;
    dishTable = malloc(dishTableCap * sizeof(DishSlot));
    for (size_t i = 0; i < dishTableCap; i++) dishTable[i].idx = -1;
    size_t mask = dishTableCap - 1;
    for (size_t i = 0; i < oldCap; i++) {
        if (old[i].idx == -1) continue;
        size_t h = hashName(old[i].key) & mask;
        while (dishTable[h].idx != -1) h = (h + 1) & mask;
        dishTable[h] = old[i];
    }
    free(old);
}

/* Lookup nome->indice; inserisce se addIfMissing e assente. -1 se assente
 * e !addIfMissing. O(1) ammortizzato. */
static int dishFindOrAdd(const char *name, int addIfMissing) {
    if (!dishTable) dishTableInit(16);
    size_t mask = dishTableCap - 1;
    size_t h = hashName(name) & mask;
    while (dishTable[h].idx != -1) {
        if (strcmp(dishTable[h].key, name) == 0) return dishTable[h].idx;
        h = (h + 1) & mask;
    }
    if (!addIfMissing) return -1;

    if ((dishTableCount + 1) * 2 > dishTableCap) {
        dishTableGrow();
        return dishFindOrAdd(name, addIfMissing);
    }
    if (numDishes == dishNamesCap) {
        dishNamesCap = dishNamesCap == 0 ? 64 : dishNamesCap * 2;
        dishNames = realloc(dishNames, dishNamesCap * sizeof(char *));
    }
    char *stored = strdup(name);
    dishNames[numDishes] = stored;
    dishTable[h].key = stored;
    dishTable[h].idx = numDishes;
    dishTableCount++;
    return numDishes++;
}

/* ---- clausole, formato CSR ----
 * litPool: letterali di tutte le clausole concatenati.
 * clauseStart[c]..clauseStart[c+1]: intervallo della clausola c in litPool.
 * Letterale = intero con segno: v+1 gradito, -(v+1) sgradito. */
static int *litPool = NULL;
static size_t litPoolSize = 0, litPoolCap = 0;

static int *clauseStart = NULL; /* lunghezza numClauses+1 */
static int numClauses = 0;
static int clauseStartCap = 0;

static void litPoolPush(int lit) {
    if (litPoolSize == litPoolCap) {
        litPoolCap = litPoolCap == 0 ? 1024 : litPoolCap * 2;
        litPool = realloc(litPool, litPoolCap * sizeof(int));
    }
    litPool[litPoolSize++] = lit;
}

static void clauseStartPush(int offset) {
    if (numClauses + 1 >= clauseStartCap) {
        clauseStartCap = clauseStartCap == 0 ? 1024 : clauseStartCap * 2;
        clauseStart = realloc(clauseStart, clauseStartCap * sizeof(int));
    }
    clauseStart[numClauses + 1] = offset;
}

static inline int clauseLen(int c) { return clauseStart[c + 1] - clauseStart[c]; }

/* Tokenizza una riga: registra i piatti o accoda una
 * clausola in litPool/clauseStart. */
static void parseLineIntoClause(char *line, int isFirstLine) {
    char *saveptr = NULL;
    char *tok = strtok_r(line, " \t\r\n", &saveptr);
    size_t litsBefore = litPoolSize;

    while (tok != NULL) {
        if (isFirstLine) {
            dishFindOrAdd(tok, 1);
        } else {
            int negated = 0;
            char *name = tok;
            if (name[0] == '-') {
                negated = 1;
                name++;
            }
            int idx = dishFindOrAdd(name, 0);
            if (idx == -1) {
                fprintf(stderr, "Piatto sconosciuto: %s\n", name);
                exit(EXIT_FAILURE);
            }
            litPoolPush(negated ? -(idx + 1) : (idx + 1));
        }
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    if (!isFirstLine && litPoolSize > litsBefore) {
        clauseStartPush((int)litPoolSize);
        numClauses++;
    }
}

/* Sostituto di getline: fgets a
 * blocchi, buffer a raddoppio.*/
static long readLine(char **lineptr, size_t *cap, FILE *stream) {
    if (*lineptr == NULL || *cap == 0) {
        *cap = 256;
        *lineptr = malloc(*cap);
    }
    size_t len = 0;
    for (;;) {
        if (len + 2 > *cap) {
            *cap *= 2;
            *lineptr = realloc(*lineptr, *cap);
        }
        if (!fgets(*lineptr + len, (int)(*cap - len), stream)) {
            if (len == 0) return -1;
            break;
        }
        size_t chunkLen = strlen(*lineptr + len);
        len += chunkLen;
        if (len > 0 && (*lineptr)[len - 1] == '\n') break;
        if (feof(stream)) break;
    }
    return (long)len;
}

/* Legge l'input da stdin e lo processa. */
static void readInput(void) {
    char *line = NULL;
    size_t lineCap = 0;
    long len;
    int firstLine = 1;

    clauseStartCap = 1024;
    clauseStart = malloc(clauseStartCap * sizeof(int));
    clauseStart[0] = 0;

    while ((len = readLine(&line, &lineCap, stdin)) != -1) {
        int onlyBlank = 1;
        for (long i = 0; i < len; i++) {
            if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n') {
                onlyBlank = 0;
                break;
            }
        }
        if (onlyBlank) continue;

        parseLineIntoClause(line, firstLine);
        firstLine = 0;
    }
    free(line);
}

/* ---- liste di occorrenze, formato CSR ----
 * code = var*2 + polarita' (0 positivo, 1 negato).
 * occStart[code]..occStart[code+1] indicizza in occList le clausole che
 * contengono quel letterale. Costruite una volta con counting sort, O(L). */
static int *occStart = NULL; /* dimensione 2*numDishes+1 */
static int *occList = NULL;  /* dimensione litPoolSize   */

static void buildOccurrences(void) {
    int codes = 2 * (numDishes > 0 ? numDishes : 1);
    occStart = calloc(codes + 1, sizeof(int));
    occList = malloc((litPoolSize > 0 ? litPoolSize : 1) * sizeof(int));

    for (size_t i = 0; i < litPoolSize; i++) {
        int lit = litPool[i];
        int var = (lit > 0 ? lit : -lit) - 1;
        int code = var * 2 + (lit > 0 ? 0 : 1);
        occStart[code + 1]++;
    }
    for (int i = 0; i < codes; i++) occStart[i + 1] += occStart[i];

    int *cursor = malloc((codes > 0 ? codes : 1) * sizeof(int));
    memcpy(cursor, occStart, codes * sizeof(int));

    for (int c = 0; c < numClauses; c++) {
        for (int p = clauseStart[c]; p < clauseStart[c + 1]; p++) {
            int lit = litPool[p];
            int var = (lit > 0 ? lit : -lit) - 1;
            int code = var * 2 + (lit > 0 ? 0 : 1);
            occList[cursor[code]++] = c;
        }
    }
    free(cursor);
}

/* Euristiche statiche, precalcolate una volta:
 * varOrder = variabili per occorrenze decrescenti (piu' vincolate prima);
 * firstTryValue = polarita' piu' frequente (fase gloutona). */
static int *varOrder = NULL;
static int *firstTryValue = NULL;
static int *g_occCountForSort = NULL;

static int cmpByOccDesc(const void *a, const void *b) {
    int va = *(const int *)a, vb = *(const int *)b;
    int diff = g_occCountForSort[vb] - g_occCountForSort[va];
    if (diff != 0) return diff;
    return va - vb;
}

static void buildVarHeuristics(void) {
    varOrder = malloc((numDishes > 0 ? numDishes : 1) * sizeof(int));
    firstTryValue = malloc((numDishes > 0 ? numDishes : 1) * sizeof(int));
    int *occCount = malloc((numDishes > 0 ? numDishes : 1) * sizeof(int));

    for (int v = 0; v < numDishes; v++) {
        int pos = occStart[2 * v + 1] - occStart[2 * v];
        int neg = occStart[2 * v + 2] - occStart[2 * v + 1];
        occCount[v] = pos + neg;
        firstTryValue[v] = (pos >= neg) ? 1 : 0;
        varOrder[v] = v;
    }
    g_occCountForSort = occCount;
    qsort(varOrder, numDishes, sizeof(int), cmpByOccDesc);
    free(occCount);
}

/* ---- stato del solver DPLL ---- */
static int *assign = NULL;         /* -1 = non assegnato, 0/1 = valore */
static int *trail = NULL;          /* variabili assegnate, in ordine   */
static int trailSize = 0;

static int *unassignedCount = NULL; /* letterali non assegnati, per clausola */
static int *trueCount = NULL;       /* letterali veri, per clausola          */

static int *propQueue = NULL;
static int propQueueCap = 0;
static int queueHead = 0, queueTail = 0;

static int currentLimit = 0; /* clausole attive nel tentativo corrente (prefisso) */

static void queuePush(int code) {
    if (queueTail == propQueueCap) {
        propQueueCap = propQueueCap == 0 ? 1024 : propQueueCap * 2;
        propQueue = realloc(propQueue, propQueueCap * sizeof(int));
    }
    propQueue[queueTail++] = code;
}

/* Individua l'unico letterale non assegnato di una clausola unitaria.
 * O(lunghezza clausola). */
static void findUnitLiteral(int c, int *outVar, int *outVal) {
    for (int p = clauseStart[c]; p < clauseStart[c + 1]; p++) {
        int lit = litPool[p];
        int v = (lit > 0 ? lit : -lit) - 1;
        if (assign[v] == -1) {
            *outVar = v;
            *outVal = (lit > 0) ? 1 : 0;
            return;
        }
    }
}

/* Assegna var=value, propaga alle clausole in occStart/occList, accoda le
 * nuove implicazioni unitarie. Ritorna 0 se una clausola risulta violata.
 * O(occorrenze di var). */
static int applyAssignment(int var, int value) {
    assign[var] = value;
    trail[trailSize++] = var;
    int ok = 1;

    for (int pol = 0; pol < 2; pol++) {
        int code = var * 2 + pol;
        int litIsTrue = (pol == 0) ? (value == 1) : (value == 0);
        for (int i = occStart[code]; i < occStart[code + 1]; i++) {
            int c = occList[i];
            if (c >= currentLimit) continue;
            if (litIsTrue) {
                trueCount[c]++;
                unassignedCount[c]--;
            } else {
                unassignedCount[c]--;
            }
            if (trueCount[c] == 0) {
                if (unassignedCount[c] == 0) {
                    ok = 0;
                } else if (unassignedCount[c] == 1) {
                    int iv = -1, ival = -1;
                    findUnitLiteral(c, &iv, &ival);
                    queuePush(iv * 2 + ival);
                }
            }
        }
    }
    return ok;
}

/* Backtrack fino a trailStart (escluso). Inverso di applyAssignment. */
static void revertTrail(int trailStart) {
    while (trailSize > trailStart) {
        int var = trail[--trailSize];
        int value = assign[var];
        for (int pol = 0; pol < 2; pol++) {
            int code = var * 2 + pol;
            int litIsTrue = (pol == 0) ? (value == 1) : (value == 0);
            for (int i = occStart[code]; i < occStart[code + 1]; i++) {
                int c = occList[i];
                if (c >= currentLimit) continue;
                if (litIsTrue) {
                    trueCount[c]--;
                    unassignedCount[c]++;
                } else {
                    unassignedCount[c]++;
                }
            }
        }
        assign[var] = -1;
    }
}

/* Drena propQueue applicando ogni implicazione. Ritorna 0 al primo
 * conflitto (diretto o implicazioni contraddittorie). */
static int propagateQueue(void) {
    while (queueHead < queueTail) {
        int code = propQueue[queueHead++];
        int var = code >> 1, val = code & 1;
        if (assign[var] == val) continue;
        if (assign[var] == (1 - val)) return 0;
        if (!applyAssignment(var, val)) return 0;
    }
    return 1;
}

/* Decisione var=value seguita da propagazione a fixpoint. */
static int decideAndPropagate(int var, int value) {
    queueHead = queueTail = 0;
    if (!applyAssignment(var, value)) return 0;
    return propagateQueue();
}

/* Prima variabile non assegnata in varOrder. O(numDishes). */
static int pickUnassignedVar(void) {
    for (int i = 0; i < numDishes; i++) {
        int v = varOrder[i];
        if (assign[v] == -1) return v;
    }
    return -1;
}

/* DPLL ricorsivo: backtracking cronologico, ordine/polarita' da euristica statica. */
static int dpll(void) {
    int var = pickUnassignedVar();
    if (var == -1) return 1;

    int trailStart = trailSize;
    int first = firstTryValue[var];

    if (decideAndPropagate(var, first)) {
        if (dpll()) return 1;
    }
    revertTrail(trailStart);

    if (decideAndPropagate(var, 1 - first)) {
        if (dpll()) return 1;
    }
    revertTrail(trailStart);

    return 0;
}

/* SAT check sul prefisso [0,limit): reinizializza assign/trail/contatori,
 * propaga le clausole gia' unitarie, poi lancia dpll. */
static int isSatisfiable(int limit) {
    currentLimit = limit;
    for (int v = 0; v < numDishes; v++) assign[v] = -1;
    trailSize = 0;
    for (int c = 0; c < limit; c++) {
        unassignedCount[c] = clauseLen(c);
        trueCount[c] = 0;
    }

    queueHead = queueTail = 0;
    for (int c = 0; c < limit; c++) {
        int len = clauseLen(c);
        if (len == 0) return 0;
        if (len == 1) {
            int lit = litPool[clauseStart[c]];
            int v = (lit > 0 ? lit : -lit) - 1;
            queuePush(v * 2 + (lit > 0 ? 1 : 0));
        }
    }
    if (!propagateQueue()) return 0;

    return dpll();
}

/* Stampa "-k\n": conversione manuale, un solo fwrite. */
static void printRemoved(int k) {
    char buf[16];
    int pos = 15;
    buf[pos--] = '\n';
    if (k == 0) {
        buf[pos--] = '0';
    } else {
        while (k > 0) {
            buf[pos--] = (char)('0' + (k % 10));
            k /= 10;
        }
    }
    buf[pos] = '-';
    fwrite(buf + pos, 1, 16 - pos, stdout);
}

/* Libera tutte le allocazioni globali. */
static void freeAll(void) {
    for (int i = 0; i < numDishes; i++) free(dishNames[i]);
    free(dishNames);
    free(dishTable);
    free(litPool);
    free(clauseStart);
    free(occStart);
    free(occList);
    free(varOrder);
    free(firstTryValue);
    free(assign);
    free(trail);
    free(unassignedCount);
    free(trueCount);
    free(propQueue);
}

int main(void) {
    setvbuf(stdin, NULL, _IOFBF, 1 << 20);
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);

    readInput();
    buildOccurrences();
    buildVarHeuristics();

    assign = malloc((numDishes > 0 ? numDishes : 1) * sizeof(int));
    trail = malloc((numDishes > 0 ? numDishes : 1) * sizeof(int));
    unassignedCount = malloc((numClauses > 0 ? numClauses : 1) * sizeof(int));
    trueCount = malloc((numClauses > 0 ? numClauses : 1) * sizeof(int));

    if (isSatisfiable(numClauses)) {
        printf("OK\n");
    } else {
        printf("KO\n");

        /* Soddisfacibilita' monotona sui prefissi: ricerca binaria del
         * prefisso massimo soddisfacibile.*/
        int lo = 0, hi = numClauses;
        while (hi - lo > 1) {
            int mid = lo + (hi - lo) / 2;
            if (isSatisfiable(mid)) lo = mid; else hi = mid;
        }

        int removedNeeded = numClauses - lo;
        for (int k = 1; k <= removedNeeded; k++) printRemoved(k);
        printf("OK\n");
    }

    freeAll();
    return 0;
}
