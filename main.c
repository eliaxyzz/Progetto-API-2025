#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int var;      
    int negated;
} Literal;

typedef struct {
    Literal *lits;
    int count;
    int capacity;
} Clause;

/* ---- piatti ---- */
static char **dishNames = NULL;
static int numDishes = 0, dishCapacity = 0;

/* ---- clausole ---- */
static Clause *clauses = NULL;
static int numClauses = 0, clauseCapacity = 0;

/* ---- stato del solver ---- */
static int *assign = NULL;
static int *trail = NULL;
static int trailSize = 0;

static int getDishIndex(const char *name, int addIfMissing) {
    for (int i = 0; i < numDishes; i++) {
        if (strcmp(dishNames[i], name) == 0) return i;
    }
    if (!addIfMissing) return -1;
    if (numDishes == dishCapacity) {
        dishCapacity = dishCapacity == 0 ? 64 : dishCapacity * 2;
        dishNames = realloc(dishNames, dishCapacity * sizeof(char *));
    }
    dishNames[numDishes] = strdup(name);
    return numDishes++;
}

static void clauseAddLiteral(Clause *c, int var, int negated) {
    if (c->count == c->capacity) {
        c->capacity = c->capacity == 0 ? 4 : c->capacity * 2;
        c->lits = realloc(c->lits, c->capacity * sizeof(Literal));
    }
    c->lits[c->count].var = var;
    c->lits[c->count].negated = negated;
    c->count++;
}

static void addClause(Clause c) {
    if (numClauses == clauseCapacity) {
        clauseCapacity = clauseCapacity == 0 ? 64 : clauseCapacity * 2;
        clauses = realloc(clauses, clauseCapacity * sizeof(Clause));
    }
    clauses[numClauses++] = c;
}


static void parseLineIntoClause(char *line, int isFirstLine) {
    Clause clause = {0};
    char *saveptr = NULL;
    char *tok = strtok_r(line, " \t\r\n", &saveptr);
    while (tok != NULL) {
        if (isFirstLine) {
            getDishIndex(tok, 1);
        } else {
            int negated = 0;
            char *name = tok;
            if (name[0] == '-') {
                negated = 1;
                name++;
            }
            int idx = getDishIndex(name, 0);
            if (idx == -1) {
                fprintf(stderr, "Piatto sconosciuto: %s\n", name);
                exit(EXIT_FAILURE);
            }
            clauseAddLiteral(&clause, idx, negated);
        }
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    if (!isFirstLine && clause.count > 0) {
        addClause(clause);
    }
}

static void readInput(void) {
    char *line = NULL;
    size_t lineCap = 0;
    ssize_t len;
    int firstLine = 1;

    while ((len = getline(&line, &lineCap, stdin)) != -1) {
        int onlyBlank = 1;
        for (ssize_t i = 0; i < len; i++) {
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

static int unitPropagate(int limit) {
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < limit; i++) {
            Clause *c = &clauses[i];
            int unassignedCount = 0, lastVar = -1, lastNeg = 0, satisfied = 0;
            for (int j = 0; j < c->count; j++) {
                int v = c->lits[j].var;
                int neg = c->lits[j].negated;
                if (assign[v] == -1) {
                    unassignedCount++;
                    lastVar = v;
                    lastNeg = neg;
                } else {
                    int litVal = neg ? (assign[v] == 0) : (assign[v] == 1);
                    if (litVal) {
                        satisfied = 1;
                        break;
                    }
                }
            }
            if (satisfied) continue;
            if (unassignedCount == 0) return 0; /* clausola violata */
            if (unassignedCount == 1) {
                assign[lastVar] = lastNeg ? 0 : 1;
                trail[trailSize++] = lastVar;
                progress = 1;
            }
        }
    }
    return 1;
}

static void revertTrail(int trailStart) {
    while (trailSize > trailStart) {
        trailSize--;
        assign[trail[trailSize]] = -1;
    }
}

static int pickUnassignedVar(void) {
    for (int v = 0; v < numDishes; v++) {
        if (assign[v] == -1) return v;
    }
    return -1;
}

static int dpll(int limit) {
    int trailStart = trailSize;

    if (!unitPropagate(limit)) {
        revertTrail(trailStart);
        return 0;
    }

    int chosen = pickUnassignedVar();
    if (chosen == -1) {
        revertTrail(trailStart);
        return 1; /* tutte le variabili assegnate senza conflitti */
    }

    assign[chosen] = 1;
    trail[trailSize++] = chosen;
    if (dpll(limit)) {
        revertTrail(trailStart);
        return 1;
    }

    assign[chosen] = 0;
    trail[trailSize++] = chosen;
    if (dpll(limit)) {
        revertTrail(trailStart);
        return 1;
    }

    revertTrail(trailStart);
    return 0;
}

static int isSatisfiable(int limit) {
    for (int v = 0; v < numDishes; v++) assign[v] = -1;
    trailSize = 0;
    return dpll(limit);
}

int main(void) {
    readInput();

    assign = malloc(sizeof(int) * (numDishes > 0 ? numDishes : 1));
    trail = malloc(sizeof(int) * (numDishes > 0 ? numDishes : 1));

    int removed = 0;
    int limit = numClauses;
    int sat = isSatisfiable(limit);

    if (sat) {
        printf("OK\n");
    } else {
        printf("KO\n");
        while (!sat) {
            removed++;
            limit = numClauses - removed;
            printf("-%d\n", removed);
            sat = isSatisfiable(limit);
        }
        printf("OK\n");
    }

    return 0;
}
