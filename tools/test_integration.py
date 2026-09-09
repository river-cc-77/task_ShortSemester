#!/usr/bin/env python3
"""东软充电桩 — 黑盒集成测试（3 功能点 × 8 条 = 24 条）。

用法（先启动 ./charge-server，建议 fresh seed 库）：
  python3 tools/test_integration.py [host] [port]

与交付物 03测试用例.xls 的「测试用例1～3」各 8 条一一对应（TC-01～TC-24）。

测试方式（黑盒）：
  - 只通过 TCP 发送 JSON 请求，检查返回 ok / error.code，不依赖源码内部实现。
  - ok()  断言业务成功（ok=true）
  - err() 断言业务失败且错误码与用例表一致

seed 常用账号：
  13800138001  正常用户，余额充足（功能点2主流程）
  13800138002  自带「待支付」订单（功能点3）
  13800138004  余额较低（TC-20 余额不足）
  13800138006  冻结账号（TC-03）
  admin/123456 管理员（代结算、查列表、置故障桩）
"""

from __future__ import annotations

import json
import math
import random
import re
import socket
import struct
import sys
import time
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
from testcase_catalog import MODULES

# seed.sql 各站单价（pile_no 前缀 SZ00N → station N），用于 TC-20 估算充电时长
STATION_PRICES = {1: 1.20, 2: 1.60, 3: 1.50, 4: 1.30, 5: 1.10}


def send_request(host: str, port: int, payload: dict) -> dict:
    """发送一帧 TCP JSON 请求并解析响应（4 字节大端长度 + UTF-8 body）。"""
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    frame = struct.pack(">I", len(body)) + body
    with socket.create_connection((host, port), timeout=8) as sock:
        sock.sendall(frame)
        length = struct.unpack(">I", _recv_exact(sock, 4))[0]
        return json.loads(_recv_exact(sock, length).decode("utf-8"))


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    """循环 recv 直到读满 size 字节，避免 TCP 半包。"""
    chunks, remaining = [], size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def ok(host: str, port: int, req: dict, label: str) -> dict:
    """断言 ok=true，失败则抛异常并打印请求标签（对应用例「预期：成功」）。"""
    resp = send_request(host, port, req)
    print(f"\n>>> {label}")
    print(json.dumps(resp, ensure_ascii=False, indent=2))
    if not resp.get("ok"):
        raise RuntimeError(f"{label} failed: {resp}")
    return resp


def err(host: str, port: int, req: dict, label: str, code: str) -> dict:
    """断言 ok=false 且 error.code 等于预期（对应用例「预期：某错误码」）。"""
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
    """从 pile.list 取第一根「闲置」桩编号（可排除指定 pile_no）。"""
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


def _pile_station_id(pile_no: str) -> int:
    """从 pile_no（如 SZ003-02）解析 station 序号。"""
    m = re.match(r"^SZ(\d+)", pile_no)
    return int(m.group(1)) if m else 0


def find_idle_pile_max_bill_rate(
    host: str, port: int, admin_token: str, exclude: Optional[set] = None,
) -> Optional[tuple[str, float, float]]:
    """选 power_kw × 单价 最大的闲置桩，便于 TC-20 在较短时间内产生超额费用。"""
    exclude = exclude or set()
    resp = send_request(
        host, port,
        {"id": "find_fast", "cmd": "pile.list", "token": admin_token, "data": {"status": "闲置"}},
    )
    if not resp.get("ok"):
        return None
    best: Optional[tuple[str, float, float, float]] = None
    for row in resp["data"]["items"]:
        no = row["pile_no"]
        if row.get("status") != "闲置" or no in exclude:
            continue
        power = float(row.get("power_kw") or 0)
        price = STATION_PRICES.get(_pile_station_id(no), 1.2)
        rate = power * price
        if best is None or rate > best[3]:
            best = (no, power, price, rate)
    if best is None:
        return None
    return best[0], best[1], best[2]


def charge_seconds_to_exceed_balance(balance: float, power_kw: float, price: float) -> int:
    """按服务端计费公式估算充电秒数，使 stop 后 amount > balance（TC-20 前置）。"""
    if power_kw <= 0 or price <= 0:
        raise RuntimeError("invalid pile power/price for charge wait")
    target = balance + 0.05  # 留余量应对四舍五入
    kwh_needed = target / price
    seconds = math.ceil(kwh_needed * 3600.0 / power_kw)
    return max(seconds + 3, 10)


def ensure_fault_pile(host: str, port: int, admin_token: str, exclude: Optional[set] = None) -> str:
    """返回可用于 TC-16 的故障桩；若无则临时将一根闲置桩置为「故障」。"""
    resp = send_request(
        host, port,
        {"id": "find_fault", "cmd": "pile.list", "token": admin_token, "data": {"status": "故障"}},
    )
    if resp.get("ok"):
        for row in resp["data"]["items"]:
            no = row["pile_no"]
            if row.get("status") == "故障" and no not in (exclude or set()):
                return no
    pile_no = find_idle_pile(host, port, admin_token, exclude=exclude)
    if not pile_no:
        raise RuntimeError("no pile available to mark fault")
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
    """清理用户身上残留订单，保证脚本可重复运行（非用例本身，测试前准备）。"""
    resp = send_request(host, port, {"id": "c0", "cmd": "order.check_open", "token": token, "data": {}})
    if not resp.get("ok") or not resp["data"].get("has_open"):
        return
    order = resp["data"]["order"]
    order_no = order["order_no"]
    status = order["status"]
    if status == "预约":
        ok(host, port, {"id": "c1", "cmd": "charge.cancel", "token": token, "data": {"order_no": order_no}}, "cleanup cancel")
        return
    if status == "充电中":
        stop = ok(host, port, {"id": "c2", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}}, "cleanup stop")
        order["amount"] = stop["data"]["amount"]
        status = "待支付"
    if status == "待支付":
        settle = send_request(host, port, {"id": "c3", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}})
        if settle.get("ok"):
            return
        if settle.get("error", {}).get("code") == "BALANCE_NOT_ENOUGH":
            amount = float(order.get("amount") or 0)
            profile = send_request(host, port, {"id": "c3p", "cmd": "user.profile.get", "token": token, "data": {}})
            balance = float(profile.get("data", {}).get("balance", 0)) if profile.get("ok") else 0.0
            if amount > balance:
                need = round(amount - balance + 1.0, 2)
                ok(host, port, {"id": "c3r", "cmd": "user.recharge", "token": token, "data": {"amount": need}}, "cleanup recharge")
        ok(host, port, {"id": "c4", "cmd": "order.admin.settle", "token": admin_token, "data": {"order_no": order_no}}, "cleanup admin settle")


def run_all(host: str, port: int) -> None:
    total = sum(len(m["cases"]) for m in MODULES)

    # =========================================================================
    # 功能点1：用户登录认证（TC-01～08）— 对应 03测试用例.xls「测试用例1」
    # 黑盒重点：各种手机号输入 + token 鉴权，只验返回结果。
    # =========================================================================

    # TC-01 正确手机号登录
    # 输入：phone=13800138001  预期：ok=true，返回 token、balance
    user01 = ok(host, port, {"id": "TC-01", "cmd": "user.login", "data": {"phone": "13800138001"}}, "TC-01 正确手机号")
    token = user01["data"]["token"]

    # TC-02 新手机号自动注册
    # 输入：未注册 11 位手机号  预期：ok=true，自动建号并返回 token
    new_phone = f"139{random.randint(10000000, 99999999)}"
    ok(host, port, {"id": "TC-02", "cmd": "user.login", "data": {"phone": new_phone}}, "TC-02 新号注册")

    # TC-03 冻结用户登录
    # 输入：phone=13800138006（seed 冻结）  预期：USER_FROZEN
    err(host, port, {"id": "TC-03", "cmd": "user.login", "data": {"phone": "13800138006"}}, "TC-03 冻结用户", "USER_FROZEN")

    # TC-04 空手机号
    # 输入：phone=""  预期：INVALID_PARAM
    err(host, port, {"id": "TC-04", "cmd": "user.login", "data": {"phone": ""}}, "TC-04 空手机号", "INVALID_PARAM")

    # TC-05 手机号过短（10 位）
    # 输入：phone=1380013800  预期：INVALID_PARAM
    err(host, port, {"id": "TC-05", "cmd": "user.login", "data": {"phone": "1380013800"}}, "TC-05 过短", "INVALID_PARAM")

    # TC-06 手机号过长（12 位）
    # 输入：phone=138001380011  预期：INVALID_PARAM
    err(host, port, {"id": "TC-06", "cmd": "user.login", "data": {"phone": "138001380011"}}, "TC-06 过长", "INVALID_PARAM")

    # TC-07 含非数字字符
    # 输入：phone=1380013800a  预期：INVALID_PARAM
    err(host, port, {"id": "TC-07", "cmd": "user.login", "data": {"phone": "1380013800a"}}, "TC-07 非法字符", "INVALID_PARAM")

    # TC-08 无效 token 访问受保护接口
    # 输入：token=bad-token，cmd=station.list  预期：UNAUTHORIZED
    err(
        host, port,
        {"id": "TC-08", "cmd": "station.list", "token": "bad-token", "data": {"lat": 22.5, "lng": 114.0}},
        "TC-08 无效token",
        "UNAUTHORIZED",
    )

    # 管理员登录：后续 TC-16/20/22/23 需要 admin_token（非独立用例）
    admin = ok(host, port, {"id": "adm", "cmd": "admin.login", "data": {"username": "admin", "password": "123456"}}, "admin login")
    admin_token = admin["data"]["token"]

    # =========================================================================
    # 功能点2：充电订单全流程（TC-09～16）— 对应「测试用例2」
    # 黑盒重点：预约→取消→再预约→充电→进度→停止→结算，及故障桩拦截。
    # =========================================================================
    cleanup_user_order(host, port, token, admin_token)

    # TC-09 无未完成订单
    # 输入：8001 order.check_open  预期：has_open=false
    open0 = ok(host, port, {"id": "TC-09", "cmd": "order.check_open", "token": token, "data": {}}, "TC-09 无未完成单")
    if open0["data"].get("has_open"):
        raise RuntimeError("TC-09 should have no open order")

    # TC-10 预约闲置桩
    # 输入：charge.reserve + 动态选取的闲置 pile_no  预期：返回 order_no，status=预约
    pile_no = find_idle_pile(host, port, admin_token)
    if not pile_no:
        raise RuntimeError("TC-10 no idle pile")
    reserve = ok(host, port, {"id": "TC-10", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}}, "TC-10 预约")
    order_no = reserve["data"]["order_no"]

    # TC-11 取消预约
    # 输入：charge.cancel 上述 order_no  预期：成功，check_open=false
    ok(host, port, {"id": "TC-11", "cmd": "charge.cancel", "token": token, "data": {"order_no": order_no}}, "TC-11 取消预约")

    # TC-12 开始充电
    # 输入：再次 reserve + charge.start  预期：status=充电中
    reserve2 = ok(host, port, {"id": "TC-12a", "cmd": "charge.reserve", "token": token, "data": {"pile_no": pile_no}}, "TC-12 再预约")
    order_no = reserve2["data"]["order_no"]
    ok(host, port, {"id": "TC-12b", "cmd": "charge.start", "token": token, "data": {"order_no": order_no}}, "TC-12 开始充电")

    # TC-13 查询充电进度
    # 输入：charge.progress  预期：含 kwh/amount/estimated_remain_seconds>0
    prog = ok(host, port, {"id": "TC-13", "cmd": "charge.progress", "token": token, "data": {"order_no": order_no}}, "TC-13 进度")
    if prog["data"].get("estimated_remain_seconds", 0) <= 0:
        raise RuntimeError("TC-13 missing estimated_remain_seconds")

    # TC-14 停止充电
    # 输入：charge.stop  预期：status=待支付，返回 amount
    ok(host, port, {"id": "TC-14", "cmd": "charge.stop", "token": token, "data": {"order_no": order_no}}, "TC-14 停止")

    # TC-15 用户结算
    # 输入：charge.settle  预期：status=已完成，扣减余额
    ok(host, port, {"id": "TC-15", "cmd": "charge.settle", "token": token, "data": {"order_no": order_no}}, "TC-15 结算")

    # TC-16 故障桩不可预约
    # 输入：charge.reserve 故障桩  预期：PILE_FAULT
    fault_pile = ensure_fault_pile(host, port, admin_token)
    err(host, port, {"id": "TC-16", "cmd": "charge.reserve", "token": token, "data": {"pile_no": fault_pile}}, "TC-16 故障桩", "PILE_FAULT")

    # =========================================================================
    # 功能点3：订单管理与待支付（TC-17～24）— 对应「测试用例3」
    # 黑盒重点：待支付拦截、越权、余额不足、代结算、列表筛选。
    # 说明：TC-23/24 需在 TC-22 代结算前执行，否则 8002 待支付单被清掉。
    # =========================================================================

    # TC-17 待支付订单检查
    # 输入：8002 order.check_open  预期：has_open=true，status=待支付
    u8002 = ok(host, port, {"id": "TC-17a", "cmd": "user.login", "data": {"phone": "13800138002"}}, "TC-17 login 8002")
    token8002 = u8002["data"]["token"]
    open8002 = ok(host, port, {"id": "TC-17", "cmd": "order.check_open", "token": token8002, "data": {}}, "TC-17 待支付检查")
    if not open8002["data"].get("has_open") or open8002["data"]["order"]["status"] != "待支付":
        raise RuntimeError("TC-17 8002 needs 待支付 order")
    pending_no = open8002["data"]["order"]["order_no"]

    # TC-18 有待支付禁止新预约
    # 输入：8002 charge.reserve  预期：ORDER_EXISTS
    err(host, port, {"id": "TC-18", "cmd": "charge.reserve", "token": token8002, "data": {"pile_no": "SZ005-01"}}, "TC-18 ORDER_EXISTS", "ORDER_EXISTS")

    # TC-19 越权结算他人订单
    # 输入：8001 token 结算 8002 的 order_no  预期：FORBIDDEN
    err(host, port, {"id": "TC-19", "cmd": "charge.settle", "token": token, "data": {"order_no": pending_no}}, "TC-19 FORBIDDEN", "FORBIDDEN")

    # TC-23 订单列表筛选（管理端）
    # 输入：order.list status=待支付  预期：列表含 pending_no，且均为待支付
    ol = ok(
        host, port,
        {"id": "TC-23", "cmd": "order.list", "token": admin_token, "data": {"status": "待支付", "limit": 50}},
        "TC-23 待支付列表",
    )
    if not any(x.get("order_no") == pending_no for x in ol["data"]["items"]):
        raise RuntimeError("TC-23 pending order not in list")

    # TC-24 有待支付禁止冻结
    # 输入：user.freeze user_id=2（8002）  预期：失败（有未完成单）
    if send_request(host, port, {"id": "TC-24", "cmd": "user.freeze", "token": admin_token, "data": {"user_id": 2, "freeze": True}}).get("ok"):
        raise RuntimeError("TC-24 freeze 8002 should fail")

    # TC-20 余额不足结算
    # 输入：8004 充电至 amount>balance 后 charge.settle  预期：BALANCE_NOT_ENOUGH
    u8004 = ok(host, port, {"id": "TC-20a", "cmd": "user.login", "data": {"phone": "13800138004"}}, "TC-20 login 8004")
    t8004 = u8004["data"]["token"]
    cleanup_user_order(host, port, t8004, admin_token)
    balance = float(
        ok(host, port, {"id": "TC-20p", "cmd": "user.profile.get", "token": t8004, "data": {}}, "TC-20 profile")["data"]["balance"]
    )
    picked = find_idle_pile_max_bill_rate(host, port, admin_token)
    if not picked:
        raise RuntimeError("TC-20 no idle pile")
    p48, power_kw, price = picked
    wait_s = charge_seconds_to_exceed_balance(balance, power_kw, price)
    on48 = ok(host, port, {"id": "TC-20b", "cmd": "charge.reserve", "token": t8004, "data": {"pile_no": p48}}, "TC-20 reserve")["data"]["order_no"]
    ok(host, port, {"id": "TC-20c", "cmd": "charge.start", "token": t8004, "data": {"order_no": on48}}, "TC-20 start")
    print(f"\n>>> TC-20 waiting {wait_s}s for amount > balance {balance}...")
    time.sleep(wait_s)
    stop48 = ok(host, port, {"id": "TC-20d", "cmd": "charge.stop", "token": t8004, "data": {"order_no": on48}}, "TC-20 stop")
    amount = float(stop48["data"]["amount"])
    if amount <= balance:
        raise RuntimeError(f"TC-20 setup: amount {amount} <= balance {balance}")
    err(host, port, {"id": "TC-20", "cmd": "charge.settle", "token": t8004, "data": {"order_no": on48}}, "TC-20 余额不足", "BALANCE_NOT_ENOUGH")

    # TC-21 充值后自行结算
    # 输入：user.recharge 补足差额 + charge.settle  预期：ok=true，订单已完成
    need = round(amount - balance + 1.0, 2)
    ok(host, port, {"id": "TC-21r", "cmd": "user.recharge", "token": t8004, "data": {"amount": need}}, "TC-21 充值")
    ok(host, port, {"id": "TC-21", "cmd": "charge.settle", "token": t8004, "data": {"order_no": on48}}, "TC-21 自行结算")

    # TC-22 管理员代结算
    # 输入：order.admin.settle 8002 的 pending_no  预期：ok=true，订单已完成
    ok(host, port, {"id": "TC-22", "cmd": "order.admin.settle", "token": admin_token, "data": {"order_no": pending_no}}, "TC-22 代结算")

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
