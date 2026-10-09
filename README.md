# Forza 4 Client-Server

**Esame 30/30 in Laboratorio di Sistemi Operativi.**

## Cosa richiedeva la traccia

La traccia richiedeva un gioco Forza 4 con un server multi-client in C,
un client in un linguaggio a scelta, comunicazione tramite socket
(senza WebSocket) e gestione parallela dei client con thread o processi figli.
Era richiesto l'uso di Docker Compose.

Ogni utente doveva poter creare piu' partite con identificativi univoci,
ma giocare attivamente in una sola alla volta. Il proprietario doveva poter
accettare o rifiutare le richieste di ingresso. I due giocatori ricevevano
griglia, turno ed esito personale; gli altri client ricevevano solo notifiche
sullo stato della partita. Era inoltre previsto il ripristino della griglia
per giocare nuovamente.

L'applicazione include anche la rivincita opzionale: dopo una vittoria
il vincitore diventa proprietario e il perdente viene rimosso; dopo un
pareggio serve il consenso di entrambi. La partita torna in attesa e
richiede una nuova richiesta di ingresso e la sua accettazione.

Erano richiesti un README con le istruzioni di avvio e una documentazione
di massimo 10 pagine sulle scelte implementative. Queste scelte sono
spiegate nel [documento PDF di sette pagine](output/pdf/implementation.pdf),
che include gli schemi dell'architettura e degli stati della partita.

## Struttura dell'applicazione

L'applicazione e' composta da:

- un server in C che gestisce le connessioni TCP e la logica della partita;
- un client testuale in Python che permette ai giocatori di inviare i comandi al server;
- un file `docker-compose.yml` per avviare l'intero progetto in modo semplice.

## Requisiti

Per eseguire il progetto servono:
- Docker
- Docker Compose

## Avvio del server

Dalla cartella del progetto eseguire:

```bash
docker compose build
docker compose up -d server
```

Per verificare che il server sia in esecuzione:

```bash
docker compose ps
```

## Avvio dei client

Per giocare servono almeno due terminali distinti. In ogni terminale avviare un client con:

```bash
docker compose run --rm client
```

Se serve, e' possibile aprire altri client ripetendo lo stesso comando.

## Comandi disponibili

Una volta aperto il client, i comandi principali sono:

- `LOGIN <nickname>`: identifica il giocatore con un nickname univoco, senza password.
- `LIST`: mostra le partite disponibili.
- `CREATE`: crea una nuova partita in attesa.
- `JOIN <id>`: invia una richiesta di partecipazione a una partita.
- `ACCEPT <id> <nickname>`: accetta una richiesta di join.
- `REJECT <id> <nickname>`: rifiuta una richiesta di join.
- `MOVE <colonna>`: inserisce una pedina nella colonna indicata.
- `STATE`: mostra lo stato della partita attiva. La griglia finale viene inviata automaticamente prima dell'esito.
- `LEAVE`: abbandona la partita in corso.
- `REMATCH [id]`: richiede la riapertura di una partita terminata. L'identificativo e' facoltativo.
- `QUIT`: chiude il client.

La colonna usata in `MOVE` deve essere compresa tra `0` e `6`.

## Esempio di partita

Client A:

```
LOGIN alice
CREATE
```

Supponiamo che la partita creata abbia id `1`.

Client B:

```
LOGIN bob
JOIN 1
```

Client A:

```
ACCEPT 1 bob
```

Dopo l'accettazione la partita inizia. I due giocatori possono alternare le mosse, ad esempio:

```
MOVE 3
MOVE 2
STATE
```

Il client renderizza il tabellone ricevuto dal server e mostra lo stato corrente della partita.

## Rivincita

Dopo una vittoria, solo il vincitore puo' usare `REMATCH`: diventa proprietario,
il perdente viene rimosso e la partita torna in attesa con la griglia vuota.
Dopo un pareggio, entrambi i giocatori devono confermare `REMATCH <id>`.
La prima conferma lascia la partita terminata e avvisa l'altro giocatore;
una conferma duplicata non conta come secondo consenso.
La seconda conferma riporta la partita in attesa, conservando il proprietario.

Per la rivincita dopo pareggio, entrambi devono essere connessi e liberi da altre partite attive.
Una disconnessione o l'avvio di un'altra partita cancella i consensi precedenti.
Il vincitore deve essere libero per riaprire una partita vinta; il perdente puo'
gia' giocare altrove, e la sua nuova partita resta invariata.
La rivincita richiede nuovamente `JOIN <id>` e `ACCEPT <id> <nickname>`:
non inizia automaticamente alla conferma.
Senza identificativo, `REMATCH` seleziona la partita creata piu' recentemente
tra quelle terminate a cui il mittente ha partecipato.

Per esempio, dopo un pareggio sulla partita 1:

```
Client A: REMATCH 1
Client B: REMATCH 1
Client B: JOIN 1
Client A: ACCEPT 1 bob
```

L'esempio suppone che A sia il proprietario e B abbia nickname `bob`.

## Arresto del progetto

Per fermare il server e rimuovere i container:

```bash
docker compose down
```

## Note

- Se un giocatore si disconnette durante la partita, il server gestisce correttamente la chiusura della sessione di gioco.

## Codice e verifiche

Gli identificatori del server C e del client Python sono in inglese, inclusi tipi,
costanti, campi, variabili e funzioni. I file sorgenti avevano gia' nomi inglesi.
L'eseguibile del server si chiama `connect_four_server`; Makefile e Dockerfile
usano questo nome. Il nome del container resta `forza4_server`.
Il parser mantiene uno stato distinto per ogni connessione. Il server protegge
lo stato delle partite con un mutex e serializza i messaggi per destinatario.
Gli errori di invio su socket chiusi non arrestano l'intero server.
La ricezione usa un buffer di 4096 byte per connessione, evitando una chiamata
di rete per ogni carattere. I numeri vengono convertiti con `strtol`,
controllando overflow, caratteri residui e intervallo prima di usarli.
L'accettazione usa la richiesta gia' trovata e l'avvio elimina tutte le
richieste pendenti della partita, senza ripetere la ricerca del nickname.

I moduli del server sono `game` (regole), `net` (comunicazione), `state`
(client e partite), `commands` (comandi) e `server` (avvio).
Il client usa `render_board`, `render_server_line`, `send_loop` e `receive_loop`.

Per ripetere i controlli di compilazione e configurazione:

```bash
docker compose config --quiet
docker compose build
docker compose run --rm --no-deps --entrypoint python3 client -c "import ast; from pathlib import Path; ast.parse(Path('client.py').read_text())"
```

La compilazione del server usa `-Wall -Wextra -Wpedantic -Werror`.
Per eseguire i test automatici da PowerShell, con Docker attivo e Python 3.9
o successivo disponibile sul computer:

```powershell
.\tests\verify.ps1
```

La verifica compila le immagini, controlla il motore C e l'invio su socket chiusi
con AddressSanitizer e UndefinedBehaviorSanitizer, verifica la sintassi del client e prova il protocollo
TCP con un server isolato su una porta locale assegnata da Docker.
Include login simultanei, piu' partite, griglia finale, rivincite, disconnessioni
e richieste concorrenti. Il container di test viene rimosso anche in caso di errore.
I test di rete controllano anche righe spezzate o ricevute insieme, terminatori
CRLF, righe vuote, righe troppo lunghe e chiusure a meta' riga. Il numero di
chiamate a `recv` viene contato per verificare il riuso dei dati gia' ricevuti.
I test del protocollo verificano che numeri troppo grandi siano rifiutati
senza cambiare griglia o turno.
I controlli di memoria riguardano i percorsi eseguiti; non provano l'assenza di
ogni possibile errore concorrente.

## Documentazione

La descrizione delle scelte implementative e' disponibile nel
[documento PDF di sette pagine](output/pdf/implementation.pdf).
La documentazione e' distribuita soltanto in PDF, senza sorgente LaTeX
o strumenti di compilazione. Il documento non contiene bibliografia
o richiami ad altre tracce.
I test permettono di ripetere le verifiche dell'applicazione.
Partite e nickname sono conservati solo in memoria e si perdono al riavvio.
Il sistema e' pensato per l'uso locale: non include password, cifratura,
persistenza o protezione contro client che smettono di leggere i messaggi.

## File nel repository

Il repository contiene sorgenti, configurazione Docker, test, README e PDF.
Il `.gitignore` esclude eseguibili compilati, cache Python, ambienti virtuali,
file di configurazione locali e temporanei. Il PDF finale viene versionato.
