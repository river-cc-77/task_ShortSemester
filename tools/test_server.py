#!/usr/bin/env python3
"""Test charge-server P0 + P1 commands: user, station, order, charge, admin, stats."""

import json
import random
import socket
import struct
import sys
import time
from typing import Optional


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


def cleanup_open_order(host: str, port: int, token: str, admin_token: Optional[str] = None) -> None:
    """Finish or settle leftover orders so tests can repeat without rebuilding db."""
    resp = send_request(
        host, port,
        {"id": "cleanup0", "cmd": "order.check_open", "token": token, "data": {}},
    )
    if not resp.get("ok") or not resp.get("data", {}).get("has_open"):
        return

    order = resp["data"]["order"]
    order_no = order["order_no"]
    status = order["status"]
    print(f"\n>>> cleanup open order {order_no} (status={status})")

    if status == "预约":
        run_test(
            host, port,
            {"id": "cleanup1", "cmd": "charge.start", "token": token,
             "data": {"order_no": order_no}},
            "cleanup charge.start",
        )
        status = "充电中"

    if status == "充电中":
        run_test(
            host, port,
            {"id": "cleanup2", "cmd": "charge.stop", "token": token,
             "data": {"order_no": order_no}},
            "cleanup charge.stop",
        )
        status = "待支付"

    if status == "待支付":
        settle = send_request(
            host, port,
            {"id": "cleanup3", "cmd": "charge.settle", "token": token,
             "data": {"order_no": order_no}},
        )
        if not settle.get("ok") and admin_token:
            run_test(
                host, port,
                {"id": "cleanup4", "cmd": "order.admin.settle", "token": admin_token,
                 "data": {"order_no": order_no}},
                "cleanup order.admin.settle",
            )
        elif not settle.get("ok"):
            raise RuntimeError(f"cleanup settle failed: {settle}")


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

    keyword_stations = run_test(
        host, port,
        {"id": "6a", "cmd": "station.list", "token": token,
         "data": {"lat": 22.5431, "lng": 114.0579, "keyword": "市民中心"}},
        "station.list keyword filter",
    )
    keyword_items = keyword_stations["data"]["items"]
    if not any("市民中心" in item.get("name", "") for item in keyword_items):
        raise RuntimeError("station.list keyword filter returned no match")

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
    cleanup_open_order(host, port, token, admin_token)

    open_check = run_test(
        host, port,
        {"id": "14", "cmd": "order.check_open", "token": token, "data": {}},
        "order.check_open (no open)",
    )
    if open_check["data"].get("has_open"):
        raise RuntimeError("8001 should have no open order before charge flow")

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

        # 重复结算应提示"订单已结算"（需求 NO.15/18）
        run_test(
            host, port,
            {"id": "19b", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
            "charge.settle duplicate", expect_ok=False,
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

    # ===== 管理端写操作（此前未覆盖） =====
    # 站名带随机后缀，保证测试可重复运行（站名唯一检查）
    test_station_name = "自动化测试站" + str(random.randint(100, 999))
    created = run_test(
        host, port,
        {"id": "26", "cmd": "station.create", "token": admin_token,
         "data": {
             "name": test_station_name,
             "address": "深圳市测试路 1 号",
             "lat": 22.5,
             "lng": 114.0,
             "price": 1.2,
             "fast_count": 2,
             "slow_count": 1,
         }},
        "station.create",
    )
    new_station_id = created["data"]["station_id"]

    admin_stations = run_test(
        host, port,
        {"id": "27", "cmd": "station.admin.list", "token": admin_token, "data": {}},
        "station.admin.list after create",
    )
    station_ids = [s["id"] for s in admin_stations["data"]["items"]]
    if new_station_id not in station_ids:
        raise RuntimeError("station.create id not found in station.admin.list")

    run_test(
        host, port,
        {"id": "28", "cmd": "pile.restart", "token": admin_token,
         "data": {"pile_no": "SZ001-03"}},
        "pile.restart",
    )

    run_test(
        host, port,
        {"id": "29", "cmd": "user.freeze", "token": admin_token,
         "data": {"user_id": 2, "freeze": True}},
        "user.freeze",
    )
    run_test(
        host, port,
        {"id": "30", "cmd": "user.login", "data": {"phone": "13800138002"}},
        "user.login after freeze", expect_ok=False,
    )
    run_test(
        host, port,
        {"id": "31", "cmd": "user.freeze", "token": admin_token,
         "data": {"user_id": 2, "freeze": False}},
        "user.unfreeze",
    )
    user8002 = run_test(
        host, port,
        {"id": "32", "cmd": "user.login", "data": {"phone": "13800138002"}},
        "user.login 8002 after unfreeze (pending order)",
    )
    token8002 = user8002["data"]["token"]

    open_order = run_test(
        host, port,
        {"id": "33a", "cmd": "order.check_open", "token": token8002, "data": {}},
        "order.check_open 8002 pending",
    )
    if not open_order["data"].get("has_open"):
        raise RuntimeError("8002 should have open order")
    if open_order["data"]["order"]["status"] != "待支付":
        raise RuntimeError("8002 open order should be 待支付")
    pending_order_no = open_order["data"]["order"]["order_no"]

    run_test_error(
        host, port,
        {"id": "33b", "cmd": "charge.reserve", "token": token8002,
         "data": {"pile_no": "SZ005-01"}},
        "charge.reserve ORDER_EXISTS (8002 pending)",
        "ORDER_EXISTS",
    )

    run_test_error(
        host, port,
        {"id": "33c", "cmd": "charge.settle", "token": token,
         "data": {"order_no": pending_order_no}},
        "charge.settle FORBIDDEN (8001 on 8002 order)",
        "FORBIDDEN",
    )

    run_test_error(
        host, port,
        {"id": "33d", "cmd": "charge.reserve", "token": token,
         "data": {"pile_no": "SZ001-03"}},
        "charge.reserve PILE_FAULT",
        "PILE_FAULT",
    )

    user8003 = run_test(
        host, port,
        {"id": "33e", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003",
    )
    token8003 = user8003["data"]["token"]
    busy_pile_no = "SZ005-04"
    busy_reserve = run_test(
        host, port,
        {"id": "33f", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": busy_pile_no}},
        "charge.reserve 8003 (PILE_BUSY setup)",
    )
    busy_order_no = busy_reserve["data"]["order_no"]
    run_test_error(
        host, port,
        {"id": "33g", "cmd": "charge.reserve", "token": token,
         "data": {"pile_no": busy_pile_no}},
        "charge.reserve PILE_BUSY",
        "PILE_BUSY",
    )
    run_test(
        host, port,
        {"id": "33h", "cmd": "charge.start", "token": token8003,
         "data": {"order_no": busy_order_no}},
        "charge.start 8003 cleanup",
    )
    run_test(
        host, port,
        {"id": "33i", "cmd": "charge.stop", "token": token8003,
         "data": {"order_no": busy_order_no}},
        "charge.stop 8003 cleanup",
    )
    run_test(
        host, port,
        {"id": "33j", "cmd": "charge.settle", "token": token8003,
         "data": {"order_no": busy_order_no}},
        "charge.settle 8003 cleanup",
    )

    run_test_error(
        host, port,
        {"id": "33k", "cmd": "stats.overview", "token": token, "data": {"days": 7}},
        "stats.overview FORBIDDEN (user token)",
        "FORBIDDEN",
    )

    run_test_error(
        host, port,
        {"id": "33l", "cmd": "user.profile.update", "token": admin_token,
         "data": {"nickname": "admin-as-user"}},
        "user.profile.update FORBIDDEN (admin token)",
        "FORBIDDEN",
    )

    user8004 = run_test(
        host, port,
        {"id": "33m", "cmd": "user.login", "data": {"phone": "13800138004"}},
        "user.login 8004 (low balance)",
    )
    token8004 = user8004["data"]["token"]
    low_balance_pile = "SZ002-01"
    low_reserve = run_test(
        host, port,
        {"id": "33n", "cmd": "charge.reserve", "token": token8004,
         "data": {"pile_no": low_balance_pile}},
        "charge.reserve 8004 (BALANCE_NOT_ENOUGH setup)",
    )
    low_order_no = low_reserve["data"]["order_no"]
    run_test(
        host, port,
        {"id": "33o", "cmd": "charge.start", "token": token8004,
         "data": {"order_no": low_order_no}},
        "charge.start 8004",
    )
    print("\n>>> waiting 65s for charge amount > balance (8004)...")
    time.sleep(65)
    run_test(
        host, port,
        {"id": "33p", "cmd": "charge.stop", "token": token8004,
         "data": {"order_no": low_order_no}},
        "charge.stop 8004",
    )
    run_test_error(
        host, port,
        {"id": "33q", "cmd": "charge.settle", "token": token8004,
         "data": {"order_no": low_order_no}},
        "charge.settle BALANCE_NOT_ENOUGH",
        "BALANCE_NOT_ENOUGH",
    )

    # ===== 新命令测试：P2 补齐 + 边界（协议 4.18 / 5.2 / 5.3） =====
    # 1) 重复站名 → "站名已存在"（需求 NO.29）
    run_test(
        host, port,
        {"id": "37", "cmd": "station.create", "token": admin_token,
         "data": {
             "name": test_station_name,
             "address": "深圳市测试路 2 号",
             "lat": 22.6,
             "lng": 114.1,
             "price": 1.3,
             "fast_count": 1,
             "slow_count": 1,
         }},
        "station.create duplicate name", expect_ok=False,
    )

    # 2) pile.update：修改新建站电桩（协议 5.2 P2）
    pile_list2 = run_test(
        host, port,
        {"id": "38", "cmd": "pile.list", "token": admin_token,
         "data": {"keyword": test_station_name}},
        "pile.list (new station)",
    )
    new_piles = pile_list2["data"]["items"]
    if len(new_piles) < 1:
        raise RuntimeError("new station has no piles")
    target_pile = new_piles[0]["pile_no"]
    run_test(
        host, port,
        {"id": "39", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": target_pile, "power_kw": 120.0}},
        "pile.update power",
    )
    run_test(
        host, port,
        {"id": "40", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": target_pile, "type": "直流"}},
        "pile.update invalid type", expect_ok=False,
    )

    # 3) pile.delete：删除新建站的空闲桩（协议 5.2 P2）
    run_test(
        host, port,
        {"id": "41", "cmd": "pile.delete", "token": admin_token,
         "data": {"pile_no": target_pile}},
        "pile.delete idle pile",
    )

    # 4) pile.restart 使用中拦截（需求 NO.26）：预约中的桩不可重启
    idle_pile2 = None
    for p in piles:
        if p["status"] == "闲置":
            idle_pile2 = p
            break
    if idle_pile2 is not None:
        reserve2 = run_test(
            host, port,
            {"id": "42", "cmd": "charge.reserve", "token": token,
             "data": {"pile_no": idle_pile2["pile_no"]}},
            "charge.reserve (for restart check)",
        )
        order2 = reserve2["data"]["order_no"]
        run_test(
            host, port,
            {"id": "43", "cmd": "pile.restart", "token": admin_token,
             "data": {"pile_no": idle_pile2["pile_no"]}},
            "pile.restart busy pile", expect_ok=False,
        )
        run_test(
            host, port,
            {"id": "44", "cmd": "charge.start", "token": token, "data": {"order_no": order2}},
            "charge.start (for admin settle)",
        )
        run_test(
            host, port,
            {"id": "45", "cmd": "charge.stop", "token": token, "data": {"order_no": order2}},
            "charge.stop (for admin settle)",
        )
        # 5) order.admin.settle：管理端代结算（协议 4.18 P1）
        run_test(
            host, port,
            {"id": "46", "cmd": "order.admin.settle", "token": admin_token,
             "data": {"order_no": order2}},
            "order.admin.settle",
        )

    # 6) user.freeze 充电中拦截（需求 NO.18/30）
    idle_pile3 = None
    for p in piles:
        if p["status"] == "闲置":
            idle_pile3 = p
            break
    if idle_pile3 is not None:
        reserve3 = run_test(
            host, port,
            {"id": "47", "cmd": "charge.reserve", "token": token,
             "data": {"pile_no": idle_pile3["pile_no"]}},
            "charge.reserve (for freeze check)",
        )
        order3 = reserve3["data"]["order_no"]
        run_test(
            host, port,
            {"id": "48", "cmd": "charge.start", "token": token, "data": {"order_no": order3}},
            "charge.start (for freeze check)",
        )
        run_test(
            host, port,
            {"id": "49", "cmd": "user.freeze", "token": admin_token,
             "data": {"user_id": 1, "freeze": True}},
            "user.freeze while charging", expect_ok=False,
        )
        run_test(
            host, port,
            {"id": "50", "cmd": "charge.stop", "token": token, "data": {"order_no": order3}},
            "charge.stop cleanup",
        )
        run_test(
            host, port,
            {"id": "51", "cmd": "charge.settle", "token": token, "data": {"order_no": order3}},
            "charge.settle cleanup",
        )

    # 7) forecast.list：负荷预测（协议 5.3 P2）
    run_test(
        host, port,
        {"id": "52", "cmd": "forecast.list", "token": token, "data": {"horizon": "1h"}},
        "forecast.list (user)",
    )
    run_test(
        host, port,
        {"id": "53", "cmd": "forecast.list", "token": admin_token,
         "data": {"horizon": "6h", "station_id": station_id}},
        "forecast.list (admin)",
    )
    run_test(
        host, port,
        {"id": "54", "cmd": "forecast.list", "token": token, "data": {"horizon": "2h"}},
        "forecast.list invalid horizon", expect_ok=False,
    )

    # ===== 错误处理测试 =====
    run_test(
        host, port,
        {"id": "33", "cmd": "user.recharge", "token": token, "data": {"amount": -10}},
        "user.recharge invalid amount", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "34", "cmd": "station.detail", "token": token, "data": {"station_id": 99999}},
        "station.detail not found", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "35", "cmd": "unknown.cmd", "token": token, "data": {}},
        "unknown cmd", expect_ok=False,
    )

    run_test(
        host, port,
        {"id": "36", "cmd": "user.profile.update", "token": "invalid_token", "data": {}},
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
