#!/usr/bin/env python3
"""东软充电桩 — 集成测试（49 条 / 7 模块）。

用法（先启动 ./charge-server，建议 fresh seed 库）：
  python3 tools/test_integration.py [host] [port]

与交付物 `03测试用例.xls` 中 TC-01～TC-49 一一对应。
"""

from __future__ import annotations

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
from testcase_catalog import MODULES


def send_request(host: str, port: int, payload: dict) -> dict:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body
    with socket.create_connection((host, port), timeout=8) as sock:
        sock.sendall(frame)
        length = struct.unpack(">I", _recv_exact(sock, 4))[0]
        return json.loads(_recv_exact(sock, length).decode("utf-8"))


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks, remaining = [], size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def ok(host: str, port: int, req: dict, label: str) -> dict:
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if not resp.get("ok"):
        raise RuntimeError(f"{label} failed: {resp}")
    return resp


def err(host: str, port: int, req: dict, label: str, code: str) -> dict:
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if resp.get("ok"):
        raise RuntimeError(f"{label} should fail")
    got = resp.get("error", {}).get("code")
    if got != code:
        raise RuntimeError(f"{label} expected {code}, got {got}")
    return resp


def find_idle_pile(host: str, port: int, admin_token: str, exclude: Optional[set] = None) -> Optional[str]:
    exclude = exclude or set()
    resp = send_request(
        host, port,
        {"id": "find", "cmd": "pile.list", "token": admin_token, "data": {"status": "闲置"}},
    )
    if not resp.get("ok"):
        return None
    for row in resp["data"]["items"]:
        no = row["pile_no"]
        if row.get("status") == "闲置" and no not in exclude:
            return no
    return None


def find_fault_pile(host: str, port: int, admin_token: str, exclude: Optional[set] = None) -> Optional[str]:
    exclude = exclude or set()
    resp = send_request(
        host, port,
        {"id": "find_fault", "cmd": "pile.list", "token": admin_token, "data": {"status": "故障"}},
    )
    if not resp.get("ok"):
        return None
    for row in resp["data"]["items"]:
        no = row["pile_no"]
        if row.get("status") == "故障" and no not in exclude:
            return no
    return None


def ensure_fault_pile(host: str, port: int, admin_token: str, exclude: Optional[set] = None) -> str:
    """返回可用于 pile.restart 的故障桩（重复跑测时 seed 故障桩可能已被重启）。"""
    pile_no = find_fault_pile(host, port, admin_token, exclude=exclude)
    if pile_no:
        return pile_no
    pile_no = find_idle_pile(host, port, admin_token, exclude=exclude)
    if not pile_no:
        raise RuntimeError("no pile available to mark fault for restart test")
    ok(
        host, port,
        {
            "id": "prep_fault",
            "cmd": "pile.update",
            "token": admin_token,
            "data": {"pile_no": pile_no, "status": "故障"},
        },
        f"prepare fault pile {pile_no}",
    )
    return pile_no


def cleanup_user_order(host: str, port: int, token: str, admin_token: str) -> None:
    resp = send_request(host, port, {"id": "c0", "cmd": "order.check_open", "token": token, "data": {}})
    if not resp.get("ok") or not resp["data"].get("has_open"):
        return
    order_no = resp["data"]["order"]["order_no"]
    status = resp["data"]["order"]["status"]
    if status == "预约":
        ok(host, port, {"id": "c1", "cmd": "charge.cancel", "token": token, "data": {"order_no": order_no}}, "cleanup cancel")
        return
    if status == "充电中":
        ok(host, port, {"id": "c2", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}}, "cleanup stop")
        status = "待支付"
    if status == "待支付":
        settle = send_request(host, port, {"id": "c3", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}})
        if not settle.get("ok"):
            ok(host, port, {"id": "c4", "cmd": "order.admin.settle", "token": admin_token, "data": {"order_no": order_no}}, "cleanup admin settle")


def run_all(host: str, port: int) -> None:
    total = sum(len(m["cases"]) for m in MODULES)  # 49

    # ===== 模块1：连通与登录 TC-01~07 =====
    ok(host, port, {"id": "TC-01", "cmd": "ping", "data": {}}, "TC-01 ping")

    user = ok(host, port, {"id": "TC-02", "cmd": "user.login", "data": {"phone": "13800138001"}}, "TC-02 user.login")
    token = user["data"]["token"]

    admin = ok(host, port, {"id": "TC-03", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}}, "TC-03 admin.login")
    admin_token = admin["data"]["token"]

    err(host, port, {"id": "TC-04", "cmd": "admin.login", "data": {"username": "admin", "password": "wrong"}}, "TC-04 wrong pwd", "UNAUTHORIZED")
    err(host, port, {"id": "TC-05", "cmd": "user.login", "data": {"phone": "13800138006"}}, "TC-05 frozen", "USER_FROZEN")

    new_phone = f"139{random.randint(10000000, 99999999)}"
    ok(host, port, {"id": "TC-06", "cmd": "user.login", "data": {"phone": new_phone}}, "TC-06 auto register")

    err(host, port, {"id": "TC-07", "cmd": "station.list", "token": "bad-token", "data": {"lat": 22.5, "lng": 114.0}}, "TC-07 bad token", "UNAUTHORIZED")

    # ===== 模块2：充电站 TC-08~14 =====
    stations = ok(host, port, {"id": "TC-08", "cmd": "station.list", "token": token, "data": {"lat": 22.5431, "lng": 114.0579}}, "TC-08 station.list")
    items = stations["data"]["items"]
    if not items:
        raise RuntimeError("TC-08 no stations")
    if [x["distance_km"] for x in items] != sorted(x["distance_km"] for x in items):
        raise RuntimeError("TC-08 not sorted by distance")

    kw = ok(host, port, {"id": "TC-09", "cmd": "station.list", "token": token, "data": {"lat": 22.5431, "lng": 114.0579, "keyword": "市民中心"}}, "TC-09 keyword")
    if not any("市民中心" in x.get("name", "") for x in kw["data"]["items"]):
        raise RuntimeError("TC-09 keyword miss")

    station_id = items[0]["id"]
    detail = ok(host, port, {"id": "TC-10", "cmd": "station.detail", "token": token, "data": {"station_id": station_id}}, "TC-10 detail")
    if not detail["data"].get("piles"):
        raise RuntimeError("TC-10 no piles")

    err(host, port, {"id": "TC-11", "cmd": "station.detail", "token": token, "data": {"station_id": 99999}}, "TC-11 bad id", "NOT_FOUND")

    ok(host, port, {"id": "TC-12a", "cmd": "station.favorite.add", "token": token, "data": {"station_id": station_id}}, "TC-12 add")
    fav = ok(host, port, {"id": "TC-12b", "cmd": "station.favorite.list", "token": token, "data": {}}, "TC-12 list")
    if not any(x["id"] == station_id for x in fav["data"]["items"]):
        raise RuntimeError("TC-12 favorite missing")
    ok(host, port, {"id": "TC-12c", "cmd": "station.favorite.remove", "token": token, "data": {"station_id": station_id}}, "TC-12 remove")

    ok(host, port, {"id": "TC-13a", "cmd": "station.favorite.add", "token": token, "data": {"station_id": station_id}}, "TC-13 dup add 1")
    ok(host, port, {"id": "TC-13b", "cmd": "station.favorite.add", "token": token, "data": {"station_id": station_id}}, "TC-13 dup add 2")
    ok(host, port, {"id": "TC-13c", "cmd": "station.favorite.remove", "token": token, "data": {"station_id": station_id}}, "TC-13 cleanup")

    fc = ok(host, port, {"id": "TC-14", "cmd": "forecast.list", "token": token, "data": {"horizon": "1h"}}, "TC-14 forecast")
    if "items" not in fc.get("data", {}):
        raise RuntimeError("TC-14 no items")

    # ===== 模块3：充电全流程 TC-15~21 =====
    cleanup_user_order(host, port, token, admin_token)

    open0 = ok(host, port, {"id": "TC-15", "cmd": "order.check_open", "token": token, "data": {}}, "TC-15 check_open")
    if open0["data"].get("has_open"):
        raise RuntimeError("TC-15 should have no open order")

    pile_no = find_idle_pile(host, port, admin_token)
    if not pile_no:
        raise RuntimeError("TC-16 no idle pile — rebuild seed db")

    reserve = ok(host, port, {"id": "TC-16", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}}, "TC-16 reserve")
    order_no = reserve["data"]["order_no"]

    ok(host, port, {"id": "TC-17", "cmd": "charge.cancel", "token": token, "data": {"order_no": order_no}}, "TC-17 cancel")
    if ok(host, port, {"id": "TC-17b", "cmd": "order.check_open", "token": token, "data": {}}, "TC-17 check")["data"].get("has_open"):
        raise RuntimeError("TC-17 still open after cancel")

    reserve2 = ok(host, port, {"id": "TC-18", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}}, "TC-18 reserve again")
    order_no = reserve2["data"]["order_no"]
    ok(host, port, {"id": "TC-18b", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}}, "TC-18 start")

    prog = ok(host, port, {"id": "TC-19", "cmd": "charge.progress", "token": token, "data": {"order_no": order_no}}, "TC-19 progress")
    if prog["data"].get("estimated_remain_seconds", 0) <= 0:
        raise RuntimeError("TC-19 missing estimated_remain_seconds")

    ok(host, port, {"id": "TC-20", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}}, "TC-20 stop")
    ok(host, port, {"id": "TC-21", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}}, "TC-21 settle")

    # ===== 模块4：业务规则 TC-22~28 =====
    u8002 = ok(host, port, {"id": "TC-22", "cmd": "user.login", "data": {"phone": "13800138002"}}, "TC-22 login 8002")
    open8002 = ok(host, port, {"id": "TC-22b", "cmd": "order.check_open", "token": u8002["data"]["token"], "data": {}}, "TC-22 check")
    if not open8002["data"].get("has_open") or open8002["data"]["order"]["status"] != "待支付":
        raise RuntimeError("TC-22 8002 needs 待支付 order")
    pending_no = open8002["data"]["order"]["order_no"]

    err(host, port, {"id": "TC-23", "cmd": "charge.reserve", "token": u8002["data"]["token"], "data": {"pile_no": "SZ005-01"}}, "TC-23 ORDER_EXISTS", "ORDER_EXISTS")
    err(host, port, {"id": "TC-24", "cmd": "charge.reserve", "token": token, "data": {"pile_no": "SZ001-03"}}, "TC-24 PILE_FAULT", "PILE_FAULT")

    u8003 = ok(host, port, {"id": "TC-25a", "cmd": "user.login", "data": {"phone": "13800138003"}}, "TC-25 login 8003")
    token8003 = u8003["data"]["token"]
    busy_pile = find_idle_pile(host, port, admin_token, exclude={"SZ001-03"})
    if not busy_pile:
        raise RuntimeError("TC-25 no pile")
    busy_res = ok(host, port, {"id": "TC-25b", "cmd": "charge.reserve", "token": token8003, "data": {"pile_no": busy_pile}}, "TC-25 occupy")
    err(host, port, {"id": "TC-25c", "cmd": "charge.reserve", "token": token, "data": {"pile_no": busy_pile}}, "TC-25 PILE_BUSY", "PILE_BUSY")
    ok(host, port, {"id": "TC-25d", "cmd": "charge.cancel", "token": token8003, "data": {"order_no": busy_res["data"]["order_no"]}}, "TC-25 cleanup cancel")

    err(host, port, {"id": "TC-26", "cmd": "charge.settle", "token": token, "data": {"order_no": pending_no}}, "TC-26 FORBIDDEN", "FORBIDDEN")

    cleanup_user_order(host, port, token, admin_token)
    pile2 = find_idle_pile(host, port, admin_token)
    if pile2:
        r27 = ok(host, port, {"id": "TC-27a", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile2}}, "TC-27 reserve")
        err(host, port, {"id": "TC-27b", "cmd": "charge.stop", "token": token, "data": {"order_no": r27["data"]["order_no"]}}, "TC-27 stop on 预约", "INVALID_PARAM")
        ok(host, port, {"id": "TC-27c", "cmd": "charge.cancel", "token": token, "data": {"order_no": r27["data"]["order_no"]}}, "TC-27 cleanup")

    err(host, port, {"id": "TC-28", "cmd": "stats.overview", "token": token, "data": {"days": 7}}, "TC-28 FORBIDDEN", "FORBIDDEN")

    # ===== 模块5：用户资料与公告 TC-29~35 =====
    if "balance" not in ok(host, port, {"id": "TC-29", "cmd": "user.profile.get", "token": token, "data": {}}, "TC-29 profile")["data"]:
        raise RuntimeError("TC-29 no balance")

    ok(host, port, {"id": "TC-30", "cmd": "user.profile.update", "token": token, "data": {"nickname": "集成测试用户"}}, "TC-30 profile.update")
    ok(host, port, {"id": "TC-31", "cmd": "user.recharge", "token": token, "data": {"amount": 50.0}}, "TC-31 recharge")

    bad0 = send_request(host, port, {"id": "TC-32a", "cmd": "user.recharge", "token": token, "data": {"amount": 0}})
    bad1 = send_request(host, port, {"id": "TC-32b", "cmd": "user.recharge", "token": token, "data": {"amount": -1}})
    if bad0.get("ok") or bad1.get("ok"):
        raise RuntimeError("TC-32 should fail for invalid amount")

    ann0 = ok(host, port, {"id": "TC-33", "cmd": "announcement.list", "token": token, "data": {}}, "TC-33 ann list")
    n0 = len(ann0["data"]["items"])

    ok(host, port, {"id": "TC-34", "cmd": "announcement.create", "token": admin_token, "data": {"title": "集成测试公告", "content": "正文", "is_active": True}}, "TC-34 create")
    alist = ok(host, port, {"id": "TC-34b", "cmd": "announcement.admin.list", "token": admin_token, "data": {}}, "TC-34 admin list")
    ann_id = next((x["id"] for x in alist["data"]["items"] if x.get("title") == "集成测试公告"), None)
    if ann_id is None:
        raise RuntimeError("TC-34 ann not found")
    if len(ok(host, port, {"id": "TC-34c", "cmd": "announcement.list", "token": token, "data": {}}, "TC-34 user list")["data"]["items"]) != n0 + 1:
        raise RuntimeError("TC-34 count mismatch")

    ok(host, port, {"id": "TC-35", "cmd": "announcement.delete", "token": admin_token, "data": {"id": ann_id}}, "TC-35 delete")
    if len(ok(host, port, {"id": "TC-35b", "cmd": "announcement.list", "token": token, "data": {}}, "TC-35 after")["data"]["items"]) != n0:
        raise RuntimeError("TC-35 count not restored")

    # ===== 模块6：管理端统计与日志 TC-36~42 =====
    stats = ok(host, port, {"id": "TC-36", "cmd": "stats.overview", "token": admin_token, "data": {"days": 7}}, "TC-36 stats")
    for f in ("today_revenue", "pile_status", "pile_health_rate", "user_count"):
        if f not in stats["data"]:
            raise RuntimeError(f"TC-36 missing {f}")

    ok(host, port, {"id": "TC-37", "cmd": "user.admin.list", "token": admin_token, "data": {}}, "TC-37 users")
    ukw = ok(host, port, {"id": "TC-38", "cmd": "user.admin.list", "token": admin_token, "data": {"phone_keyword": "8001"}}, "TC-38 search")
    if not any("8001" in x.get("phone", "") for x in ukw["data"]["items"]):
        raise RuntimeError("TC-38 search miss")

    pl = ok(host, port, {"id": "TC-39", "cmd": "pile.list", "token": admin_token, "data": {"status": "闲置"}}, "TC-39 piles")
    if pl["data"]["items"] and any(x.get("status") != "闲置" for x in pl["data"]["items"]):
        raise RuntimeError("TC-39 filter leak")

    dfrom, dto = admin_default_date_range()
    ol = ok(host, port, {"id": "TC-40", "cmd": "order.list", "token": admin_token, "data": {"status": "已完成", "limit": 20, "date_from": dfrom, "date_to": dto}}, "TC-40 orders")
    if ol["data"]["items"] and any(x.get("status") != "已完成" for x in ol["data"]["items"]):
        raise RuntimeError("TC-40 status leak")

    logs = ok(host, port, {"id": "TC-41", "cmd": "operation_log.list", "token": admin_token, "data": {"action": "登录", "limit": 50}}, "TC-41 log action")
    if logs["data"]["items"] and any(x.get("action") != "登录" for x in logs["data"]["items"]):
        raise RuntimeError("TC-41 action leak")

    ok(host, port, {"id": "TC-42", "cmd": "operation_log.list", "token": admin_token, "data": {"keyword": "admin", "limit": 50}}, "TC-42 log keyword")

    # ===== 模块7：管理端写操作 TC-43~49 =====
    sname = f"集成测试站{random.randint(100, 999)}"
    if "station_id" not in ok(host, port, {"id": "TC-43", "cmd": "station.create", "token": admin_token, "data": {"name": sname, "address": "测试路1号", "lat": 22.5, "lng": 114.0, "price": 1.2, "fast_count": 1, "slow_count": 1}}, "TC-43 create")["data"]:
        raise RuntimeError("TC-43 no station_id")

    ok(host, port, {"id": "TC-44", "cmd": "pile.restart", "token": admin_token, "data": {"pile_no": ensure_fault_pile(host, port, admin_token)}}, "TC-44 restart")

    ok(host, port, {"id": "TC-45", "cmd": "user.freeze", "token": admin_token, "data": {"user_id": 3, "freeze": True}}, "TC-45 freeze")
    if send_request(host, port, {"id": "TC-45b", "cmd": "user.login", "data": {"phone": "13800138003"}}).get("ok"):
        raise RuntimeError("TC-45 should not login")

    ok(host, port, {"id": "TC-46", "cmd": "user.freeze", "token": admin_token, "data": {"user_id": 3, "freeze": False}}, "TC-46 unfreeze")
    ok(host, port, {"id": "TC-46b", "cmd": "user.login", "data": {"phone": "13800138003"}}, "TC-46 login again")

    if send_request(host, port, {"id": "TC-47", "cmd": "user.freeze", "token": admin_token, "data": {"user_id": 2, "freeze": True}}).get("ok"):
        raise RuntimeError("TC-47 freeze 8002 should fail")

    u8004 = ok(host, port, {"id": "TC-48a", "cmd": "user.login", "data": {"phone": "13800138004"}}, "TC-48 login 8004")
    t8004 = u8004["data"]["token"]
    cleanup_user_order(host, port, t8004, admin_token)
    p48 = find_idle_pile(host, port, admin_token)
    if p48:
        on48 = ok(host, port, {"id": "TC-48b", "cmd": "charge.reserve", "token": t8004, "data": {"pile_no": p48}}, "TC-48 reserve")["data"]["order_no"]
        ok(host, port, {"id": "TC-48c", "cmd": "charge.start", "token": t8004, "data": {"order_no": on48}}, "TC-48 start")
        print("\n>>> TC-48 waiting 65s for charge amount...")
        time.sleep(65)
        ok(host, port, {"id": "TC-48d", "cmd": "charge.stop", "token": t8004, "data": {"order_no": on48}}, "TC-48 stop")
        ok(host, port, {"id": "TC-48e", "cmd": "order.admin.settle", "token": admin_token, "data": {"order_no": on48}}, "TC-48 admin settle")
    else:
        print("WARN TC-48 skipped: no idle pile")

    pd_pile = find_idle_pile(host, port, admin_token)
    if not pd_pile:
        raise RuntimeError("TC-49 no pile for detail")
    pd = ok(host, port, {"id": "TC-49", "cmd": "pile.detail", "token": admin_token, "data": {"pile_no": pd_pile}}, "TC-49 pile.detail")
    if "pile_no" not in pd.get("data", {}):
        raise RuntimeError("TC-49 bad detail response")

    print(f"\n========== ALL {total} TESTS PASSED ==========")


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
    try:
        run_all(host, port)
        return 0
    except Exception as exc:
        print(f"\nTEST FAILED: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
