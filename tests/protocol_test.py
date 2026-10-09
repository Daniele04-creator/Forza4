"""TCP regression tests. Run against an isolated server, never a shared game server."""

import argparse
import concurrent.futures
import os
import re
import socket
import struct
import threading
import time

EMPTY = "/".join(["......."] * 6)
MARKER = "__TEST_SYNC__"
MARKER_REPLY = "ERR comando sconosciuto"
DRAW_MOVES = [1, 3, 4, 0, 3, 5, 3, 4, 2, 1, 1, 0, 1, 1, 1, 6, 3, 0, 2, 3, 6,
              5, 3, 6, 5, 2, 0, 0, 0, 4, 5, 2, 6, 5, 5, 4, 2, 6, 4, 6, 4, 2]
RESET_LINGER = struct.pack("hh" if os.name == "nt" else "ii", 1, 0)


class Peer:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port), timeout=10)
        self.buffer = b""
        assert self.line().startswith("OK connesso"), "Missing greeting"

    def send(self, command):
        self.socket.sendall((command + "\n").encode())

    def line(self):
        while b"\n" not in self.buffer:
            block = self.socket.recv(65536)
            if not block:
                raise EOFError("Server closed the connection")
            self.buffer += block
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode().rstrip("\r")

    def sync(self, command=None):
        self.send((command + "\n" if command else "") + MARKER)
        lines = []
        while True:
            line = self.line()
            if line == MARKER_REPLY:
                return lines
            lines.append(line)

    def close(self):
        self.socket.close()


class Session:
    def __init__(self, host, port, names):
        self.host, self.port = host, port
        self.peers = {}
        self.commands = 0
        for name in names:
            peer = Peer(host, port)
            self.peers[name] = peer
            assert peer.sync("LOGIN " + name) == ["OK autenticato come " + name]

    def command(self, name, command, expected=None):
        # The sender's marker follows the command in its own TCP stream. Once
        # received, its synchronous broadcasts have already been sent. Markers
        # on the other streams delimit those broadcasts without timing guesses.
        result = {name: self.peers[name].sync(command)}
        for other, peer in self.peers.items():
            if other != name:
                result[other] = peer.sync()
        self.commands += 1
        if expected:
            assert any(expected in line for line in result[name]), (name, command, result)
        return result

    def create(self, owner):
        result = self.command(owner, "CREATE")
        line = next(line for line in result[owner] if re.fullmatch(r"OK partita \d+ creata", line))
        return int(line.split()[2])

    def start(self, owner, guest, game=None):
        game = self.create(owner) if game is None else game
        self.command(guest, f"JOIN {game}", "OK richiesta di join inviata")
        result = self.command(owner, f"ACCEPT {game} {guest}", "OK accettato")
        for player in (owner, guest):
            assert any(EMPTY in line for line in result[player]), result
            assert any(f"Partita iniziata {game}" in line for line in result[player]), result
        for observer in self.peers.keys() - {owner, guest}:
            assert result[observer] == [f"Partita in corso {game}"], result
        return game

    def disconnect(self, name):
        peer = self.peers.pop(name)
        peer.send("QUIT")
        assert peer.line() == "OK arrivederci"
        # EOF occurs after cleanup has released all game/client references.
        try:
            peer.line()
        except EOFError:
            pass
        else:
            raise AssertionError("QUIT did not close the socket")
        peer.close()

    def close(self):
        for peer in self.peers.values():
            peer.close()


def assert_no_boards(result, observers):
    for name in observers:
        assert not any("Tabellone " in line or "tabellone=" in line for line in result[name]), result


def assert_finished(result, game, player1, player2, board, winner=None):
    for name in (player1, player2):
        expected_board = f"Tabellone {game} {board} turno di -"
        outcome = "finita in pareggio" if winner is None else ("Hai vinto" if name == winner else "Hai perso")
        assert result[name].count(expected_board) == 1, result
        end_index = next(i for i, line in enumerate(result[name]) if outcome in line)
        assert result[name].index(expected_board) < end_index, result
        if winner == name:
            assert not any("Hai perso" in line for line in result[name]), result
    observers = result.keys() - {player1, player2}
    assert_no_boards(result, observers)
    for name in observers:
        assert result[name] == [f"Partita finita {game}"], result


def play_draw(session, owner, guest):
    game = session.start(owner, guest)
    board = [["."] * 7 for _ in range(6)]
    for turn, column in enumerate(DRAW_MOVES):
        name, symbol = (owner, "X") if turn % 2 == 0 else (guest, "O")
        row = max(row for row in range(6) if board[row][column] == ".")
        board[row][column] = symbol
        compact = "/".join("".join(row) for row in board)
        result = session.command(name, f"MOVE {column}")
        for player in (owner, guest):
            assert any(f"Tabellone {game} {compact} turno di " in line for line in result[player]), result
        assert_no_boards(result, session.peers.keys() - {owner, guest})
        if turn < 41:
            assert "OK mossa accettata" in result[name], result
        else:
            assert compact.count("X") == 21 and compact.count("O") == 21
            assert "." not in compact
            assert_finished(result, game, owner, guest, compact)
    return game


def assert_waiting(session, observer, game, owner):
    result = session.command(observer, "LIST")
    assert f"Partita {game} In Attesa creata da={owner} P1=- P2=-" in result[observer], result


def functional_tests(host, port):
    session = Session(host, port, ("alice", "bob", "carol", "dave", "eve"))
    try:
        first = session.create("alice")
        second = session.create("alice")
        assert first != second
        listing = session.command("eve", "LIST")["eve"]
        assert sum("In Attesa creata da=alice" in line for line in listing) == 2
        for number in ("4294967297", "-4294967295", "9" * 100, "1x"):
            session.command("bob", f"JOIN {number}", "ERR id partita non valido")
            session.command("alice", f"ACCEPT {number} bob", "ERR id partita non valido")
            session.command("alice", f"REJECT {number} bob", "ERR id partita non valido")
            session.command("alice", f"REMATCH {number}", "ERR id partita non valido")
        session.command("bob", f"JOIN {first}", "OK richiesta di join inviata")
        session.command("carol", f"JOIN {first}", "OK richiesta di join inviata")
        session.command("eve", f"JOIN {second}", "OK richiesta di join inviata")
        session.command("eve", f"ACCEPT {first} bob", "ERR solo il proprietario")
        result = session.command("alice", f"REJECT {first} carol", "OK rifiutato")
        assert any("rifiutata" in line for line in result["carol"])
        result = session.command("alice", f"ACCEPT {first} bob", "OK accettato")
        assert any(EMPTY in line for line in result["alice"])
        session.command("eve", f"JOIN {first}", "non e accessibile")
        session.command("bob", f"JOIN {second}", "ERR sei gia in una partita attiva")
        session.command("alice", "CREATE", "ERR sei gia in una partita attiva")
        session.command("alice", f"ACCEPT {second} eve", "ERR impossibile avviare")
        other = session.start("dave", "carol")
        assert len({first, second, other}) == 3
        session.command("bob", "MOVE 1", "ERR non e il tuo turno")
        invalid_columns = ("-1", "7", "bad", "4294967296", "4294967297",
                           "-4294967296", "9" * 100, "1x")
        for column in invalid_columns:
            result = session.command("alice", f"MOVE {column}", "ERR colonna non valida")
            assert all(not lines for name, lines in result.items() if name != "alice"), result
        state = session.command("alice", "STATE")["alice"]
        assert state == [f"stato partita {first} In Corso tabellone={EMPTY}"], state
        for turn in range(6):
            result = session.command("alice" if turn % 2 == 0 else "bob", "MOVE 0", "OK mossa accettata")
            assert_no_boards(result, ("carol", "dave", "eve"))
            assert all(not result[name] for name in ("carol", "dave", "eve")), result
        session.command("alice", "MOVE 0", "ERR mossa non valida")
        assert any(EMPTY in line for line in session.command("carol", "STATE")["carol"])
        result = session.command("alice", "LEAVE", "OK partita abbandonata")
        assert any("Hai perso" in line for line in result["alice"])
        assert any("Hai vinto" in line for line in result["bob"])
        result = session.command("bob", "REMATCH", "OK rematch aperta")
        assert any("rimosso" in line for line in result["alice"])
        assert_waiting(session, "eve", first, "bob")
        session.start("bob", "alice", first)
        for _ in range(3):
            session.command("bob", "MOVE 0", "OK mossa accettata")
            session.command("alice", "MOVE 1", "OK mossa accettata")
        result = session.command("bob", "MOVE 0", "Hai vinto")
        board = "/".join([".......", ".......", "X......", "XO.....", "XO.....", "XO....."])
        assert_finished(result, first, "bob", "alice", board, "bob")
        session.command("alice", f"REMATCH {first}", "ERR")
        session.command("bob", "STATE", "ERR nessuna partita attiva")
        session.command("carol", "LEAVE", "OK partita abbandonata")

        # A winner can reopen the old match even when its former loser has
        # already entered another match. That current match must remain intact.
        active_elsewhere = session.start("alice", "eve")
        session.command("alice", "MOVE 3", "OK mossa accettata")
        session.command("alice", "REMATCH", "ERR")
        session.command("bob", f"REMATCH {first}", "OK rematch aperta")
        assert_waiting(session, "carol", first, "bob")
        state = session.command("alice", "STATE")["alice"]
        active_board = "/".join(["......."] * 5 + ["...X..."])
        assert state == [f"stato partita {active_elsewhere} In Corso tabellone={active_board}"], state
        session.command("eve", "MOVE 2", "OK mossa accettata")
        session.command("eve", "LEAVE", "OK partita abbandonata")

        drawn = play_draw(session, "carol", "dave")
        result = session.command("carol", f"REMATCH {drawn}", "OK consenso rematch registrato")
        assert not any("Partita in attesa" in line for lines in result.values() for line in lines), result
        assert any("Richiesta di rematch dopo pareggio" in line for line in result["dave"]), result
        duplicate = session.command("carol", f"REMATCH {drawn}", "OK consenso rematch gia registrato")
        assert all(not lines for name, lines in duplicate.items() if name != "carol"), duplicate
        assert any(f"Partita {drawn} Finita" in line for line in session.command("eve", "LIST")["eve"])
        result = session.command("dave", "REMATCH", "OK rematch aperta")
        assert any("OK rematch aperta" in line for line in result["carol"]), result
        assert all(not any("Partita iniziata" in line for line in lines) for lines in result.values()), result
        for observer in ("alice", "bob", "eve"):
            assert result[observer] == [f"Partita in attesa {drawn} di carol"], result
        assert_waiting(session, "eve", drawn, "carol")
        session.command("dave", "MOVE 0", "ERR nessuna partita attiva")
        session.start("carol", "dave", drawn)
        session.command("carol", "LEAVE", "OK partita abbandonata")

        old_draw = play_draw(session, "alice", "bob")
        session.command("alice", f"REMATCH {old_draw}", "OK consenso rematch registrato")
        alternate = session.start("alice", "eve")
        session.command("alice", f"REMATCH {old_draw}", "ERR")
        session.command("bob", f"REMATCH {old_draw}", "ERR")
        session.command("alice", "LEAVE", "OK partita abbandonata")
        # Default REMATCH targets Alice's most recently created finished match
        # (a loss), rather than the older draw, so it cannot cast that vote.
        session.command("alice", "REMATCH", "ERR solo il vincitore")
        result = session.command("bob", f"REMATCH {old_draw}", "OK consenso rematch registrato")
        assert not any("rematch aperta" in line for lines in result.values() for line in lines), result
        session.command("alice", f"REMATCH {old_draw}", "OK rematch aperta")
        assert_waiting(session, "eve", old_draw, "alice")
        assert alternate != old_draw

        disconnected_draw = play_draw(session, "carol", "dave")
        session.command("carol", f"REMATCH {disconnected_draw}", "OK consenso rematch registrato")
        session.disconnect("dave")
        session.command("carol", f"REMATCH {disconnected_draw}", "ERR")
        print(f"PASS protocol: {session.commands} commands, 5 clients, 2 simultaneous games, final boards, rematch consent and stale/disconnected votes")
    finally:
        session.close()


def parallel_login_tests(host, port):
    for round_number in range(10):
        peers = [Peer(host, port) for _ in range(64)]
        barrier = threading.Barrier(64)
        try:
            def login(index):
                nickname = f"round{round_number}player{index}"
                barrier.wait(timeout=20)
                response = peers[index].sync("LOGIN " + nickname)
                assert response == ["OK autenticato come " + nickname], (nickname, response)
            with concurrent.futures.ThreadPoolExecutor(max_workers=64) as executor:
                list(executor.map(login, range(64)))
        finally:
            for peer in peers:
                peer.close()
    print("PASS parallel parser: 640 LOGIN commands, 64 simultaneous clients in each of 10 rounds")


def disconnect_and_concurrency_tests(host, port):
    session = Session(host, port, ("stress_owner", "stress_guest", "stress_observer"))
    try:
        game = session.start("stress_owner", "stress_guest")
        guest = session.peers.pop("stress_guest")
        guest.socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, RESET_LINGER)
        guest.close()
        owner = session.peers["stress_owner"]
        lines = []
        while not any("Hai vinto" in line for line in lines):
            lines.append(owner.line())
        assert any("disconnessione" in line for line in lines), lines
        session.command("stress_owner", "REMATCH", "OK rematch aperta")
        # A closed observer can receive a broadcast while its thread is cleaning
        # up: the process must survive the resulting socket write failure.
        reset_peer = Peer(host, port)
        assert reset_peer.sync("LOGIN reset_observer") == ["OK autenticato come reset_observer"]
        reset_peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, RESET_LINGER)
        reset_peer.close()
        session.create("stress_owner")
        new_guest = Peer(host, port)
        session.peers["stress_newguest"] = new_guest
        assert new_guest.sync("LOGIN stress_newguest") == ["OK autenticato come stress_newguest"]
        session.start("stress_owner", "stress_newguest", game)
        barrier = threading.Barrier(2)
        def exercise(name):
            peer = session.peers[name]
            barrier.wait(timeout=10)
            for _ in range(30):
                for command in ("MOVE 0", "STATE", "CREATE", "JOIN 2147483647", "LEAVE", f"REMATCH {game}"):
                    response = peer.sync(command)
                    assert response, (name, command)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            list(executor.map(exercise, ("stress_owner", "stress_newguest")))
        observer_lines = session.peers["stress_observer"].sync()
        assert_no_boards({"stress_observer": observer_lines}, ("stress_observer",))
        session.command("stress_observer", "STATE", "ERR nessuna partita attiva")
        session.command("stress_owner", "LIST")
        print("PASS resilience: abrupt reset, disconnect victory, broadcast to closed socket, 360 concurrent commands and live server")
    finally:
        session.close()


def wait_for_server(host, port):
    deadline = time.monotonic() + 20
    while True:
        try:
            peer = Peer(host, port)
            peer.close()
            return
        except OSError:
            if time.monotonic() >= deadline:
                raise
            # Startup retry only; protocol assertions use stream markers.
            time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    wait_for_server(args.host, args.port)
    functional_tests(args.host, args.port)
    parallel_login_tests(args.host, args.port)
    disconnect_and_concurrency_tests(args.host, args.port)


if __name__ == "__main__":
    main()
