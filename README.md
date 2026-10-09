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
spiegate nel [documento PDF di otto pagine](implementation.pdf),
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

## Arresto del progetto

Per fermare il server e rimuovere i container:

```bash
docker compose down
```
