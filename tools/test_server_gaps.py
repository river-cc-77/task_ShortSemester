#!/usr/bin/env python3
"""Supplement tests for server APIs not covered (or not fully asserted) by test_server.py.

Run after charge-server is up. Recommended on a fresh seed db:
  cd db && rm -f charge.db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql
  cd ../server && ./charge-server   # terminal 1
  python3 tools/test_server.py      # optional baseline
  python3 tools/test_server_gaps.py # this script (~30s)
"""

import json
import random
import socket
import sqlite3
import struct
import sys
from pathlib import Path
from typing import Optional


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


def run_test_error(
    host: str, port: int, req: dict, label: str, expect_code: str
) -> dict:
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if resp.get("ok"):
        raise RuntimeError(f"{label} should have failed")
    code = resp.get("error", {}).get("code")
    if code != expect_code:
        raise RuntimeError(f"{label} expected {expect_code}, got {code}: {resp}")
    return resp


def db_path() -> Path:
    import os

    env = os.environ.get("ADS_DB")
    if env:
        return Path(env)
    return Path(__file__).resolve().parent.parent / "db" / "charge.db"


def finish_order(host: str, port: int, token: str, order_no: str, admin_token: Optional[str] = None) -> None:
    """Drive an open order to 已完成 (best-effort by current status)."""
    check = send_request(
        host, port,
        {"id": "fin0", "cmd": "order.check_open", "token": token, "data": {}},
    )
    if not check.get("ok"):
        return
    if not check.get("data", {}).get("has_open"):
        return
    order = check["data"]["order"]
    if order["order_no"] != order_no:
        order_no = order["order_no"]
    status = order["status"]
    if status == "预约":
        run_test(
            host, port,
            {"id": "fin1", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
            f"finish start {order_no}",
        )
        status = "充电中"
    if status == "充电中":
        stop = run_test(
            host, port,
            {"id": "fin2", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
            f"finish stop {order_no}",
        )
        order["amount"] = stop["data"]["amount"]
        status = "待支付"
    if status == "待支付":
        settle = send_request(
            host, port,
            {"id": "fin3", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
        )
        if settle.get("ok"):
            return
        code = settle.get("error", {}).get("code")
        if code == "BALANCE_NOT_ENOUGH":
            db = db_path()
            amount = order.get("amount")
            if amount is None or float(amount) <= 0:
                conn = sqlite3.connect(db)
                cur = conn.cursor()
                cur.execute("SELECT amount FROM charge_order WHERE order_no = ?", (order_no,))
                row = cur.fetchone()
                conn.close()
                amount = float(row[0]) if row else 0.0
            else:
                amount = float(amount)
            conn = sqlite3.connect(db)
            cur = conn.cursor()
            cur.execute(
                "SELECT u.balance FROM user u "
                "JOIN charge_order o ON o.user_id = u.id WHERE o.order_no = ?",
                (order_no,),
            )
            row = cur.fetchone()
            conn.close()
            balance = float(row[0]) if row else 0.0
            if balance + 0.001 < amount:
                need = round(amount - balance + 1.0, 2)
                if need < 0.01:
                    need = 1.0
                run_test(
                    host, port,
                    {"id": "finR", "cmd": "user.recharge", "token": token, "data": {"amount": need}},
                    f"finish recharge {need} for {order_no}",
                )
            if admin_token:
                run_test(
                    host, port,
                    {"id": "fin4", "cmd": "order.admin.settle", "token": admin_token,
                     "data": {"order_no": order_no}},
                    f"finish admin settle {order_no}",
                )
            else:
                run_test(
                    host, port,
                    {"id": "fin4", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
                    f"finish settle after recharge {order_no}",
                )
            return
        if admin_token:
            run_test(
                host, port,
                {"id": "fin4", "cmd": "order.admin.settle", "token": admin_token,
                 "data": {"order_no": order_no}},
                f"finish admin settle {order_no}",
            )
        else:
            raise RuntimeError(f"finish settle failed: {settle}")


def seed_expired_reservation(db: Path, pile_no: str = "SZ005-02", order_no: str = "CDTIMEOUT01") -> None:
    conn = sqlite3.connect(db)
    cur = conn.cursor()
    cur.execute("DELETE FROM charge_order WHERE order_no = ?", (order_no,))
    cur.execute("SELECT id FROM pile WHERE pile_no = ?", (pile_no,))
    row = cur.fetchone()
    if not row:
        conn.close()
        raise RuntimeError(f"pile {pile_no} not found in {db}")
    pile_id = row[0]
    cur.execute(
        """
        INSERT INTO charge_order
            (order_no, user_id, station_id, pile_id, status, reserve_at, created_at)
        VALUES (?, 3, 5, ?, '预约',
                datetime('now','localtime','-4 hours'),
                datetime('now','localtime','-4 hours'))
        """,
        (order_no, pile_id),
    )
    cur.execute("UPDATE pile SET status = '预约' WHERE pile_no = ?", (pile_no,))
    conn.commit()
    conn.close()
    print(f"\n>>> seeded expired reservation {order_no} on {pile_no}")


def test_3h_timeout(host: str, port: int, token8003: str) -> None:
    print("\n========== A. 预约超 3h 自动取消 ==========")
    seed_expired_reservation(db_path())
    open_check = run_test(
        host, port,
        {"id": "A1", "cmd": "order.check_open", "token": token8003, "data": {}},
        "order.check_open after timeout seed",
    )
    if open_check["data"].get("has_open"):
        raise RuntimeError("8003 should have no open order after timeout cleanup")
    reserve = run_test(
        host, port,
        {"id": "A2", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": "SZ005-02"}},
        "charge.reserve SZ005-02 after timeout",
    )
    finish_order(host, port, token8003, reserve["data"]["order_no"])


def test_illegal_charge_states(host: str, port: int, token8003: str, token8002: str) -> None:
    print("\n========== B. 非法状态 charge.start / charge.stop ==========")
    reserve = run_test(
        host, port,
        {"id": "B1a", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": "SZ005-03"}},
        "reserve for stop-on-预约",
    )
    order_no = reserve["data"]["order_no"]
    run_test_error(
        host, port,
        {"id": "B1b", "cmd": "charge.stop", "token": token8003, "data": {"order_no": order_no}},
        "charge.stop on 预约",
        "INVALID_PARAM",
    )
    finish_order(host, port, token8003, order_no)

    open8002 = run_test(
        host, port,
        {"id": "B2a", "cmd": "order.check_open", "token": token8002, "data": {}},
        "8002 check_open (need 待支付)",
    )
    if not open8002["data"].get("has_open"):
        raise RuntimeError("8002 needs a 待支付 order — rebuild db/seed before gap tests")
    pending_no = open8002["data"]["order"]["order_no"]
    if open8002["data"]["order"]["status"] != "待支付":
        raise RuntimeError(f"8002 open order should be 待支付, got {open8002['data']['order']['status']}")
    run_test_error(
        host, port,
        {"id": "B2b", "cmd": "charge.start", "token": token8002, "data": {"order_no": pending_no}},
        "charge.start on 待支付",
        "INVALID_PARAM",
    )


def test_unpaid_pile_still_reservable(host: str, port: int, token8001: str, token8002: str) -> None:
    print("\n========== C. 待支付时桩仍闲置，他人可预约使用 ==========")
    open8002 = run_test(
        host, port,
        {"id": "C1", "cmd": "order.check_open", "token": token8002, "data": {}},
        "8002 check_open (need 待支付 on SZ001-05)",
    )
    if not open8002["data"].get("has_open"):
        raise RuntimeError("8002 needs a 待支付 order — rebuild db/seed before gap tests")
    if open8002["data"]["order"].get("pile_no") != "SZ001-05":
        raise RuntimeError("8002 pending order should be on SZ001-05")

    finish_order(host, port, token8001, "", None)

    detail = run_test(
        host, port,
        {"id": "C2", "cmd": "station.detail", "token": token8001, "data": {"station_id": 1}},
        "station.detail for SZ001-05 status",
    )
    piles = {p["pile_no"]: p for p in detail["data"]["piles"]}
    if piles.get("SZ001-05", {}).get("status") != "闲置":
        raise RuntimeError("待支付订单的桩在列表中应显示闲置")

    reserve = run_test(
        host, port,
        {"id": "C3", "cmd": "charge.reserve", "token": token8001,
         "data": {"pile_no": "SZ001-05"}},
        "8001 reserve SZ001-05 while 8002 unpaid",
    )
    order_no = reserve["data"]["order_no"]
    finish_order(host, port, token8001, order_no, None)


def test_order_list_filters(host: str, port: int, admin_token: str) -> None:
    print("\n========== C. order.list 筛选 ==========")
    done = run_test(
        host, port,
        {"id": "C1", "cmd": "order.list", "token": admin_token,
         "data": {"status": "已完成", "limit": 10}},
        "order.list status=已完成",
    )
    items = done["data"]["items"]
    if items and not all(i.get("status") == "已完成" for i in items):
        raise RuntimeError("status filter returned non-已完成 rows")

    by_phone = run_test(
        host, port,
        {"id": "C2", "cmd": "order.list", "token": admin_token,
         "data": {"phone": "13800138001", "limit": 20}},
        "order.list phone=8001",
    )
    for row in by_phone["data"]["items"]:
        if "8001" not in row.get("phone", ""):
            raise RuntimeError(f"phone filter leak: {row}")

    run_test(
        host, port,
        {"id": "C3", "cmd": "order.list", "token": admin_token,
         "data": {"date_from": "2026-08-26", "date_to": "2026-08-28", "limit": 50}},
        "order.list date range",
    )


def test_pile_delete_busy(host: str, port: int, token8003: str, admin_token: str) -> None:
    print("\n========== D. pile.delete 预约态 ==========")
    reserve = run_test(
        host, port,
        {"id": "D1", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": "SZ005-04"}},
        "reserve for pile.delete test",
    )
    order_no = reserve["data"]["order_no"]
    run_test_error(
        host, port,
        {"id": "D2", "cmd": "pile.delete", "token": admin_token, "data": {"pile_no": "SZ005-04"}},
        "pile.delete on 预约 pile",
        "INVALID_PARAM",
    )
    finish_order(host, port, token8003, order_no, admin_token)


def test_admin_settle_duplicate(host: str, port: int, admin_token: str) -> None:
    print("\n========== E. order.admin.settle 重复结算 ==========")
    lst = run_test(
        host, port,
        {"id": "E1", "cmd": "order.list", "token": admin_token,
         "data": {"status": "已完成", "limit": 1}},
        "find completed order",
    )
    items = lst["data"]["items"]
    if not items:
        raise RuntimeError("no completed order for duplicate settle test")
    order_no = items[0]["order_no"]
    run_test_error(
        host, port,
        {"id": "E2", "cmd": "order.admin.settle", "token": admin_token,
         "data": {"order_no": order_no}},
        "order.admin.settle duplicate",
        "INVALID_PARAM",
    )


def test_station_create_validation(host: str, port: int, admin_token: str) -> None:
    print("\n========== F. station.create 参数校验 ==========")
    base = {
        "token": admin_token,
        "cmd": "station.create",
        "data": {
            "address": "深圳市测试路 99 号",
            "lat": 22.5,
            "lng": 114.0,
            "price": 1.2,
            "fast_count": 1,
            "slow_count": 0,
        },
    }
    run_test(
        host, port,
        {"id": "F1", **base, "data": {**base["data"], "name": ""}},
        "station.create empty name", expect_ok=False,
    )
    run_test(
        host, port,
        {"id": "F2", **base, "data": {**base["data"], "name": "测站", "price": 0}},
        "station.create zero price", expect_ok=False,
    )
    run_test(
        host, port,
        {"id": "F3", **base, "data": {**base["data"], "name": "测站", "fast_count": 0, "slow_count": 0}},
        "station.create no piles", expect_ok=False,
    )


def test_low_priority(host: str, port: int, token: str, admin_token: str) -> None:
    print("\n========== G. 低优先级补充 ==========")
    new_phone = f"139{random.randint(10000000, 99999999)}"
    new_user = run_test(
        host, port,
        {"id": "G1", "cmd": "user.login", "data": {"phone": new_phone}},
        f"user.login new phone {new_phone}",
    )
    if "token" not in new_user["data"]:
        raise RuntimeError("new user login missing token")

    avatar = run_test(
        host, port,
        {"id": "G2", "cmd": "user.profile.update", "token": token,
         "data": {"avatar_path": "/img/test_avatar.png"}},
        "user.profile.update avatar",
    )
    if avatar["data"].get("avatar_path") != "/img/test_avatar.png":
        raise RuntimeError("avatar_path not updated")

    run_test(
        host, port,
        {"id": "G3a", "cmd": "station.favorite.add", "token": token, "data": {"station_id": 2}},
        "station.favorite.add first",
    )
    run_test(
        host, port,
        {"id": "G3b", "cmd": "station.favorite.add", "token": token, "data": {"station_id": 2}},
        "station.favorite.add duplicate (OR IGNORE -> ok)",
    )
    run_test(
        host, port,
        {"id": "G3c", "cmd": "station.favorite.remove", "token": token, "data": {"station_id": 2}},
        "station.favorite.remove cleanup",
    )

    stats = run_test(
        host, port,
        {"id": "G4", "cmd": "stats.overview", "token": admin_token, "data": {"days": 7}},
        "stats.overview fields",
    )
    data = stats["data"]
    for key in (
        "today_revenue", "today_orders", "month_revenue",
        "total_revenue", "user_count", "revenue_trend", "pile_status",
    ):
        if key not in data:
            raise RuntimeError(f"stats.overview missing field: {key}")
    if not isinstance(data["revenue_trend"], list):
        raise RuntimeError("revenue_trend should be a list")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    db = db_path()
    if not db.is_file():
        print(f"DB not found: {db}", file=sys.stderr)
        print("Run: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql")
        return 1

    print(f"Gap tests -> {host}:{port}, db={db}")

    admin = run_test(
        host, port,
        {"id": "0", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        "admin.login",
    )
    admin_token = admin["data"]["token"]

    user = run_test(
        host, port,
        {"id": "0u", "cmd": "user.login", "data": {"phone": "13800138001"}},
        "user.login 8001",
    )
    token = user["data"]["token"]

    user8002 = run_test(
        host, port,
        {"id": "0u2", "cmd": "user.login", "data": {"phone": "13800138002"}},
        "user.login 8002",
    )
    token8002 = user8002["data"]["token"]

    user8003 = run_test(
        host, port,
        {"id": "0u3", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003",
    )
    token8003 = user8003["data"]["token"]

    test_3h_timeout(host, port, token8003)
    test_illegal_charge_states(host, port, token8003, token8002)
    test_unpaid_pile_still_reservable(host, port, token, token8002)
    test_order_list_filters(host, port, admin_token)
    test_pile_delete_busy(host, port, token8003, admin_token)
    test_admin_settle_duplicate(host, port, admin_token)
    test_station_create_validation(host, port, admin_token)
    test_low_priority(host, port, token, admin_token)

    print("\n========== H. event.push ==========")
    print("SKIP: server push needs long-lived client connection — manual UI test only.")

    print("\nAll gap tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nGAP TEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
