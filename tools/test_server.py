#!/usr/bin/env python3
"""Minimal client to test charge-server P0 commands."""

import json
import socket
import struct
import sys


def send_request(host: str, port: int, payload: dict) -> dict:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body

    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(frame)
        header = _recv_exact(sock, 4)
        length = struct.unpack(">I", header)[0]
        response_body = _recv_exact(sock, length)
        return json.loads(response_body.decode("utf-8"))


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    tests = [
        {"id": "1", "cmd": "ping", "data": {}},
        {"id": "2", "cmd": "user.login", "data": {"phone": "13800138001"}},
        {"id": "3", "cmd": "user.login", "data": {"phone": "13800138006"}},
        {"id": "4", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        {"id": "5", "cmd": "admin.login", "data": {"username": "admin", "password": "wrong"}},
    ]

    for req in tests:
        resp = send_request(host, port, req)
        print(f"\n>>> {req['cmd']}")
        print(json.dumps(resp, ensure_ascii=False, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
