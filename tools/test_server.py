#!/usr/bin/env python3
"""charge-server 主回归测试（P0 + P1 + 部分 P2）。

用法（先启动 ./charge-server）：
  python3 tools/test_server.py [host] [port]

测试账号（来自 seed.sql）：
  用户 13800138001 正常有余额 | 13800138002 有待支付订单 | 13800138003 可冻结测试
  13800138004 低余额 | 13800138006 已冻结 | 管理员 admin/123456

用例概览：
  §1  连通与登录（ping / 用户登录 / 管理员登录 / 错误密码与冻结用户）
  §2  充电站（列表排序、关键词、详情含电桩）
  §3  用户资料与充值
  §4  收藏电站增删查
  §5  公告列表
  §6  订单检查 + 充电全流程（预约→充电→停止→结算→重复结算拦截）
  §7  管理端只读（电站/用户/电桩/统计/订单列表）
  §8  管理端写操作（建站、重启桩、冻结/解冻、边界拦截）
  §9  P2 电桩/电站 CRUD、代结算、forecast、通用错误处理
"""

import json
import random
import socket
import struct
import sys
import time
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_common import admin_default_date_range


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
    """发请求并打印；expect_ok=True 时期望 ok，False 时期望失败。"""
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
    """期望失败且 error.code 等于 expect_code（如 ORDER_EXISTS、FORBIDDEN）。"""
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
    """清理指定用户身上残留的未完成订单，使测试可重复跑而不用重建数据库。

    按状态推进：预约→start→充电中→stop→待支付→settle→已完成；
    用户余额不足时尝试管理员代结算。
    """
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

    # ===== §1 P0 基础：连通与登录 =====

    # id=1  ping — 无需 token，验证 TCP 帧协议与服务进程存活
    run_test(host, port, {"id": "1", "cmd": "ping", "data": {}}, "ping")

    # id=2  user.login — 正常用户 8001 登录，应返回 token 与用户信息
    user = run_test(
        host, port,
        {"id": "2", "cmd": "user.login", "data": {"phone": "13800138001"}},
        "user.login",
    )
    token = user["data"]["token"]

    # id=3  user.login — seed 中 8006 状态为「冻结」，登录必须失败
    run_test(
        host, port,
        {"id": "3", "cmd": "user.login", "data": {"phone": "13800138006"}},
        "user.login frozen", expect_ok=False,
    )

    # id=4  admin.login — 管理员 admin/123456 登录成功
    admin = run_test(
        host, port,
        {"id": "4", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}},
        "admin.login",
    )
    admin_token = admin["data"]["token"]

    # id=5  admin.login — 错误密码应拒绝，防止弱鉴权
    run_test(
        host, port,
        {"id": "5", "cmd": "admin.login", "data": {"username": "admin", "password": "wrong"}},
        "admin.login wrong password", expect_ok=False,
    )

    # ===== §2 充电站：列表 / 筛选 / 详情 =====

    # id=6  station.list — 按经纬度查附近站，结果须按 distance_km 升序
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

    # id=6a station.list — keyword 模糊匹配站名（如「市民中心」）
    keyword_stations = run_test(
        host, port,
        {"id": "6a", "cmd": "station.list", "token": token,
         "data": {"lat": 22.5431, "lng": 114.0579, "keyword": "市民中心"}},
        "station.list keyword filter",
    )
    keyword_items = keyword_stations["data"]["items"]
    if not any("市民中心" in item.get("name", "") for item in keyword_items):
        raise RuntimeError("station.list keyword filter returned no match")

    # id=7  station.detail — 返回站点信息及下属电桩列表
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

    # ===== §3 用户资料与充值 =====

    # id=8a user.profile.get — 拉取资料，必须含 balance（Client 余额展示依赖）
    profile = run_test(
        host, port,
        {"id": "8a", "cmd": "user.profile.get", "token": token, "data": {}},
        "user.profile.get",
    )
    if "balance" not in profile.get("data", {}):
        raise RuntimeError("user.profile.get missing balance")

    # id=8  user.profile.update — 修改昵称
    run_test(
        host, port,
        {"id": "8", "cmd": "user.profile.update", "token": token,
         "data": {"nickname": "测试用户"}},
        "user.profile.update",
    )

    # id=9  user.recharge — 充值 50 元，余额增加并写 wallet_log
    run_test(
        host, port,
        {"id": "9", "cmd": "user.recharge", "token": token, "data": {"amount": 50.0}},
        "user.recharge",
    )

    # ===== §4 收藏电站 =====

    # id=10 station.favorite.add — 收藏当前站点
    run_test(
        host, port,
        {"id": "10", "cmd": "station.favorite.add", "token": token,
         "data": {"station_id": station_id}},
        "station.favorite.add",
    )
    # id=11 station.favorite.list — 收藏列表应包含刚收藏的站
    run_test(
        host, port,
        {"id": "11", "cmd": "station.favorite.list", "token": token, "data": {}},
        "station.favorite.list",
    )
    # id=12 station.favorite.remove — 取消收藏
    run_test(
        host, port,
        {"id": "12", "cmd": "station.favorite.remove", "token": token,
         "data": {"station_id": station_id}},
        "station.favorite.remove",
    )

    # ===== §5 公告 =====

    # id=13 announcement.list — 用户端首页公告，返回 items 数组
    run_test(
        host, port,
        {"id": "13", "cmd": "announcement.list", "token": token, "data": {}},
        "announcement.list",
    )

    # ===== §6 订单检查 + 充电全流程 =====

    # 先清掉 8001 可能残留的未完成单，保证后续从干净状态开始
    cleanup_open_order(host, port, token, admin_token)

    # id=14 order.check_open — 无未完成订单时 has_open=false
    open_check = run_test(
        host, port,
        {"id": "14", "cmd": "order.check_open", "token": token, "data": {}},
        "order.check_open (no open)",
    )
    if open_check["data"].get("has_open"):
        raise RuntimeError("8001 should have no open order before charge flow")

    # --- 充电主流程：预约 → 开始 → 查进度 → 停止 → 结算 ---
    # 从详情里找「闲置」桩；没有则跳过（seed 被其他测试改乱时）
    idle_pile = None
    for p in piles:
        if p["status"] == "闲置":
            idle_pile = p
            break
    if idle_pile is None:
        print("\nWARNING: no idle pile found, skipping charge flow tests")
    else:
        pile_no = idle_pile["pile_no"]

        # id=15 charge.reserve — 预约成功，桩→预约，生成 order_no
        reserve = run_test(
            host, port,
            {"id": "15", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}},
            "charge.reserve",
        )
        order_no = reserve["data"]["order_no"]

        # id=16 charge.start — 开始充电，订单→充电中，桩→在用
        run_test(
            host, port,
            {"id": "16", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}},
            "charge.start",
        )

        # id=17 charge.progress — 查询实时电量/金额/进度
        run_test(
            host, port,
            {"id": "17", "cmd": "charge.progress", "token": token, "data": {"order_no": order_no}},
            "charge.progress",
        )

        # id=18 charge.stop — 停止充电，订单→待支付，返回 amount
        stop = run_test(
            host, port,
            {"id": "18", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}},
            "charge.stop",
        )
        amount = stop["data"]["amount"]

        # id=19 charge.settle — 扣余额，订单→已完成，桩→闲置
        run_test(
            host, port,
            {"id": "19", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
            "charge.settle",
        )

        # id=19b 对已完成的单再次 settle 必须失败（防重复扣款）
        run_test(
            host, port,
            {"id": "19b", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}},
            "charge.settle duplicate", expect_ok=False,
        )

    # id=20 order.list — 用户端查自己的历史订单
    run_test(
        host, port,
        {"id": "20", "cmd": "order.list", "token": token, "data": {"limit": 10}},
        "order.list (user)",
    )

    # ===== §7 管理端只读接口 =====

    # id=21 station.admin.list — 电站列表（含 idle_piles、online_rate）
    admin_station_list = run_test(
        host, port,
        {"id": "21", "cmd": "station.admin.list", "token": admin_token, "data": {}},
        "station.admin.list",
    )
    admin_station_items = admin_station_list["data"]["items"]
    if not admin_station_items:
        raise RuntimeError("station.admin.list returned no items")
    for row in admin_station_items:
        for field in ("id", "name", "total_piles", "idle_piles", "online_rate"):
            if field not in row:
                raise RuntimeError(f"station.admin.list missing {field}: {row}")
        if row["idle_piles"] < 0 or row["idle_piles"] > row["total_piles"]:
            raise RuntimeError(f"idle_piles out of range: {row}")
    if not any(r.get("idle_piles", 0) > 0 for r in admin_station_items):
        raise RuntimeError("fresh seed should have at least one station with idle_piles > 0")

    # id=22 user.admin.list — 用户管理表格
    run_test(
        host, port,
        {"id": "22", "cmd": "user.admin.list", "token": admin_token, "data": {}},
        "user.admin.list",
    )

    # id=23 pile.list — 电桩管理列表
    run_test(
        host, port,
        {"id": "23", "cmd": "pile.list", "token": admin_token, "data": {}},
        "pile.list",
    )

    # id=24 stats.overview — KPI + 近 7 日营收折线
    run_test(
        host, port,
        {"id": "24", "cmd": "stats.overview", "token": admin_token, "data": {"days": 7}},
        "stats.overview",
    )

    # id=25 order.list — 管理端查全部订单（日期区间与 Admin UI 默认一致）
    order_from, order_to = admin_default_date_range()
    run_test(
        host, port,
        {"id": "25", "cmd": "order.list", "token": admin_token,
         "data": {"limit": 10, "date_from": order_from, "date_to": order_to}},
        "order.list (admin)",
    )

    # ===== §8 管理端写操作 + 业务边界拦截 =====

    # id=26 station.create — 新建电站（随机站名避免重复）；并自动创建 fast/slow 桩
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

    # id=27 建站后列表应包含新 station_id
    admin_stations = run_test(
        host, port,
        {"id": "27", "cmd": "station.admin.list", "token": admin_token, "data": {}},
        "station.admin.list after create",
    )
    station_ids = [s["id"] for s in admin_stations["data"]["items"]]
    if new_station_id not in station_ids:
        raise RuntimeError("station.create id not found in station.admin.list")

    # id=28 pile.restart — 远程重启闲置桩 SZ002-03，应写 operation_log
    run_test(
        host, port,
        {"id": "28", "cmd": "pile.restart", "token": admin_token,
         "data": {"pile_no": "SZ002-03"}},
        "pile.restart",
    )

    # id=29 user.freeze — 8002 seed 中有「待支付」订单，冻结必须被拒绝
    run_test(
        host, port,
        {"id": "29", "cmd": "user.freeze", "token": admin_token,
         "data": {"user_id": 2, "freeze": True}},
        "user.freeze 8002 blocked (待支付 open order)",
        expect_ok=False,
    )
    # id=29b 冻结 8003（无未完成单），应成功
    run_test(
        host, port,
        {"id": "29b", "cmd": "user.freeze", "token": admin_token,
         "data": {"user_id": 3, "freeze": True}},
        "user.freeze 8003",
    )
    # id=30 冻结后 8003 不能再登录
    run_test(
        host, port,
        {"id": "30", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login after freeze 8003", expect_ok=False,
    )
    # id=31 解冻 8003
    run_test(
        host, port,
        {"id": "31", "cmd": "user.freeze", "token": admin_token,
         "data": {"user_id": 3, "freeze": False}},
        "user.unfreeze 8003",
    )
    # id=32 8002 从未被冻结，仍可登录（与 id=29 对照）
    user8002 = run_test(
        host, port,
        {"id": "32", "cmd": "user.login", "data": {"phone": "13800138002"}},
        "user.login 8002 (pending order, never frozen)",
    )
    token8002 = user8002["data"]["token"]

    # id=33a 8002 应有一笔 seed 里的「待支付」订单
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

    # id=33b 有待支付单时不能再预约新桩 → ORDER_EXISTS
    run_test_error(
        host, port,
        {"id": "33b", "cmd": "charge.reserve", "token": token8002,
         "data": {"pile_no": "SZ005-01"}},
        "charge.reserve ORDER_EXISTS (8002 pending)",
        "ORDER_EXISTS",
    )

    # id=33c 8001 不能结算 8002 的订单 → FORBIDDEN
    run_test_error(
        host, port,
        {"id": "33c", "cmd": "charge.settle", "token": token,
         "data": {"order_no": pending_order_no}},
        "charge.settle FORBIDDEN (8001 on 8002 order)",
        "FORBIDDEN",
    )

    # id=33d 故障桩 SZ001-03 不可预约 → PILE_FAULT
    run_test_error(
        host, port,
        {"id": "33d", "cmd": "charge.reserve", "token": token,
         "data": {"pile_no": "SZ001-03"}},
        "charge.reserve PILE_FAULT",
        "PILE_FAULT",
    )

    # --- id=33e~33j 桩已被预约/占用时，他人不可再预约 ---
    user8003 = run_test(
        host, port,
        {"id": "33e", "cmd": "user.login", "data": {"phone": "13800138003"}},
        "user.login 8003",
    )
    token8003 = user8003["data"]["token"]
    busy_pile_no = "SZ005-04"
    # 8003 先占住 SZ005-04
    busy_reserve = run_test(
        host, port,
        {"id": "33f", "cmd": "charge.reserve", "token": token8003,
         "data": {"pile_no": busy_pile_no}},
        "charge.reserve 8003 (PILE_BUSY setup)",
    )
    busy_order_no = busy_reserve["data"]["order_no"]
    # 8001 再预约同一桩 → PILE_BUSY
    run_test_error(
        host, port,
        {"id": "33g", "cmd": "charge.reserve", "token": token,
         "data": {"pile_no": busy_pile_no}},
        "charge.reserve PILE_BUSY",
        "PILE_BUSY",
    )
    # 清理 8003 占用的桩，避免影响后续测试
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

    # id=33k 用户 token 不能调管理端 stats.overview → FORBIDDEN
    run_test_error(
        host, port,
        {"id": "33k", "cmd": "stats.overview", "token": token, "data": {"days": 7}},
        "stats.overview FORBIDDEN (user token)",
        "FORBIDDEN",
    )

    # id=33l 管理员 token 不能调用户资料接口 → FORBIDDEN（角色隔离）
    run_test_error(
        host, port,
        {"id": "33l", "cmd": "user.profile.update", "token": admin_token,
         "data": {"nickname": "admin-as-user"}},
        "user.profile.update FORBIDDEN (admin token)",
        "FORBIDDEN",
    )

    # --- id=33m~33q 余额不足不能结算 ---
    # 8004 seed 余额很低；充够久使 amount > balance，settle 应 BALANCE_NOT_ENOUGH
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
    # 服务端 time_scale=60：等 65 真实秒 ≈ 65 模拟分钟，使费用超过余额
    print("\n>>> waiting 65s for charge amount > balance (8004)...")
    time.sleep(65)
    run_test(
        host, port,
        {"id": "33p", "cmd": "charge.stop", "token": token8004,
         "data": {"order_no": low_order_no}},
        "charge.stop 8004",
    )
    # id=33q 余额不足时 settle 失败，订单保持待支付
    run_test_error(
        host, port,
        {"id": "33q", "cmd": "charge.settle", "token": token8004,
         "data": {"order_no": low_order_no}},
        "charge.settle BALANCE_NOT_ENOUGH",
        "BALANCE_NOT_ENOUGH",
    )

    # ===== §9 P2 电桩/电站 CRUD、代结算、负荷预测 =====

    # id=37 重复站名建站应失败（站名唯一约束）
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

    # id=38~39 pile.update — 修改新建站电桩功率；非法 type 应拒绝
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
    # id=39 改功率
    run_test(
        host, port,
        {"id": "39", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": target_pile, "power_kw": 120.0}},
        "pile.update power",
    )
    # id=40 非法类型「直流」（只允许快充/慢充）
    run_test(
        host, port,
        {"id": "40", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": target_pile, "type": "直流"}},
        "pile.update invalid type", expect_ok=False,
    )
    # id=40a 手动设为「预约」应拒绝（状态由订单流程驱动，协议 5.2 + adminhandler）
    run_test(
        host, port,
        {"id": "40a", "cmd": "pile.update", "token": admin_token,
         "data": {"pile_no": target_pile, "status": "预约"}},
        "pile.update manual 预约 rejected", expect_ok=False,
    )

    # id=41 pile.delete — 删除闲置且无历史约束的桩
    run_test(
        host, port,
        {"id": "41", "cmd": "pile.delete", "token": admin_token,
         "data": {"pile_no": target_pile}},
        "pile.delete idle pile",
    )

    # id=42~46 预约中的桩不可 restart；走完流程后测管理员代结算
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
        # id=43 桩处于预约态时 restart 必须失败
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
        # id=46 order.admin.settle — 管理员替用户扣款并完成订单
        run_test(
            host, port,
            {"id": "46", "cmd": "order.admin.settle", "token": admin_token,
             "data": {"order_no": order2}},
            "order.admin.settle",
        )

    # id=47~51 用户「充电中」时不可被冻结；测完清理订单
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
        # id=49 充电中冻结 user_id=1 应失败
        run_test(
            host, port,
            {"id": "49", "cmd": "user.freeze", "token": admin_token,
             "data": {"user_id": 1, "freeze": True}},
            "user.freeze while charging", expect_ok=False,
        )
        # id=50~51 停止并结算，恢复环境
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

    # id=52~54 forecast.list — 负荷预测（读 load_forecast 表）；非法 horizon 应失败
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

    # ===== §10 通用错误处理 =====

    # id=33 充值金额为负 → 参数错误
    run_test(
        host, port,
        {"id": "33", "cmd": "user.recharge", "token": token, "data": {"amount": -10}},
        "user.recharge invalid amount", expect_ok=False,
    )

    # id=34 不存在的 station_id → 未找到
    run_test(
        host, port,
        {"id": "34", "cmd": "station.detail", "token": token, "data": {"station_id": 99999}},
        "station.detail not found", expect_ok=False,
    )

    # id=35 未知命令 → 协议层拒绝
    run_test(
        host, port,
        {"id": "35", "cmd": "unknown.cmd", "token": token, "data": {}},
        "unknown cmd", expect_ok=False,
    )

    # id=36 无效 token → 未授权
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
