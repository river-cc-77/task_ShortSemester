#!/usr/bin/env python3
"""event.push 冒烟测试：长连接用户应收到 charge.progress 推送。"""

from __future__ import annotations

import json
import socket
import struct
import sys
import threading
import time


def send_request(host: str, port: int, payload: dict) -> dict:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body
    with socket.create_connection((host, port), timeout=8) as sock:
        sock.sendall(frame)
        header = sock.recv(4)
        length = struct.unpack(">I", header)[0]
        resp = sock.recv(length)
        return json.loads(resp.decode("utf-8"))


def pick_idle_pile(host: str, port: int, admin_token: str) -> dict:
    resp = send_request(
        host,
        port,
        {"id": "p1", "cmd": "pile.list", "token": admin_token, "data": {"status": "闲置"}},
    )
    items = resp.get("data", {}).get("items", [])
    if not items:
        raise RuntimeError("no idle pile")
    return items[0]


def listen_push(host: str, port: int, token: str, timeout_sec: float = 10.0) -> dict | None:
    body = json.dumps(
        {"id": "bind", "cmd": "user.profile.get", "token": token, "data": {}},
        ensure_ascii=False,
    ).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body

    with socket.create_connection((host, port), timeout=8) as sock:
        sock.sendall(frame)
        deadline = time.time() + timeout_sec
        buffer = b""
        while time.time() < deadline:
            sock.settimeout(max(0.2, deadline - time.time()))
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            buffer += chunk
            while len(buffer) >= 4:
                length = struct.unpack(">I", buffer[:4])[0]
                if len(buffer) < 4 + length:
                    break
                payload = json.loads(buffer[4 : 4 + length].decode("utf-8"))
                buffer = buffer[4 + length :]
                if payload.get("cmd") == "event.push":
                    return payload
    return None


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    user = send_request(
        host,
        port,
        {"id": "1", "cmd": "user.login", "data": {"phone": "13800138001", "password": "123456"}},
    )
    if not user.get("ok"):
        raise RuntimeError(f"login failed: {user}")
    token = user["data"]["token"]

    admin = send_request(
        host,
        port,
        {"id": "2", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
    )
    pile = pick_idle_pile(host, port, admin["data"]["token"])
    pile_no = pile["pile_no"]

    push_result: dict | None = None

    def worker() -> None:
        nonlocal push_result
        push_result = listen_push(host, port, token, timeout_sec=12.0)

    t = threading.Thread(target=worker, daemon=True)
    t.start()
    time.sleep(0.8)

    reserve = send_request(
        host,
        port,
        {"id": "r1", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
    )
    if not reserve.get("ok"):
        raise RuntimeError(f"reserve failed: {reserve}")
    order_no = reserve["data"]["order_no"]

    start = send_request(
        host,
        port,
        {"id": "s1", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
    )
    if not start.get("ok"):
        raise RuntimeError(f"start failed: {start}")

    t.join(timeout=15.0)
    if not push_result:
        raise RuntimeError("未收到 event.push（请确认已重编 charge-server）")

    data = push_result.get("data", {})
    if data.get("type") != "charge.progress":
        raise RuntimeError(f"unexpected push type: {data.get('type')}")
    if data.get("payload", {}).get("order_no") != order_no:
        raise RuntimeError("push order_no mismatch")

    print("OK event.push charge.progress:", json.dumps(push_result, ensure_ascii=False))

    send_request(
        host,
        port,
        {"id": "x1", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
    )
    send_request(
        host,
        port,
        {"id": "x2", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
    )
    print("All event.push checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
