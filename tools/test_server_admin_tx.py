#!/usr/bin/env python3
"""Admin 管理端 UI 后端 + 数据库事务一致性测试。

对应 admin/mainwindow.cpp 各页面调用的 API，并直接查 SQLite 验证
「预约/结算/代结算」后订单、桩状态、余额、流水、操作日志是否一致。

用法：
  python3 tools/make_db.py
  cd server && ./charge-server
  python3 tools/test_server_admin_tx.py

用例分组：
  A  总览页 stats.overview（KPI、pile_status 四态、revenue_trend）
  B  用户页 user.admin.list 字段与 phone_keyword 筛选
  C  冻结/解冻往返；待支付时禁止冻结
  D  事务：reserve 后 DB 里订单与桩同为「预约」
  E  事务：settle 后余额、wallet_log、订单状态、桩回闲置
  F  事务：余额不足时不部分扣款，订单保持待支付
  G  事务：order.admin.settle 写入 operation_log「代结算」
  H  电桩页 list 筛选 / update / restart / 写日志 / 占用态拦截
  I  电桩 detail / create / delete
  J  订单页 order.list 字段与筛选
  K  电站 CRUD + operation_log.list
"""

from __future__ import annotations

import json
import socket
import sqlite3
import struct
import sys
from pathlib import Path
from typing import Any, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_common import admin_default_date_range


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
    """遍历 station 1~5 的 detail，返回第一个「闲置」桩号。"""
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
    """推进订单到已完成；余额不足时充值或 admin.settle。"""
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
    """A. Admin 总览页 — stats.overview 返回 UI 需要的 KPI、四态桩统计、7 日折线。"""
    print("\n========== A. Admin 总览页 API（mainwindow 总览） ==========")
    # A1：days=7 时检查 today_revenue / pile_status 四键 / revenue_trend 长度与结构
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

    trend = data.get("revenue_trend")
    if not isinstance(trend, list) or len(trend) != 7:
        raise RuntimeError(f"revenue_trend should be a list of 7 days, got {len(trend) if isinstance(trend, list) else type(trend)}")
    for item in trend:
        if "date" not in item or "revenue" not in item:
            raise RuntimeError("revenue_trend item missing date/revenue")

    # A2：不传 days 时使用默认天数（协议默认 7）
    run_test(
        host,
        port,
        {"id": "A2", "cmd": "stats.overview", "token": admin_token, "data": {}},
        "stats.overview default days",
    )


def test_admin_user_list_api(host: str, port: int, admin_token: str) -> None:
    """B. Admin 用户页 — user.admin.list 表格字段 + phone_keyword 筛选。"""
    print("\n========== B. Admin 用户页 API（mainwindow 用户表格） ==========")
    # B1：每行必须有 user_id/phone/nickname/balance/created_at/status
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

    # B2：phone_keyword=8001 只返回含 8001 的手机号
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

    # B3：无匹配 keyword 应返回空列表
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
    """C. 冻结 8003 → 不能登录 → 解冻 → 能登录（用户页冻结按钮）。"""
    print("\n========== C. Admin 冻结/解冻（用户页按钮） ==========")
    user_id = db_query_scalar(db_path(), "SELECT id FROM user WHERE phone = ?", ("13800138003",))

    # C1 冻结
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
    # C2 冻结后登录失败
    run_test(
        host,
        port,
        {"id": "C2", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003 after freeze",
        expect_ok=False,
    )

    # C3 解冻
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
    # C4 解冻后可登录
    run_test(
        host,
        port,
        {"id": "C4", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003 after unfreeze",
    )


def test_freeze_blocked_with_pending_order(host: str, port: int, admin_token: str) -> None:
    """C2. 8002 有待支付订单时冻结应被拒绝（避免冻结+待支付死锁）。"""
    print("\n========== C2. 待支付订单时禁止冻结 ==========")
    user_id = db_query_scalar(db_path(), "SELECT id FROM user WHERE phone = ?", ("13800138002",))
    run_test(
        host,
        port,
        {
            "id": "C5",
            "cmd": "user.freeze",
            "token": admin_token,
            "data": {"user_id": user_id, "freeze": True},
        },
        "user.freeze 8002 with 待支付 order",
        expect_ok=False,
    )


def test_tx_reserve_consistency(host: str, port: int, token: str, admin_token: str) -> None:
    """D. 事务一致性：reserve 成功后 SQLite 里 pile=预约 且 order=预约。"""
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
    """E. 事务一致性：settle 后订单已完成、桩闲置、余额扣减、wallet_log 增一条。"""
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
    """F. 余额不足时 settle 失败，且 balance 不被部分扣除，订单仍为待支付。"""
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
    """G. order.admin.settle 成功后 operation_log 多一条 action=代结算。"""
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


def test_admin_pile_page_api(host: str, port: int, token: str, admin_token: str) -> None:
    """H. Admin 电桩页 — list 筛选、update 写库与日志、restart、占用态禁止改状态/重启。"""
    print("\n========== H. Admin 电桩页 API（list / update / restart） ==========")
    db = db_path()

    all_piles = run_test(
        host,
        port,
        {"id": "H1", "cmd": "pile.list", "token": admin_token, "data": {}},
        "pile.list all",
    )
    items = all_piles["data"]["items"]
    if not items:
        raise RuntimeError("pile.list returned empty on seed db")

    required_fields = (
        "pile_no",
        "station_name",
        "type",
        "power_kw",
        "status",
        "charge_count",
        "charge_minutes",
    )
    for row in items:
        for field in required_fields:
            if field not in row:
                raise RuntimeError(f"pile.list row missing {field}: {row}")

    idle_only = run_test(
        host,
        port,
        {
            "id": "H2",
            "cmd": "pile.list",
            "token": admin_token,
            "data": {"status": "闲置"},
        },
        "pile.list status=闲置",
    )
    for row in idle_only["data"]["items"]:
        if row.get("status") != "闲置":
            raise RuntimeError(f"status filter leak: {row}")

    by_keyword = run_test(
        host,
        port,
        {
            "id": "H3",
            "cmd": "pile.list",
            "token": admin_token,
            "data": {"keyword": "SZ005"},
        },
        "pile.list keyword=SZ005",
    )
    for row in by_keyword["data"]["items"]:
        if "SZ005" not in row.get("pile_no", ""):
            raise RuntimeError(f"keyword filter leak: {row}")

    by_station = run_test(
        host,
        port,
        {
            "id": "H4",
            "cmd": "pile.list",
            "token": admin_token,
            "data": {"station_id": 1},
        },
        "pile.list station_id=1",
    )
    if not by_station["data"]["items"]:
        raise RuntimeError("station_id=1 should return piles")

    sz005_idle = [
        row
        for row in items
        if row.get("pile_no", "").startswith("SZ005") and row.get("status") == "闲置"
    ]
    if not sz005_idle:
        raise RuntimeError("need idle SZ005 pile for update/restart tests (run make_db.py?)")
    pile_no = sz005_idle[0]["pile_no"]
    orig_power = float(sz005_idle[0]["power_kw"])
    log_cnt_before = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")

    run_test_error(
        host,
        port,
        {
            "id": "H5c",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": "SZ001-05", "power_kw": 10.5},
        },
        "pile.update blocked (8002 待支付 on idle SZ001-05)",
        "INVALID_PARAM",
    )
    run_test(
        host,
        port,
        {
            "id": "H5",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": pile_no, "power_kw": 9.5},
        },
        f"pile.update {pile_no} power",
    )
    run_test_error(
        host,
        port,
        {
            "id": "H5b",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": pile_no, "status": "预约"},
        },
        "pile.update manual 预约 rejected",
        "INVALID_PARAM",
    )
    power_after = db_query_scalar(db, "SELECT power_kw FROM pile WHERE pile_no = ?", (pile_no,))
    if abs(power_after - 9.5) > 0.01:
        raise RuntimeError(f"pile.update not persisted: expected 9.5 db={power_after}")

    action = db_query_scalar(
        db, "SELECT action FROM operation_log ORDER BY id DESC LIMIT 1"
    )
    if action != "修改电桩":
        raise RuntimeError(f"pile.update should write operation_log 修改电桩, got {action}")

    run_test(
        host,
        port,
        {
            "id": "H5f",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": pile_no, "status": "故障"},
        },
        f"pile.update {pile_no} status 故障",
    )

    run_test(
        host,
        port,
        {
            "id": "H6",
            "cmd": "pile.restart",
            "token": admin_token,
            "data": {"pile_no": pile_no},
        },
        f"pile.restart fault {pile_no}",
    )
    status_after_restart = db_query_scalar(
        db, "SELECT status FROM pile WHERE pile_no = ?", (pile_no,)
    )
    if status_after_restart != "闲置":
        raise RuntimeError(f"after restart fault pile should be 闲置, got {status_after_restart}")

    restart_action = db_query_scalar(
        db, "SELECT action FROM operation_log ORDER BY id DESC LIMIT 1"
    )
    if restart_action != "远程重启电桩":
        raise RuntimeError(f"pile.restart log expected 远程重启电桩, got {restart_action}")

    run_test_error(
        host,
        port,
        {
            "id": "H6b",
            "cmd": "pile.restart",
            "token": admin_token,
            "data": {"pile_no": pile_no},
        },
        f"pile.restart idle {pile_no} rejected",
        "INVALID_PARAM",
    )

    # restore power for repeat runs
    run_test(
        host,
        port,
        {
            "id": "H6r",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": pile_no, "power_kw": orig_power},
        },
        f"pile.update restore power {pile_no}",
    )

    busy_pile = find_idle_pile(host, port, token)
    reserve = run_test(
        host,
        port,
        {"id": "H7a", "cmd": "charge.reserve", "token": token, "data": {"pile_no": busy_pile}},
        f"reserve {busy_pile} for restart block",
    )
    order_no = reserve["data"]["order_no"]
    run_test_error(
        host,
        port,
        {
            "id": "H7b",
            "cmd": "pile.restart",
            "token": admin_token,
            "data": {"pile_no": busy_pile},
        },
        "pile.restart on 预约 pile",
        "INVALID_PARAM",
    )
    run_test_error(
        host,
        port,
        {
            "id": "H7c",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": busy_pile, "status": "故障"},
        },
        "pile.update status while active order",
        "INVALID_PARAM",
    )
    finish_order(host, port, token, order_no, admin_token)

    log_cnt_after = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")
    if log_cnt_after < log_cnt_before + 3:
        raise RuntimeError("pile update/restart/restore should add operation_log rows")


def test_admin_pile_detail_and_create(host: str, port: int, token: str, admin_token: str) -> None:
    """I. pile.detail 闲置/预约态 current_order；pile.create 自动生成桩号并写日志。"""
    print("\n========== I. Admin 电桩 detail / create ==========")
    db = db_path()

    idle_detail = run_test(
        host,
        port,
        {
            "id": "I1",
            "cmd": "pile.detail",
            "token": admin_token,
            "data": {"pile_no": "SZ005-01"},
        },
        "pile.detail idle pile",
    )
    data = idle_detail["data"]
    for field in ("pile_no", "station_name", "type", "power_kw", "status", "current_order"):
        if field not in data:
            raise RuntimeError(f"pile.detail missing {field}: {data}")
    if data["current_order"] is not None:
        raise RuntimeError(f"idle pile should have null current_order, got {data['current_order']}")

    pile_no = find_idle_pile(host, port, token)
    reserve = run_test(
        host,
        port,
        {"id": "I2a", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
        f"reserve {pile_no} for pile.detail",
    )
    order_no = reserve["data"]["order_no"]

    active_detail = run_test(
        host,
        port,
        {
            "id": "I2",
            "cmd": "pile.detail",
            "token": admin_token,
            "data": {"pile_no": pile_no},
        },
        "pile.detail active pile",
    )
    current_order = active_detail["data"]["current_order"]
    if not current_order:
        raise RuntimeError("reserved pile should expose current_order in pile.detail")
    if current_order.get("order_no") != order_no:
        raise RuntimeError(f"current_order mismatch: {current_order}")
    if current_order.get("status") != "预约":
        raise RuntimeError(f"expected 预约 order, got {current_order.get('status')}")

    finish_order(host, port, token, order_no, admin_token)

    log_cnt_before = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")
    created = run_test(
        host,
        port,
        {
            "id": "I3",
            "cmd": "pile.create",
            "token": admin_token,
            "data": {"station_id": 5, "type": "慢充", "power_kw": 7.0},
        },
        "pile.create auto pile_no",
    )
    new_pile_no = created["data"]["pile_no"]
    if not new_pile_no.startswith("SZ005-"):
        raise RuntimeError(f"unexpected auto pile_no: {new_pile_no}")

    db_status = db_query_scalar(
        db, "SELECT status FROM pile WHERE pile_no = ?", (new_pile_no,)
    )
    if db_status != "闲置":
        raise RuntimeError(f"new pile should be 闲置, got {db_status}")

    action = db_query_scalar(
        db, "SELECT action FROM operation_log ORDER BY id DESC LIMIT 1"
    )
    if action != "新增电桩":
        raise RuntimeError(f"pile.create log expected 新增电桩, got {action}")

    run_test_error(
        host,
        port,
        {
            "id": "I4",
            "cmd": "pile.create",
            "token": admin_token,
            "data": {
                "station_id": 5,
                "type": "慢充",
                "power_kw": 7.0,
                "pile_no": new_pile_no,
            },
        },
        "pile.create duplicate pile_no",
        "INVALID_PARAM",
    )

    run_test(
        host,
        port,
        {"id": "I5", "cmd": "pile.delete", "token": admin_token, "data": {"pile_no": new_pile_no}},
        f"cleanup pile.delete {new_pile_no}",
    )

    log_cnt_after = db_query_scalar(db, "SELECT COUNT(*) FROM operation_log")
    if log_cnt_after < log_cnt_before + 2:
        raise RuntimeError("pile.create/delete should add operation_log rows")


def test_admin_order_page_api(host: str, port: int, admin_token: str) -> None:
    """J. Admin 订单页 — order.list 返回 UI 表格字段；status/phone 筛选。"""
    print("\n========== J. Admin 订单管理 order.list ==========")
    order_from, order_to = admin_default_date_range()

    all_orders = run_test(
        host,
        port,
        {
            "id": "J1",
            "cmd": "order.list",
            "token": admin_token,
            "data": {"limit": 50, "date_from": order_from, "date_to": order_to},
        },
        "order.list all",
    )
    items = all_orders["data"]["items"]
    if not items:
        raise RuntimeError("order.list returned empty on seed db")

    required_fields = (
        "order_no",
        "phone",
        "station_name",
        "pile_no",
        "status",
        "kwh",
        "amount",
        "reserve_at",
        "start_at",
        "end_at",
    )
    for row in items:
        for field in required_fields:
            if field not in row:
                raise RuntimeError(f"order.list row missing {field}: {row}")

    pending = run_test(
        host,
        port,
        {
            "id": "J2",
            "cmd": "order.list",
            "token": admin_token,
            "data": {
                "status": "待支付",
                "limit": 20,
                "date_from": order_from,
                "date_to": order_to,
            },
        },
        "order.list status=待支付",
    )
    pending_items = pending["data"]["items"]
    if not pending_items:
        raise RuntimeError("seed should have at least one 待支付 order")
    for row in pending_items:
        if row.get("status") != "待支付":
            raise RuntimeError(f"status filter leak: {row}")

    by_phone = run_test(
        host,
        port,
        {
            "id": "J3",
            "cmd": "order.list",
            "token": admin_token,
            "data": {
                "phone": "13800138002",
                "limit": 20,
                "date_from": order_from,
                "date_to": order_to,
            },
        },
        "order.list phone=8002",
    )
    for row in by_phone["data"]["items"]:
        if "13800138002" not in row.get("phone", ""):
            raise RuntimeError(f"phone filter leak: {row}")


def test_station_crud_and_operation_log(host: str, port: int, admin_token: str) -> None:
    """K. 电站 CRUD + operation_log.list — 含 idle_piles；有历史订单的站不可删。"""
    print("\n========== K. 电站 CRUD + 操作日志 ==========")
    log_from, log_to = admin_default_date_range()

    # K1：station.admin.list 含 idle_piles、online_rate（电站管理表格）
    stations = run_test(
        host,
        port,
        {"id": "K1", "cmd": "station.admin.list", "token": admin_token, "data": {}},
        "station.admin.list",
    )
    items = stations["data"]["items"]
    if not items:
        raise RuntimeError("station.admin.list empty")
    for row in items:
        for field in ("id", "name", "address", "price", "total_piles", "idle_piles", "online_rate"):
            if field not in row:
                raise RuntimeError(f"station.admin.list missing {field}: {row}")

    created = run_test(
        host,
        port,
        {
            "id": "K2",
            "cmd": "station.create",
            "token": admin_token,
            "data": {
                "name": "测试删除站K",
                "address": "深圳市测试区1号",
                "lat": 22.55,
                "lng": 114.06,
                "price": 1.25,
                "fast_count": 1,
                "slow_count": 1,
            },
        },
        "station.create for delete test",
    )
    station_id = created["data"]["station_id"]

    run_test(
        host,
        port,
        {
            "id": "K3",
            "cmd": "station.update",
            "token": admin_token,
            "data": {
                "station_id": station_id,
                "name": "测试删除站K-改",
                "address": "深圳市测试区2号",
                "lat": 22.56,
                "lng": 114.07,
                "price": 1.35,
            },
        },
        "station.update",
    )

    run_test_error(
        host,
        port,
        {
            "id": "K4",
            "cmd": "station.delete",
            "token": admin_token,
            "data": {"station_id": 1},
        },
        "station.delete blocked (history orders)",
        "INVALID_PARAM",
    )

    run_test(
        host,
        port,
        {
            "id": "K5",
            "cmd": "station.delete",
            "token": admin_token,
            "data": {"station_id": station_id},
        },
        "station.delete empty test station",
    )

    logs = run_test(
        host,
        port,
        {
            "id": "K6",
            "cmd": "operation_log.list",
            "token": admin_token,
            "data": {"date_from": log_from, "date_to": log_to, "limit": 50},
        },
        "operation_log.list",
    )
    log_items = logs["data"]["items"]
    if not log_items:
        raise RuntimeError("operation_log.list empty after station ops")
    for row in log_items:
        for field in ("action", "created_at", "admin_username"):
            if field not in row:
                raise RuntimeError(f"operation_log.list missing {field}: {row}")

    filtered = run_test(
        host,
        port,
        {
            "id": "K7",
            "cmd": "operation_log.list",
            "token": admin_token,
            "data": {
                "action": "修改电站",
                "date_from": log_from,
                "date_to": log_to,
                "limit": 20,
            },
        },
        "operation_log.list action filter",
    )
    for row in filtered["data"]["items"]:
        if row.get("action") != "修改电站":
            raise RuntimeError(f"operation_log action filter leak: {row}")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
    db = db_path()

    if not db.is_file():
        print(f"DB not found: {db}", file=sys.stderr)
        print("Run: python3 tools/make_db.py")
        return 1

    print(f"Admin+TX tests -> {host}:{port}, db={db}")

    # 前置登录：管理员 + 主测试用户 8001
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
    test_freeze_blocked_with_pending_order(host, port, admin_token)
    test_admin_pile_page_api(host, port, token, admin_token)
    test_admin_pile_detail_and_create(host, port, token, admin_token)
    test_admin_order_page_api(host, port, admin_token)
    test_station_crud_and_operation_log(host, port, admin_token)
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
