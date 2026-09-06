#!/usr/bin/env python3
"""Admin UI backend + transaction consistency tests (server 16c6912+).

Covers APIs used by admin/mainwindow.cpp and post-transaction DB invariants
not fully asserted by test_server.py / test_server_gaps.py.

Recommended run order (fresh db):
  python3 tools/make_db.py
  cd server && qmake6 charge-server.pro && make -j4 && ./charge-server   # terminal 1
  python3 tools/test_server.py                                          # optional baseline
  python3 tools/test_server_gaps.py                                     # optional
  python3 tools/test_server_admin_tx.py                                 # this script (~20s)
"""

from __future__ import annotations

import json
import socket
import sqlite3
import struct
import sys
from pathlib import Path
from typing import Any, Optional


def db_path() -> Path:
    import os

    env = os.environ.get("ADS_DB")
    if env:
        return Path(env)
    return Path(__file__).resolve().parent.parent / "db" / "charge.db"


def send_request(host: str, port: int, payload: dict) -> dict:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(frame)
        header = _recv_exact(sock, 4)
        length = struct.unpack(">I", header)[0]
        return json.loads(_recv_exact(sock, length).decode("utf-8"))


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


def run_test_error(host: str, port: int, req: dict, label: str, expect_code: str) -> dict:
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if resp.get("ok"):
        raise RuntimeError(f"{label} should have failed")
    code = resp.get("error", {}).get("code")
    if code != expect_code:
        raise RuntimeError(f"{label} expected {expect_code}, got {code}: {resp}")
    return resp


def db_query_one(db: Path, sql: str, params: tuple = ()) -> Optional[tuple]:
    conn = sqlite3.connect(db)
    cur = conn.cursor()
    cur.execute(sql, params)
    row = cur.fetchone()
    conn.close()
    return row


def db_query_scalar(db: Path, sql: str, params: tuple = ()) -> Any:
    row = db_query_one(db, sql, params)
    if row is None:
        raise RuntimeError(f"db query returned no row: {sql} {params}")
    return row[0]


def find_idle_pile(host: str, port: int, token: str) -> str:
    for station_id in range(1, 6):
        detail = send_request(
            host,
            port,
            {
                "id": f"idle{station_id}",
                "cmd": "station.detail",
                "token": token,
                "data": {"station_id": station_id},
            },
        )
        if not detail.get("ok"):
            continue
        for pile in detail["data"]["piles"]:
            if pile.get("status") == "闲置":
                return pile["pile_no"]
    raise RuntimeError("no idle pile found for transaction test")


def _order_amount(db: Path, order_no: str, order: dict) -> float:
    amount = order.get("amount")
    if amount is not None and float(amount) > 0:
        return float(amount)
    return float(db_query_scalar(db, "SELECT amount FROM charge_order WHERE order_no = ?", (order_no,)))


def _recharge_for_settle(host: str, port: int, token: str, order_no: str, order: dict) -> None:
    db = db_path()
    amount = _order_amount(db, order_no, order)
    balance = db_query_scalar(
        db,
        "SELECT u.balance FROM user u "
        "JOIN charge_order o ON o.user_id = u.id WHERE o.order_no = ?",
        (order_no,),
    )
    if balance + 0.001 >= amount:
        return
    need = round(amount - balance + 1.0, 2)
    if need < 0.01:
        need = 1.0
    run_test(
        host,
        port,
        {"id": "finR", "cmd": "user.recharge", "token": token, "data": {"amount": need}},
        f"finish recharge {need} for {order_no}",
    )


def finish_order(host: str, port: int, token: str, order_no: str, admin_token: str) -> None:
    check = send_request(
        host,
        port,
        {"id": "fin0", "cmd": "order.check_open", "token": token, "data": {}},
    )
    if not check.get("ok") or not check.get("data", {}).get("has_open"):
        return
    order = check["data"]["order"]
    if order_no and order["order_no"] != order_no:
        order_no = order["order_no"]
    elif not order_no:
        order_no = order["order_no"]
    status = order["status"]
    if status == "预约":
        run_test(
            host,
            port,
            {"id": "fin1", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
            f"finish start {order_no}",
        )
        status = "充电中"
    if status == "充电中":
        stop = run_test(
            host,
            port,
            {"id": "fin2", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
            f"finish stop {order_no}",
        )
        order["amount"] = stop["data"]["amount"]
        status = "待支付"
    if status != "待支付":
        return

    settle = send_request(
        host,
        port,
        {"id": "fin3", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
    )
    if settle.get("ok"):
        return

    code = settle.get("error", {}).get("code")
    if code == "BALANCE_NOT_ENOUGH":
        _recharge_for_settle(host, port, token, order_no, order)
        if admin_token:
            run_test(
                host,
                port,
                {
                    "id": "fin4",
                    "cmd": "order.admin.settle",
                    "token": admin_token,
                    "data": {"order_no": order_no},
                },
                f"finish admin settle {order_no}",
            )
        else:
            run_test(
                host,
                port,
                {"id": "fin4", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
                f"finish settle after recharge {order_no}",
            )
        return

    if admin_token:
        run_test(
            host,
            port,
            {
                "id": "fin4",
                "cmd": "order.admin.settle",
                "token": admin_token,
                "data": {"order_no": order_no},
            },
            f"finish admin settle {order_no}",
        )
    else:
        raise RuntimeError(f"finish settle failed: {settle}")


def test_admin_dashboard_api(host: str, port: int, admin_token: str) -> None:
    print("\n========== A. Admin 总览页 API（mainwindow 总览） ==========")
    stats = run_test(
        host,
        port,
        {"id": "A1", "cmd": "stats.overview", "token": admin_token, "data": {"days": 7}},
        "stats.overview days=7",
    )
    data = stats["data"]
    for key in ("today_revenue", "today_orders", "user_count", "pile_status"):
        if key not in data:
            raise RuntimeError(f"stats.overview missing {key} (admin UI needs it)")

    pile_status = data["pile_status"]
    if not isinstance(pile_status, dict):
        raise RuntimeError("pile_status should be object")
    for status_key in ("闲置", "预约", "在用", "故障"):
        if status_key not in pile_status:
            raise RuntimeError(f"pile_status missing {status_key} (admin UI expects all four)")

    run_test(
        host,
        port,
        {"id": "A2", "cmd": "stats.overview", "token": admin_token, "data": {}},
        "stats.overview default days",
    )


def test_admin_user_list_api(host: str, port: int, admin_token: str) -> None:
    print("\n========== B. Admin 用户页 API（mainwindow 用户表格） ==========")
    all_users = run_test(
        host,
        port,
        {"id": "B1", "cmd": "user.admin.list", "token": admin_token, "data": {}},
        "user.admin.list all",
    )
    items = all_users["data"]["items"]
    if not items:
        raise RuntimeError("user.admin.list returned empty")

    required_fields = ("user_id", "phone", "nickname", "balance", "created_at", "status")
    for row in items:
        for field in required_fields:
            if field not in row:
                raise RuntimeError(f"user.admin.list row missing {field}: {row}")

    filtered = run_test(
        host,
        port,
        {
            "id": "B2",
            "cmd": "user.admin.list",
            "token": admin_token,
            "data": {"phone_keyword": "8001"},
        },
        "user.admin.list phone_keyword=8001",
    )
    fitems = filtered["data"]["items"]
    if not fitems:
        raise RuntimeError("phone_keyword=8001 should match seed user")
    if not all("8001" in row.get("phone", "") for row in fitems):
        raise RuntimeError(f"phone_keyword filter leak: {fitems}")

    empty = run_test(
        host,
        port,
        {
            "id": "B3",
            "cmd": "user.admin.list",
            "token": admin_token,
            "data": {"phone_keyword": "19999999999"},
        },
        "user.admin.list no match",
    )
    if empty["data"]["items"]:
        raise RuntimeError("phone_keyword no match should return empty items")


def test_admin_freeze_roundtrip(host: str, port: int, admin_token: str) -> None:
    print("\n========== C. Admin 冻结/解冻（用户页按钮） ==========")
    user_id = db_query_scalar(db_path(), "SELECT id FROM user WHERE phone = ?", ("13800138003",))

    run_test(
        host,
        port,
        {
            "id": "C1",
            "cmd": "user.freeze",
            "token": admin_token,
            "data": {"user_id": user_id, "freeze": True},
        },
        "user.freeze 8003",
    )
    run_test(
        host,
        port,
        {"id": "C2", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003 after freeze",
        expect_ok=False,
    )

    run_test(
        host,
        port,
        {
            "id": "C3",
            "cmd": "user.freeze",
            "token": admin_token,
            "data": {"user_id": user_id, "freeze": False},
        },
        "user.freeze unfreeze 8003",
    )
    run_test(
        host,
        port,
        {"id": "C4", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003 after unfreeze",
    )


def test_tx_reserve_consistency(host: str, port: int, token: str, admin_token: str) -> None:
    print("\n========== D. 事务：reserve 订单与桩状态一致 ==========")
    db = db_path()
    finish_order(host, port, token, "", admin_token)

    pile_no = find_idle_pile(host, port, token)
    reserve = run_test(
        host,
        port,
        {"id": "D1", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
        f"charge.reserve {pile_no}",
    )
    order_no = reserve["data"]["order_no"]

    pile_status = db_query_scalar(db, "SELECT status FROM pile WHERE pile_no = ?", (pile_no,))
    if pile_status != "预约":
        raise RuntimeError(f"after reserve pile should be 预约, got {pile_status}")

    order_status = db_query_scalar(
        db, "SELECT status FROM charge_order WHERE order_no = ?", (order_no,)
    )
    if order_status != "预约":
        raise RuntimeError(f"after reserve order should be 预约, got {order_status}")

    finish_order(host, port, token, order_no, admin_token)


def test_tx_settle_consistency(host: str, port: int, token: str, admin_token: str) -> None:
    print("\n========== E. 事务：settle 扣款/流水/订单/桩统计一致 ==========")
    db = db_path()
    finish_order(host, port, token, "", admin_token)

    balance_before = db_query_scalar(db, "SELECT balance FROM user WHERE phone = ?", ("13800138001",))
    wallet_cnt_before = db_query_scalar(db, "SELECT COUNT(*) FROM wallet_log WHERE user_id = 1")

    pile_no = find_idle_pile(host, port, token)
    reserve = run_test(
        host,
        port,
        {"id": "E1", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
        f"charge.reserve {pile_no}",
    )
    order_no = reserve["data"]["order_no"]
    run_test(
        host,
        port,
        {"id": "E2", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
        "charge.start",
    )
    stop = run_test(
        host,
        port,
        {"id": "E3", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
        "charge.stop",
    )
    amount = stop["data"]["amount"]

    settle = run_test(
        host,
        port,
        {"id": "E4", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
        "charge.settle",
    )
    balance_after_api = settle["data"]["balance_after"]

    order_status = db_query_scalar(
        db, "SELECT status FROM charge_order WHERE order_no = ?", (order_no,)
    )
    if order_status != "已完成":
        raise RuntimeError(f"after settle order should be 已完成, got {order_status}")

    pile_status = db_query_scalar(db, "SELECT status FROM pile WHERE pile_no = ?", (pile_no,))
    if pile_status != "闲置":
        raise RuntimeError(f"after full flow pile should be 闲置, got {pile_status}")

    balance_after_db = db_query_scalar(db, "SELECT balance FROM user WHERE phone = ?", ("13800138001",))
    expected = round(balance_before - amount, 2)
    if abs(balance_after_db - expected) > 0.01:
        raise RuntimeError(
            f"balance mismatch: before={balance_before} amount={amount} "
            f"expected={expected} db={balance_after_db}"
        )
    if abs(balance_after_api - balance_after_db) > 0.01:
        raise RuntimeError(f"API balance_after {balance_after_api} != db {balance_after_db}")

    wallet_cnt_after = db_query_scalar(db, "SELECT COUNT(*) FROM wallet_log WHERE user_id = 1")
    if wallet_cnt_after != wallet_cnt_before + 1:
        raise RuntimeError("settle should add exactly one wallet_log row")

    log_delta = db_query_scalar(
        db,
        "SELECT delta FROM wallet_log WHERE user_id = 1 ORDER BY id DESC LIMIT 1",
    )
    if abs(log_delta + amount) > 0.01:
        raise RuntimeError(f"wallet_log delta should be -amount, got {log_delta} vs -{amount}")


def test_tx_settle_insufficient_no_partial(host: str, port: int, admin_token: str) -> None:
    print("\n========== F. 事务：余额不足时不应部分扣款 ==========")
    db = db_path()

    user8004 = run_test(
        host,
        port,
        {"id": "F1", "cmd": "user.login", "data": {"phone": "13800138004"}},
        "user.login 8004",
    )
    token8004 = user8004["data"]["token"]

    # test_server.py 可能给 8004 留下待支付单，先清理
    finish_order(host, port, token8004, "", admin_token)

    balance_before = db_query_scalar(db, "SELECT balance FROM user WHERE phone = ?", ("13800138004",))

    pile_no = find_idle_pile(host, port, token8004)
    reserve = run_test(
        host,
        port,
        {"id": "F2", "cmd": "charge.reserve", "token": token8004, "data": {"pile_no": pile_no}},
        f"charge.reserve 8004 on {pile_no}",
    )
    order_no = reserve["data"]["order_no"]
    run_test(
        host,
        port,
        {"id": "F3", "cmd": "charge.start", "token": token8004, "data": {"order_no": order_no}},
        "charge.start 8004",
    )
    stop = run_test(
        host,
        port,
        {"id": "F4", "cmd": "charge.stop", "token": token8004, "data": {"order_no": order_no}},
        "charge.stop 8004",
    )
    amount = stop["data"]["amount"]
    if amount <= balance_before:
        print(f"SKIP F: stop amount {amount} <= balance {balance_before}, cannot test insufficient settle")
        finish_order(host, port, token8004, order_no, admin_token)
        return

    run_test_error(
        host,
        port,
        {"id": "F5", "cmd": "charge.settle", "token": token8004, "data": {"order_no": order_no}},
        "charge.settle insufficient",
        "BALANCE_NOT_ENOUGH",
    )

    balance_after = db_query_scalar(db, "SELECT balance FROM user WHERE phone = ?", ("13800138004",))
    if abs(balance_after - balance_before) > 0.001:
        raise RuntimeError(f"insufficient settle changed balance: {balance_before} -> {balance_after}")

    order_status = db_query_scalar(
        db, "SELECT status FROM charge_order WHERE order_no = ?", (order_no,)
    )
    if order_status != "待支付":
        raise RuntimeError(f"insufficient settle should leave 待支付, got {order_status}")

    finish_order(host, port, token8004, order_no, admin_token)


def test_tx_admin_settle_operation_log(host: str, port: int, token: str, admin_token: str) -> None:
    print("\n========== G. 事务：admin.settle 写 operation_log ==========")
    db = db_path()
    log_cnt_before = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")

    pile_no = find_idle_pile(host, port, token)
    reserve = run_test(
        host,
        port,
        {"id": "G1", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
        f"charge.reserve for admin settle log",
    )
    order_no = reserve["data"]["order_no"]
    run_test(
        host,
        port,
        {"id": "G2", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
        "charge.start",
    )
    run_test(
        host,
        port,
        {"id": "G3", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
        "charge.stop",
    )
    run_test(
        host,
        port,
        {
            "id": "G4",
            "cmd": "order.admin.settle",
            "token": admin_token,
            "data": {"order_no": order_no},
        },
        "order.admin.settle",
    )

    log_cnt_after = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")
    if log_cnt_after != log_cnt_before + 1:
        raise RuntimeError("order.admin.settle should add one operation_log row")

    action = db_query_scalar(
        db, "SELECT action FROM operation_log ORDER BY id DESC LIMIT 1"
    )
    if action != "代结算":
        raise RuntimeError(f"expected operation_log action 代结算, got {action}")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
    db = db_path()

    if not db.is_file():
        print(f"DB not found: {db}", file=sys.stderr)
        print("Run: python3 tools/make_db.py")
        return 1

    print(f"Admin+TX tests -> {host}:{port}, db={db}")

    admin = run_test(
        host,
        port,
        {"id": "0", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        "admin.login",
    )
    admin_token = admin["data"]["token"]

    user = run_test(
        host,
        port,
        {"id": "0u", "cmd": "user.login", "data": {"phone": "13800138001"}},
        "user.login 8001",
    )
    token = user["data"]["token"]

    test_admin_dashboard_api(host, port, admin_token)
    test_admin_user_list_api(host, port, admin_token)
    test_admin_freeze_roundtrip(host, port, admin_token)
    test_tx_reserve_consistency(host, port, token, admin_token)
    test_tx_settle_consistency(host, port, token, admin_token)
    test_tx_settle_insufficient_no_partial(host, port, admin_token)
    test_tx_admin_settle_operation_log(host, port, token, admin_token)

    print("\nAll admin+transaction tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nADMIN+TX TEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
