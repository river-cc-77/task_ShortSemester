#!/usr/bin/env python3
"""test_server.py 的补充测试 — 覆盖主测试未深入断言的边界场景。

用法（建议 fresh seed 库，先跑 test_server.py 也可）：
  cd db && rm -f charge.db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql
  cd ../server && ./charge-server
  python3 tools/test_server_gaps.py

用例分组：
  A  预约超过 3 小时自动取消（需直接写 DB 模拟超时）
  B  非法状态：预约态不能 stop、待支付态不能 start
  C  待支付时桩对外仍显示闲置，他人可预约；order.list 筛选
  D  pile.delete 在预约/待支付/有历史订单时禁止
  D3 pile.update 待支付未完成单时禁止改桩（桩显示闲置但有 open order）
  E  order.admin.settle 重复结算拦截
  F  station.create 参数校验（空名、零电价、零桩数）
  G  低优先级：新号注册、头像、重复收藏、stats 字段完整性
  H  event.push — 跳过（需长连接，仅 UI 手测）
"""

import json
import random
import socket
import sqlite3
import struct
import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_common import admin_default_date_range, seed_order_sample_range


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
    """把一笔未完成订单推进到「已完成」，供 gap 测试清理现场。

    若用户余额不足会先充值或走 admin.settle。
    """
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
    """在 SQLite 里插入一条 4 小时前的「预约」单，模拟超过 3h 未启动的预约。

    服务端在 check_open / reserve 时应自动取消超时预约并释放桩。
    """
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
    """A. 预约超 3 小时自动取消

    1) 写入超时预约 → 2) check_open 触发清理，has_open=false
    3) 同一桩可再次预约
    """
    print("\n========== A. 预约超 3h 自动取消 ==========")
    seed_expired_reservation(db_path())
    # A1：访问 check_open 时服务端应清理超时单
    open_check = run_test(
        host, port,
        {"id": "A1", "cmd": "order.check_open", "token": token8003, "data": {}},
        "order.check_open after timeout seed",
    )
    if open_check["data"].get("has_open"):
        raise RuntimeError("8003 should have no open order after timeout cleanup")
    # A2：桩已释放，8003 可重新预约 SZ005-02
    reserve = run_test(
        host, port,
        {"id": "A2", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": "SZ005-02"}},
        "charge.reserve SZ005-02 after timeout",
    )
    finish_order(host, port, token8003, reserve["data"]["order_no"])


def test_illegal_charge_states(host: str, port: int, token8003: str, token8002: str) -> None:
    """B. 非法状态的 charge.start / charge.stop

    B1：仅「预约」时不能 stop（须先 start 到充电中）
    B2：「待支付」时不能 start（须 settle 或重新预约）
    """
    print("\n========== B. 非法状态 charge.start / charge.stop ==========")
    # B1a/B1b：预约态调用 stop → INVALID_PARAM
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

    # B2a/B2b：8002 的 seed 待支付单上调用 start → INVALID_PARAM
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
    """C1. 待支付订单不占用桩对外展示 — 他人仍可预约

    业务规则：用户 A 待支付时，桩在列表里仍显示「闲置」，
    用户 B 可以预约同一桩（A 的订单仍阻塞 A 自己开新单）。
    """
    print("\n========== C. 待支付时桩仍闲置，他人可预约使用 ==========")
    # C1：确认 8002 在 SZ001-05 上有待支付单
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

    # C2：station.detail 里 SZ001-05 应对外显示「闲置」
    detail = run_test(
        host, port,
        {"id": "C2", "cmd": "station.detail", "token": token8001, "data": {"station_id": 1}},
        "station.detail for SZ001-05 status",
    )
    piles = {p["pile_no"]: p for p in detail["data"]["piles"]}
    if piles.get("SZ001-05", {}).get("status") != "闲置":
        raise RuntimeError("待支付订单的桩在列表中应显示闲置")

    # C3：8001 可以预约该桩并完成流程
    reserve = run_test(
        host, port,
        {"id": "C3", "cmd": "charge.reserve", "token": token8001,
         "data": {"pile_no": "SZ001-05"}},
        "8001 reserve SZ001-05 while 8002 unpaid",
    )
    order_no = reserve["data"]["order_no"]
    finish_order(host, port, token8001, order_no, None)


def test_order_list_filters(host: str, port: int, admin_token: str) -> None:
    """C2. order.list 管理端筛选

    测 status=已完成、phone 模糊、date_from/date_to 日期范围。
    """
    print("\n========== C2. order.list 筛选 ==========")
    # C1：只返回「已完成」
    done = run_test(
        host, port,
        {"id": "C1", "cmd": "order.list", "token": admin_token,
         "data": {"status": "已完成", "limit": 10}},
        "order.list status=已完成",
    )
    items = done["data"]["items"]
    if not items:
        raise RuntimeError("order.list status=已完成 returned empty on seed db")
    if not all(i.get("status") == "已完成" for i in items):
        raise RuntimeError("status filter returned non-已完成 rows")

    # C2：按手机号筛 8001
    by_phone = run_test(
        host, port,
        {"id": "C2", "cmd": "order.list", "token": admin_token,
         "data": {"phone": "13800138001", "limit": 20}},
        "order.list phone=8001",
    )
    for row in by_phone["data"]["items"]:
        if "8001" not in row.get("phone", ""):
            raise RuntimeError(f"phone filter leak: {row}")

    # C3：按 seed 固定日期区间筛选（2026-08-26~28 有演示订单）
    sample_from, sample_to = seed_order_sample_range()
    run_test(
        host, port,
        {"id": "C3", "cmd": "order.list", "token": admin_token,
         "data": {"date_from": sample_from, "date_to": sample_to, "limit": 50}},
        "order.list date range (seed sample)",
    )
    order_from, order_to = admin_default_date_range()
    run_test(
        host, port,
        {"id": "C4", "cmd": "order.list", "token": admin_token,
         "data": {"date_from": order_from, "date_to": order_to, "limit": 50}},
        "order.list date range (admin default)",
    )


def test_pile_update_open_order_on_idle_pile(host: str, port: int, admin_token: str) -> None:
    """D3. 桩显示「闲置」但存在他人待支付单时，pile.update 应拒绝（SZ001-05 / 8002 seed）。"""
    print("\n========== D3. pile.update 待支付 open order ==========")
    run_test_error(
        host, port,
        {"id": "D5", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": "SZ001-05", "power_kw": 11.0}},
        "pile.update blocked (8002 待支付 on idle pile)",
        "INVALID_PARAM",
    )


def test_pile_delete_busy(host: str, port: int, token8003: str, admin_token: str) -> None:
    """D1. pile.delete — 桩处于「预约」时不可删"""
    print("\n========== D. pile.delete 预约态 ==========")
    reserve = run_test(
        host, port,
        {"id": "D1", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": "SZ005-04"}},
        "reserve for pile.delete test",
    )
    order_no = reserve["data"]["order_no"]
    # D2：预约态删除桩 → INVALID_PARAM
    run_test_error(
        host, port,
        {"id": "D2", "cmd": "pile.delete", "token": admin_token, "data": {"pile_no": "SZ005-04"}},
        "pile.delete on 预约 pile",
        "INVALID_PARAM",
    )
    finish_order(host, port, token8003, order_no, admin_token)


def test_pile_delete_open_and_history(host: str, port: int, admin_token: str) -> None:
    """D2. pile.delete — 有待支付未完成单 / 有历史已完成单的桩不可删

    D3：SZ001-05 — 8002 待支付（桩显示闲置但有 open order）
    D4：SZ001-02 — seed 中有已完成历史订单
    """
    print("\n========== D2. pile.delete 待支付/历史订单 ==========")
    run_test_error(
        host, port,
        {"id": "D3", "cmd": "pile.delete", "token": admin_token, "data": {"pile_no": "SZ001-05"}},
        "pile.delete on 待支付 order pile (idle status)",
        "INVALID_PARAM",
    )
    run_test_error(
        host, port,
        {"id": "D4", "cmd": "pile.delete", "token": admin_token, "data": {"pile_no": "SZ001-02"}},
        "pile.delete on pile with completed orders",
        "INVALID_PARAM",
    )


def test_admin_settle_duplicate(host: str, port: int, admin_token: str) -> None:
    """E. order.admin.settle 对已完成的订单再次代结算应失败"""
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
    """F. station.create 参数校验 — 空站名、零电价、快慢桩都为 0"""
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
    # F1 空站名
    run_test(
        host, port,
        {"id": "F1", **base, "data": {**base["data"], "name": ""}},
        "station.create empty name", expect_ok=False,
    )
    # F2 电价为 0
    run_test(
        host, port,
        {"id": "F2", **base, "data": {**base["data"], "name": "测站", "price": 0}},
        "station.create zero price", expect_ok=False,
    )
    # F3 不创建任何桩
    run_test(
        host, port,
        {"id": "F3", **base, "data": {**base["data"], "name": "测站", "fast_count": 0, "slow_count": 0}},
        "station.create no piles", expect_ok=False,
    )


def test_low_priority(host: str, port: int, token: str, admin_token: str) -> None:
    """G. 低优先级补充

    G1 新手机号自动注册 | G2 头像路径更新 | G3 重复收藏幂等
    G4 stats.overview 字段齐全且 revenue_trend 天数与 days 参数一致
    """
    print("\n========== G. 低优先级补充 ==========")
    # G1：未注册手机号登录即注册
    new_phone = f"139{random.randint(10000000, 99999999)}"
    new_user = run_test(
        host, port,
        {"id": "G1", "cmd": "user.login", "data": {"phone": new_phone}},
        f"user.login new phone {new_phone}",
    )
    if "token" not in new_user["data"]:
        raise RuntimeError("new user login missing token")

    # G2：更新 avatar_path 并校验回写
    avatar = run_test(
        host, port,
        {"id": "G2", "cmd": "user.profile.update", "token": token,
         "data": {"avatar_path": "/img/test_avatar.png"}},
        "user.profile.update avatar",
    )
    if avatar["data"].get("avatar_path") != "/img/test_avatar.png":
        raise RuntimeError("avatar_path not updated")

    # G3：重复收藏应成功（DB OR IGNORE），再 remove 清理
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

    # G4：stats 返回 Admin 折线图所需的全部 KPI 字段
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
    trend = data["revenue_trend"]
    if len(trend) != 7:
        raise RuntimeError(f"revenue_trend should have 7 entries for days=7, got {len(trend)}")
    for item in trend:
        if "date" not in item or "revenue" not in item:
            raise RuntimeError("revenue_trend item missing date/revenue")

    # G4b：days=30 时 trend 数组长度应为 30（Admin 切换 7/30 日）
    stats30 = run_test(
        host, port,
        {"id": "G4b", "cmd": "stats.overview", "token": admin_token, "data": {"days": 30}},
        "stats.overview days=30 trend length",
    )
    trend30 = stats30["data"]["revenue_trend"]
    if len(trend30) != 30:
        raise RuntimeError(f"revenue_trend should have 30 entries for days=30, got {len(trend30)}")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    db = db_path()
    if not db.is_file():
        print(f"DB not found: {db}", file=sys.stderr)
        print("Run: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql")
        return 1

    print(f"Gap tests -> {host}:{port}, db={db}")

    # 前置：登录管理员与三个测试用户
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
    test_pile_update_open_order_on_idle_pile(host, port, admin_token)
    test_pile_delete_busy(host, port, token8003, admin_token)
    test_pile_delete_open_and_history(host, port, admin_token)
    test_admin_settle_duplicate(host, port, admin_token)
    test_station_create_validation(host, port, admin_token)
    test_low_priority(host, port, token, admin_token)

    print("\n========== H. event.push ==========")
    print("SKIP: event.push 需客户端长连接收推送，自动化不测，请 UI 手测。")

    print("\nAll gap tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nGAP TEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
