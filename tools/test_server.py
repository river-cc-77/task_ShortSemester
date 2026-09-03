#!/usr/bin/env python3
"""Test charge-server P0 + P1 station commands."""

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


def run_test(host: str, port: int, req: dict, label: str, expect_ok: bool = True) -> dict:
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if expect_ok and not resp.get("ok"):
        raise RuntimeError(f"{label} failed: {resp}")
    if not expect_ok and resp.get("ok"):
        raise RuntimeError(f"{label} should have failed")
    return resp


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    run_test(host, port, {"id": "1", "cmd": "ping", "data": {}}, "ping")

    user = run_test(
        host,
        port,
        {"id": "2", "cmd": "user.login", "data": {"phone": "13800138001"}},
        "user.login",
    )
    token = user["data"]["token"]

    run_test(
        host,
        port,
        {"id": "3", "cmd": "user.login", "data": {"phone": "13800138006"}},
        "user.login frozen",
        expect_ok=False,
    )

    run_test(
        host,
        port,
        {"id": "4", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        "admin.login",
    )

    run_test(
        host,
        port,
        {"id": "5", "cmd": "admin.login", "data": {"username": "admin", "password": "wrong"}},
        "admin.login wrong password",
        expect_ok=False,
    )

    stations = run_test(
        host,
        port,
        {
            "id": "6",
            "cmd": "station.list",
            "token": token,
            "data": {"lat": 22.5431, "lng": 114.0579, "keyword": ""},
        },
        "station.list",
    )
    items = stations["data"]["items"]
    if len(items) < 1:
        raise RuntimeError("station.list returned no items")
    distances = [item["distance_km"] for item in items]
    if distances != sorted(distances):
        raise RuntimeError("station.list not sorted by distance")

    run_test(
        host,
        port,
        {
            "id": "7",
            "cmd": "station.detail",
            "token": token,
            "data": {"station_id": items[0]["id"]},
        },
        "station.detail",
    )

    print("\nAll tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nTEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
