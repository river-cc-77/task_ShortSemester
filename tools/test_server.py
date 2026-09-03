#!/usr/bin/env python3
"""Test charge-server P0 + P1 commands: user, station, order, charge, admin, stats."""

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

    # ===== P0 基础 =====
    run_test(host, port, {"id": "1", "cmd": "ping", "data": {}}, "ping")

    user = run_test(
        host, port,
        {"id": "2", "cmd": "user.login", "data": {"phone": "13800138001"}},
        "user.login",
    )
    token = user["data"]["token"]

    run_test(
        host, port,
        {"id": "3", "cmd": "user.login", "data": {"phone": "13800138006"}},
        "user.login frozen", expect_ok=False,
    )

    admin = run_test(
        host, port,
        {"id": "4", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        "admin.login",
    )
    admin_token = admin["data"]["token"]

    run_test(
        host, port,
        {"id": "5", "cmd": "admin.login", "data": {"username": "admin", "password": "wrong"}},
        "admin.login wrong password", expect_ok=False,
    )

    # ===== 充电站 =====
    stations = run_test(
        host, port,
        {"id": "6", "cmd": "station.list", "token": token,
         "data": {"lat": 22.5431, "lng": 114.0579, "keyword": ""}},
        "station.list",
    )
    items = stations["data"]["items"]
    if len(items) < 1:
        raise RuntimeError("station.list returned no items")
    distances = [item["distance_km"] for item in items]
    if distances != sorted(distances):
        raise RuntimeError("station.list not sorted by distance")

    station_id = items[0]["id"]
    detail = run_test(
        host, port,
        {"id": "7", "cmd": "station.detail", "token": token, "data": {"station_id": station_id}},
        "station.detail",
    )
    piles = detail["data"]["piles"]
    if len(piles) < 1:
        raise RuntimeError("station.detail has no piles")
    pile_no = piles[0]["pile_no"]

    # ===== 用户资料 & 充值 =====
    run_test(
        host, port,
        {"id": "8", "cmd": "user.profile.update", "token": token,
         "data": {"nickname": "测试用户"}},
        "user.profile.update",
    )

    run_test(
        host, port,
        {"id": "9", "cmd": "user.recharge", "token": token, "data": {"amount": 50.0}},
        "user.recharge",
    )

    # ===== 收藏 =====
    run_test(
        host, port,
        {"id": "10", "cmd": "station.favorite.add", "token": token,
         "data": {"station_id": station_id}},
        "station.favorite.add",
    )
    run_test(
        host, port,
        {"id": "11", "cmd": "station.favorite.list", "token": token, "data": {}},
        "station.favorite.list",
    )
    run_test(
        host, port,
        {"id": "12", "cmd": "station.favorite.remove", "token": token,
         "data": {"station_id": station_id}},
        "station.favorite.remove",
    )

    # ===== 公告 =====
    run_test(
        host, port,
        {"id": "13", "cmd": "announcement.list", "token": token, "data": {}},
        "announcement.list",
    )

    # ===== 订单检查 =====
    run_test(
        host, port,
        {"id": "14", "cmd": "order.check_open", "token": token, "data": {}},
        "order.check_open (no open)",
    )

    # ===== 充电全流程 =====
    # 找一个闲置电桩
    idle_pile = None
    for p in piles:
        if p["status"] == "闲置":
            idle_pile = p
            break
    if idle_pile is None:
        print("\nWARNING: no idle pile found, skipping charge flow tests")
    else:
        pile_no = idle_pile["pile_no"]

        reserve = run_test(
            host, port,
            {"id": "15", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
            "charge.reserve",
        )
        order_no = reserve["data"]["order_no"]

        run_test(
            host, port,
            {"id": "16", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
            "charge.start",
        )

        run_test(
            host, port,
            {"id": "17", "cmd": "charge.progress", "token": token, "data": {"order_no": order_no}},
            "charge.progress",
        )

        stop = run_test(
            host, port,
            {"id": "18", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
            "charge.stop",
        )
        amount = stop["data"]["amount"]

        run_test(
            host, port,
            {"id": "19", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
            "charge.settle",
        )

    # ===== 订单列表 =====
    run_test(
        host, port,
        {"id": "20", "cmd": "order.list", "token": token, "data": {"limit": 10}},
        "order.list (user)",
    )

    # ===== 管理端 =====
    run_test(
        host, port,
        {"id": "21", "cmd": "station.admin.list", "token": admin_token, "data": {}},
        "station.admin.list",
    )

    run_test(
        host, port,
        {"id": "22", "cmd": "user.admin.list", "token": admin_token, "data": {}},
        "user.admin.list",
    )

    run_test(
        host, port,
        {"id": "23", "cmd": "pile.list", "token": admin_token, "data": {}},
        "pile.list",
    )

    run_test(
        host, port,
        {"id": "24", "cmd": "stats.overview", "token": admin_token, "data": {"days": 7}},
        "stats.overview",
    )

    run_test(
        host, port,
        {"id": "25", "cmd": "order.list", "token": admin_token, "data": {"limit": 10}},
        "order.list (admin)",
    )

    # ===== 错误处理测试 =====
    run_test(
        host, port,
        {"id": "26", "cmd": "user.recharge", "token": token, "data": {"amount": -10}},
        "user.recharge invalid amount", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "27", "cmd": "station.detail", "token": token, "data": {"station_id": 99999}},
        "station.detail not found", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "28", "cmd": "unknown.cmd", "token": token, "data": {}},
        "unknown cmd", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "29", "cmd": "user.profile.update", "token": "invalid_token", "data": {}},
        "unauthorized", expect_ok=False,
    )

    print("\nAll tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nTEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
