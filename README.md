# Progetto API 2025 – Il Catering Impossibile

Prova finale di **Algoritmi e Principi dell'Informatica**, a.a. 2025/2026.

Data una cena aziendale, un elenco di piatti disponibili e le richieste (categoriche, in forma di
clausola) di ogni dipendente, si stabilisce se esiste un menu che soddisfi tutti i dipendenti
contemporaneamente. Se non esiste, si scartano le richieste a partire dal dipendente più in basso
in gerarchia, una alla volta, fino a trovare un sottoinsieme soddisfacibile.

Il progetto è scritto in **C11**, senza dipendenze esterne, ed è ottimizzato per tempo di
esecuzione e occupazione di memoria: sono questi i due criteri su cui il progetto viene valutato.

---

## Indice

1. [Il problema](#1-il-problema)
2. [Formato di input e output](#2-formato-di-input-e-output)
3. [Architettura del codice](#3-architettura-del-codice)
4. [Strutture dati](#4-strutture-dati)
5. [Strategie e scelte progettuali](#5-strategie-e-scelte-progettuali)
6. [Analisi di complessità temporale](#6-analisi-di-complessità-temporale)
7. [Analisi di complessità spaziale](#7-analisi-di-complessità-spaziale)
8. [Casi limite gestiti](#8-casi-limite-gestiti)
9. [Colli di bottiglia noti e possibili evoluzioni](#9-colli-di-bottiglia-noti-e-possibili-evoluzioni)
10. [Compilazione, esecuzione e test](#10-compilazione-esecuzione-e-test)
11. [Autore](#11-autore)

---

## 1. Il problema

Ogni **piatto** è una variabile booleana: servito o non servito. Ogni **dipendente** esprime una
richiesta categorica, cioè una lista di opzioni di cui almeno una deve valere (un piatto gradito
servito, oppure un piatto sgradito escluso). Questa è esattamente una **clausola CNF**: una
disgiunzione di letterali sui piatti. Tutte le richieste devono valere simultaneamente, quindi il
problema è **CNF-SAT** sull'insieme di clausole letto in ingresso.

Se l'intero insieme non è soddisfacibile, si procede a scartare le richieste a partire
dall'**ultimo** dipendente (il più in basso in gerarchia), poi il penultimo, e così via, fino a
trovare un prefisso della lista che sia soddisfacibile.

### Osservazione chiave

La soddisfacibilità è **monotona rispetto ai prefissi**: se le prime `L` clausole sono
soddisfacibili, lo sono anche le prime `L' < L` (un sottoinsieme di una formula soddisfacibile è
sempre soddisfacibile, perché ha meno vincoli). L'insieme dei prefissi soddisfacibili è quindi un
intervallo `[0, L*]`. Questa proprietà è il punto di partenza della strategia di rimozione (vedi
[sezione 5.1](#51-ricerca-binaria-sul-prefisso-invece-di-rimozione-sequenziale)) ed è la principale
differenza di questo progetto rispetto a un DPLL "da manuale".

CNF-SAT è NP-completo in generale, quindi nel caso peggiore teorico nessun algoritmo evita un costo
esponenziale. Le istanze fornite (e quelle generate dallo script incluso) hanno però un rapporto
clausole/variabili molto alto — centinaia di migliaia di richieste su poche migliaia di piatti — che
rende la propagazione unitaria estremamente efficace nel determinare rapidamente soddisfacibilità o
conflitto, senza dover esplorare un albero di ricerca ampio. Le scelte implementative descritte in
questo documento sono pensate per sfruttare proprio questa caratteristica.

---

## 2. Formato di input e output

### Struttura del file

```
<piatto_1> <piatto_2> ... <piatto_D>
<richiesta dipendente 1>
<richiesta dipendente 2>
...
<richiesta dipendente C>
```

- La prima riga elenca tutti i piatti disponibili (nomi con le convenzioni di un identificatore C).
- Ogni riga successiva è la richiesta di un dipendente: una lista di piatti, dal più alto in
  gerarchia al più basso. Un piatto preceduto da `-` significa "non gradito" (letterale negato).

### Output atteso

| Caso | Output |
|---|---|
| Tutte le richieste soddisfacibili | `OK` |
| Non soddisfacibili | `KO`, poi una riga `-k` per ogni dipendente scartato dal fondo (`k = 1, 2, ...`), fino alla riga `OK` che segna il primo prefisso soddisfacibile |

Esempio (dal testo del progetto):

```
KO
-1
-2
OK
```

`-1` indica che si è provato senza l'ultimo dipendente (fallito), `-2` che si è provato scartando
anche il penultimo (riuscito), da cui `OK`.

---

## 3. Architettura del codice

Sorgente unico, `main.c`, diviso in sezioni ordinate secondo le dipendenze (nessun prototipo
anticipato: ogni funzione è `static` e definita prima dell'uso).

```
DIZIONARIO PIATTI     hashName, dishTableInit, dishTableGrow, dishFindOrAdd
CLAUSOLE (CSR)        litPoolPush, clauseStartPush, clauseLen
PARSING               parseLineIntoClause, readLine, readInput
OCCORRENZE (CSR)      buildOccurrences
EURISTICHE            cmpByOccDesc, buildVarHeuristics
STATO SOLVER          assign, trail, unassignedCount, trueCount, propQueue
PROPAGAZIONE          queuePush, findUnitLiteral, applyAssignment, revertTrail,
                      propagateQueue, decideAndPropagate
RICERCA DPLL          pickUnassignedVar, dpll, isSatisfiable
OUTPUT                printRemoved
CLEANUP               freeAll
MAIN                  parsing, ricerca binaria sul prefisso, stampa
```

Il `main` legge l'input, costruisce le strutture ausiliarie una sola volta (occorrenze ed
euristiche), verifica l'istanza completa e, solo se necessario, avvia la ricerca binaria sul
prefisso soddisfacibile.

---

## 4. Strutture dati

### 4.1 Dizionario piatti — tabella hash a indirizzamento aperto

```c
typedef struct { const char *key; int idx; } DishSlot;
static DishSlot *dishTable;   // capacità sempre potenza di 2
```

Ogni letterale di ogni clausola richiede una risoluzione nome → indice. Una scansione lineare
dell'array dei piatti (come nella prima versione del progetto) costerebbe O(D) per letterale, cioè
O(L · D) complessivo — con D e L nell'ordine delle migliaia/milioni, un fattore quadratico
inaccettabile. La tabella hash (FNV-1a, linear probing, raddoppio quando il fattore di carico supera
0.5) riporta il lookup a **O(1) atteso**.

### 4.2 Clausole in formato CSR (Compressed Sparse Row)

```c
static int *litPool;      // tutti i letterali di tutte le clausole, concatenati
static int *clauseStart;  // clauseStart[c] .. clauseStart[c+1] = intervallo della clausola c
```

Un letterale è un intero non nullo: `idx+1` se il piatto è gradito, `-(idx+1)` se sgradito (segno e
indice si leggono senza campi separati). Rispetto a una `struct Clause` con array dinamico allocato
per ciascuna delle fino a centinaia di migliaia di clausole, il formato CSR:

- elimina altrettante `malloc`/`realloc` individuali (e il relativo overhead di allocatore);
- massimizza la località di cache, perché i letterali di clausole vicine sono fisicamente vicini in
  memoria;
- occupa esattamente `L` interi più `C+1` interi di offset, senza frammentazione.

### 4.3 Liste di occorrenze — anch'esse in formato CSR

```c
static int *occStart;  // dimensione 2*D + 1, indicizzato per "letterale codificato"
static int *occList;   // dimensione L, indici di clausola
```

Per ogni piatto `v`, il codice `2v` raccoglie le clausole in cui compare positivo, `2v+1` quelle in
cui compare negato. Costruite una sola volta con un **counting sort** in `buildOccurrences` (due
passate su `litPool`, O(L)).

Questa è la struttura che rende efficiente la propagazione unitaria (sezione 5.2): quando una
variabile viene assegnata, `applyAssignment` aggiorna **solo** le clausole che la contengono,
seguendo `occStart[2v] .. occStart[2v+2]`, invece di riscandire l'intera formula.

### 4.4 Contatori per clausola

```c
static int *unassignedCount;  // letterali non ancora assegnati, per clausola
static int *trueCount;        // letterali già veri, per clausola
```

Invece di un flag booleano "soddisfatta", si mantiene un **conteggio** di letterali veri. Una
clausola è soddisfatta se `trueCount > 0`, in conflitto se `trueCount == 0 && unassignedCount == 0`,
unitaria se `trueCount == 0 && unassignedCount == 1`. Il conteggio (anziché un flag) è ciò che rende
`revertTrail` **simmetrico e corretto** anche quando una clausola è soddisfatta da più letterali
contemporaneamente (basta decrementare, senza dover ricordare "quale" letterale l'aveva soddisfatta).

### 4.5 Coda di propagazione

```c
static int *propQueue;  // letterali impliciti da processare (BFS)
```

Buffer riutilizzato ad ogni chiamata di `propagateQueue`, con capacità a raddoppio. Evita la
ricorsione diretta nella propagazione (che accoppierebbe la profondità dello stack alla lunghezza
della catena di implicazioni) e permette di rilevare implicazioni contraddittorie sullo stesso
letterale prima di applicarle.

### 4.6 Riepilogo

| Struttura | Implementazione | Motivazione |
|---|---|---|
| Dizionario piatti | Hash table, indirizzamento aperto | Lookup O(1) atteso invece di O(D) |
| Clausole | CSR (`litPool` + `clauseStart`) | Niente allocazioni per clausola, cache-friendly |
| Occorrenze letterale → clausole | CSR (`occStart` + `occList`) | Propagazione unitaria "a variabile modificata" |
| Stato per clausola | Contatori `trueCount` / `unassignedCount` | Backtracking simmetrico, robusto a letterali duplicati |
| Ordine di decisione | Array statico `varOrder` | Euristica precalcolata, nessun ricalcolo per tentativo |
| Coda di propagazione | Array a raddoppio | Propagazione BFS senza ricorsione |

---

## 5. Strategie e scelte progettuali

### 5.1 Ricerca binaria sul prefisso, invece di rimozione sequenziale

L'approccio più diretto — rimuovere un dipendente alla volta dal fondo e riverificare da zero —
richiede fino a **C** risoluzioni SAT complete, ciascuna costosa. Sfruttando la monotonia dei
prefissi (sezione 1), il prefisso soddisfacibile più lungo `L*` si trova con una ricerca binaria:

```
lo = 0                 // SAT(0) = vero per costruzione (nessuna clausola)
hi = numClauses         // SAT(numClauses) = falso, già verificato
mentre hi - lo > 1:
    mid = (lo + hi) / 2
    se SAT(mid): lo = mid
    altrimenti:   hi = mid
```

Questo riduce il numero di risoluzioni SAT da **O(C)** a **O(log C)** — circa 19 anziché fino a
500 000 sui casi più grandi. La sequenza di righe `-1 .. -K*` richiesta in output non va quindi
ri-verificata riga per riga: la monotonia **garantisce** che tutti i valori intermedi siano ancora
insoddisfacibili, quindi si deduce e si stampa l'intera sequenza da `K* = numClauses - L*` senza
ulteriori chiamate al solver.

### 5.2 Propagazione unitaria guidata dalle occorrenze

Un DPLL "da manuale" riscandisce tutte le clausole ad ogni fixpoint di propagazione: O(C · lunghezza
media) per round, ripetuto fino a stabilizzarsi. Con le liste di occorrenze (sezione 4.3),
`applyAssignment` tocca solo le clausole realmente coinvolte da una variabile appena assegnata, in
tempo proporzionale al numero di occorrenze di quella variabile. È l'analogo semplificato di uno
schema a **letterali sorvegliati** (watched literals), scelto per la sua semplicità di
implementazione a fronte di un beneficio pressoché identico su queste istanze (dove ogni variabile
compare in relativamente poche clausole rispetto al totale).

`isSatisfiable` esegue una scansione iniziale O(limit) per individuare le clausole già unitarie in
partenza (non essendo scaturite da un'assegnazione, la propagazione "a variabile modificata" da sola
non le scoprirebbe); da lì in poi, `dpll`/`decideAndPropagate` propagano esclusivamente in risposta
a decisioni, senza mai più riscandire l'intera formula.

### 5.3 Euristiche statiche di decisione

`buildVarHeuristics` calcola, una sola volta dopo il parsing:

- **`varOrder`**: le variabili ordinate per numero di occorrenze decrescente. Si decide prima sui
  piatti più "vincolati", con l'obiettivo di innescare prima conflitti (nei casi insoddisfacibili) o
  propagazioni a cascata (nei casi soddisfacibili).
- **`firstTryValue`**: per ogni piatto, la polarità più frequente tra le sue occorrenze. È
  un'euristica gloutona standard per SAT casuale: assegnare la polarità dominante soddisfa
  immediatamente il maggior numero di clausole possibile.

Essendo calcolate una volta sola sull'intera formula e riusate per tutti i tentativi della ricerca
binaria, il loro costo (O(D log D) per l'ordinamento) è trascurabile rispetto al totale.

### 5.4 Verifica prima della ricerca, in due fasi separate

`isSatisfiable(limit)` reinizializza lo stato, propaga le clausole unitarie di partenza e solo dopo
richiama `dpll`. Questo separa nettamente "cosa deriva staticamente dall'istanza" da "cosa deriva
dalle decisioni", evitando di dover distinguere i due casi dentro lo stesso ciclo.

### 5.5 Letterale codificato come intero con segno

Un letterale è rappresentato come un singolo `int` (`idx+1` o `-(idx+1)`) invece di una `struct {int
var; int negated;}`. Stesso ingombro in memoria, ma dimezza le operazioni di confronto/decodifica
nei cicli più caldi (`applyAssignment`, `revertTrail`, `buildOccurrences`) a un semplice test di
segno.

### 5.6 I/O bufferizzato e portabile

`getline` (POSIX) non è disponibile su tutte le toolchain (in particolare MinGW su Windows). Il
progetto usa una propria `readLine`, basata su `fgets` a blocchi con buffer a raddoppio: stesso
comportamento ammortizzato O(1) per carattere, ma senza dipendenze da estensioni non standard.
`stdin`/`stdout` sono bufferizzati esplicitamente a 1 MiB via `setvbuf`, e la stampa delle righe
`-k` (potenzialmente centinaia di migliaia) usa una conversione intero→stringa manuale con un unico
`fwrite`, evitando l'overhead di `printf` ripetuto per ogni riga.

### 5.7 Cleanup esplicito

`freeAll` libera ogni struttura allocata (nomi dei piatti, tabella hash, array CSR, occorrenze,
stato del solver) prima del `return` di `main`. Il programma è verificato **pulito con valgrind**
(`--leak-check=full`): 0 blocchi persi su tutti i casi di test disponibili, incluso il percorso che
attraversa la ricerca binaria (vedi [sezione 10](#10-compilazione-esecuzione-e-test)).

---

## 6. Analisi di complessità temporale

### Notazione

| Simbolo | Significato |
|---|---|
| `N` | dimensione totale dell'input (caratteri) |
| `D` | numero di piatti (variabili booleane) |
| `C` | numero di clausole (richieste dei dipendenti) |
| `L` | numero totale di letterali (somma delle lunghezze di tutte le clausole) |
| `K*` | numero minimo di dipendenti da scartare dal fondo |

Le complessità della tabella hash sono **attese**, sotto l'ipotesi di hash uniforme.

### Fasi principali

| Fase | Complessità | Note |
|---|---|---|
| Parsing (`readInput`) | O(N) atteso | lookup piatto O(1) atteso per letterale |
| `buildOccurrences` | O(L) | counting sort, due passate |
| `buildVarHeuristics` | O(D log D) | `qsort` su `varOrder` |
| Una chiamata `isSatisfiable(limit)` | O(occorrenze toccate + ramificazioni) | dominato dalla propagazione sulle istanze fornite |
| Numero di chiamate `isSatisfiable` | O(log C) | ricerca binaria, invece di O(C) per rimozione sequenziale |
| Stampa dell'esito | O(K*) | conversione intero→stringa manuale, un solo `fwrite` per riga |

### Il costo della ricerca DPLL

Nel caso peggiore teorico, CNF-SAT è NP-completo: un'istanza avversaria può forzare un numero di
ramificazioni esponenziale in `D`, indipendentemente dalle euristiche. Le istanze generate dallo
script fornito (e quelle del testo del progetto) hanno però un rapporto `C/D` molto alto — nel test
più grande generato per la verifica, 500 000 clausole su 3 000 piatti — che rende la propagazione
unitaria da sola sufficiente a determinare la maggior parte delle assegnazioni, lasciando poche
ramificazioni effettive a `dpll`. Su questo test la risoluzione completa (istanza intera + ricerca
binaria su ~19 tentativi) impiega **meno di un secondo** (misurato: ~0,5 s).

### Complessità complessiva

```
O(N + L + D log D + (log C) · costo_isSatisfiable)
```

dove `costo_isSatisfiable` è, in pratica su queste istanze, prossimo a lineare nel numero di
occorrenze coinvolte grazie alla propagazione guidata dalle liste di occorrenza (sezione 5.2), pur
restando esponenziale nel caso peggiore teorico.

---

## 7. Analisi di complessità spaziale

| Componente | Occupazione |
|---|---|
| Nomi dei piatti + tabella hash | O(D) |
| Clausole (`litPool` + `clauseStart`) | O(L + C) |
| Liste di occorrenze (`occStart` + `occList`) | O(D + L) |
| Contatori per clausola (`unassignedCount`, `trueCount`) | O(C) |
| Stato del solver (`assign`, `trail`, `varOrder`, `firstTryValue`) | O(D) |
| Coda di propagazione | O(D) nel caso peggiore (raddoppio) |
| Buffer di I/O | 2 MiB fissi (1 MiB input + 1 MiB output) |

**Totale: O(N + L + D + C)**, lineare nella dimensione dell'istanza. Nessuna struttura ha
occupazione superlineare: in particolare non si duplicano le clausole per ogni tentativo della
ricerca binaria, che riusa sempre gli stessi array CSR filtrando dinamicamente per `currentLimit`.

Misurato (Process Working Set) sul test da 500 000 clausole / 3 000 piatti / ~79 MB di input:
**picco di circa 62 MB**, per un rapporto memoria/input inferiore a 1×.

---

## 8. Casi limite gestiti

| Caso | Comportamento |
|---|---|
| Nessuna richiesta (`C = 0`) | Vacuamente soddisfacibile, stampa `OK` |
| Piatto non elencato nella prima riga ma referenziato in una richiesta | Errore su `stderr`, uscita controllata |
| Clausola vuota (difensivo; non producibile dal formato di input, le righe vuote sono già scartate a monte) | Trattata come sempre falsa: rende insoddisfacibile qualunque prefisso che la includa |
| Letterali duplicati o tautologici nella stessa richiesta (es. `pizza pizza`, `pizza -pizza`) | Gestiti correttamente dal conteggio `trueCount`/`unassignedCount`, senza logica dedicata |
| Righe molto lunghe (prima riga con migliaia di piatti) | Buffer di `readLine` a raddoppio, nessun limite fisso |
| Terminatori di riga CRLF | `\r` incluso nei delimitatori di `strtok_r`, righe vuote riconosciute correttamente |
| Istanza già soddisfacibile nella sua interezza | Nessuna ricerca binaria: si stampa `OK` dopo la sola verifica iniziale |
| Fallimento di `malloc`/`realloc` | Non gestito esplicitamente (comportamento standard C: terminazione); dato l'ordine di grandezza dei test non è un caso atteso |

---

## 9. Colli di bottiglia noti e possibili evoluzioni

### 9.1 Scelta della variabile — scansione lineare di `varOrder`

`pickUnassignedVar` scandisce linearmente `varOrder` ad ogni decisione, in cerca della prima
variabile non assegnata: O(D) per decisione. Con `D` nell'ordine delle migliaia (come nei test
forniti) questo è trascurabile rispetto al costo della propagazione, ma diventerebbe il termine
dominante — O(decisioni · D) — su istanze con decine o centinaia di migliaia di piatti. Una coda a
priorità aggiornata dinamicamente, o una struttura "union-find" per saltare le variabili già
assegnate, eliminerebbe questo costo residuo.

### 9.2 Nessun apprendimento di clausole (no CDCL)

Il solver usa backtracking cronologico puro: quando un ramo fallisce si torna esattamente al punto
di decisione precedente, senza dedurre e memorizzare il motivo del conflitto (clause learning) né
saltare a ritroso più punti di decisione contemporaneamente (backjumping non cronologico). Su
istanze con conflitti "strutturati" e ripetuti, un solver CDCL (come nei moderni risolutori SAT)
potrebbe evitare di riscoprire lo stesso conflitto più volte. Non è stato necessario per le istanze
fornite, dove l'alta densità di propagazione unitaria limita già fortemente le ramificazioni
effettive, ma resta il principale margine di miglioramento in caso di istanze meno "propagazione-
friendly".

### 9.3 Filtro `currentLimit` sulle occorrenze

Durante la ricerca binaria, `applyAssignment`/`revertTrail` scandiscono le liste di occorrenze per
intero e scartano con un confronto (`if (c >= currentLimit) continue;`) le clausole non incluse nel
tentativo corrente. Poiché `occList` è costruita per clausole crescenti, un taglio con ricerca
binaria sull'intervallo (invece dello scarto ad uno ad uno) ridurrebbe il lavoro nei tentativi con
`limit` piccolo — miglioramento minore, non necessario alle dimensioni testate.

### 9.4 Pure-literal elimination

Non implementata: un piatto che compare in una sola polarità in tutte le clausole attive potrebbe
essere assegnato direttamente senza ramificare. Sulle istanze generate (letterali con segno
scelto casualmente riga per riga) questo caso è raro, quindi il beneficio atteso è basso rispetto al
costo di mantenerne aggiornato il conteggio ad ogni assegnazione/backtrack.

---

## 10. Compilazione, esecuzione e test

### Compilazione

```bash
# build di sviluppo, con tutti i warning
gcc -std=gnu11 -Wall -Wextra -O2 -o main main.c

# build di consegna (massima ottimizzazione)
gcc -std=gnu11 -O2 -march=native -o main main.c
```

Il codice compila senza alcun warning con `-Wall -Wextra`, sia con GCC su Linux sia con MinGW-w64 su
Windows (non usa `getline` né altre estensioni POSIX non portabili).

### Esecuzione

```bash
./main < input.txt > output.txt
```

### Verifica di correttezza

```bash
diff <(./main < "Test cases/open3.txt") atteso.txt && echo OK
```

Gli esempi del testo del progetto (soddisfacibile e insoddisfacibile) e tutti i file in
`Test cases/` sono stati verificati sia per corrispondenza diretta con l'output atteso, sia per
cross-check con un'implementazione DPLL di riferimento scritta indipendentemente in Python.

### Verifica della memoria

```bash
valgrind --leak-check=full --show-leak-kinds=all ./main < "Test cases/open7.txt" > /dev/null
```

Eseguito su tutti i casi disponibili, incluso il percorso che attraversa la ricerca binaria
(istanza insoddisfacibile): **0 errori, 0 blocchi persi** in ogni caso.

### Misurazione dei tempi e della memoria di picco

```bash
/usr/bin/time -v ./main < "Test cases/open7.txt" > /dev/null
```

Dati misurati in fase di sviluppo (si veda anche [sezione 6](#6-analisi-di-complessità-temporale) e
[sezione 7](#7-analisi-di-complessità-spaziale)):

| Test | Dimensione input | Clausole | Piatti | Tempo | Memoria di picco |
|---|---|---|---|---|---|
| `open7.txt` | 5,5 MB | ~50 000 | 700 | ~66 ms | pochi MB |
| test sintetico generato ad-hoc | ~79 MB | 500 000 | 3 000 | ~0,5 s | ~62 MB |

### Generatore di test

La cartella `Generatore di test cases/` contiene lo script Python usato per produrre i file in
`Test cases/`, con configurazioni crescenti di numero di piatti, dipendenti e opzioni massime per
richiesta.

---

## 11. Autore

**Elia Dallanoce**
Prova finale di Algoritmi e Principi dell'Informatica
Anno accademico 2025/2026
