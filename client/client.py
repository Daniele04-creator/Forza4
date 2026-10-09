#!/usr/bin/env python3
import os
import socket
import sys
import threading

SERVER_ADDRESS = os.getenv("SERVER_HOST", "server")
SERVER_PORT = int(os.getenv("SERVER_PORT", "5555"))
ROWS = 6
COLUMNS = 7


def render_board(compact_board):
    rows = compact_board.split("/")
    rendered_rows = []
    separator = "+" + "---+" * COLUMNS

    if len(rows) != ROWS:
        return None

    for row in rows:
        if len(row) != COLUMNS:
            return None

        cells = []
        for cell in row:
            if cell == ".":
                cells.append(" ")
            elif cell == "X":
                cells.append("X")
            elif cell == "O":
                cells.append("O")
            else:
                return None
        rendered_rows.append("| " + " | ".join(cells) + " |")

    output = ["  0   1   2   3   4   5   6", separator]
    for row in rendered_rows:
        output.append(row)
        output.append(separator)

    return "\n".join(output)


def render_server_line(line):
    if line.startswith("Tabellone "):
        prefix, separator, nickname = line.partition(" turno di ")
        if separator:
            parts = prefix.split(" ", 2)
            if len(parts) == 3:
                board = render_board(parts[2])
                if board is not None:
                    return "Tabellone %s turno di %s\n%s" % (parts[1], nickname, board)

    if line.startswith("stato partita "):
        prefix, separator, compact_board = line.partition(" tabellone=")
        if separator:
            board = render_board(compact_board)
            if board is not None:
                return "%s\n%s" % (prefix, board)

    return line


def send_loop(client_connection):
    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            line = "QUIT"

        if not line:
            continue

        try:
            client_connection.sendall((line + "\n").encode("utf-8"))
        except OSError:
            return

        if line.upper() == "QUIT":
            try:
                client_connection.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            return


def receive_loop(client_connection):
    accumulator = b""

    while True:
        try:
            data = client_connection.recv(4096)
        except OSError:
            print("Connessione interrotta.")
            return 1

        if not data:
            print("Connessione chiusa dal server.")
            return 0

        accumulator += data

        while b"\n" in accumulator:
            raw_line, accumulator = accumulator.split(b"\n", 1)
            line = raw_line.rstrip(b"\r").decode("utf-8", errors="replace")
            rendered_line = render_server_line(line)
            print("\n%s\n> " % rendered_line, end="", flush=True)


def main():
    client_connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        client_connection.connect((SERVER_ADDRESS, SERVER_PORT))
    except OSError as error:
        print("Errore di connessione a %s:%s: %s" % (SERVER_ADDRESS, SERVER_PORT, error))
        return 1

    print("Usa i comandi del protocollo, per esempio: LOGIN mario")

    send_thread = threading.Thread(target=send_loop, args=(client_connection,), daemon=True)
    send_thread.start()

    try:
        return receive_loop(client_connection)
    finally:
        try:
            client_connection.close()
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
