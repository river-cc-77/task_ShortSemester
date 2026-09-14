#!/usr/bin/env python3
"""批量生成模拟历史充电订单，供 collector 聚合与 ML 预测使用。

用法:
  python ml/generate_orders.py [数量] [--days 90]

示例:
  python ml/generate_orders.py 3000
  python ml/generate_orders.py 5000 --days 60
"""

from __future__ import annotations

import argparse
import random
import sqlite3
import sys
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import connect_db, resolve_db_path


def main() -> int:
    parser = argparse.ArgumentParser(description="生成模拟充电订单")
    parser.add_argument("count", nargs="?", type=int, default=3000, help="订单数量，默认 3000")
    parser.add_argument("--days", type=int, default=30, help="时间跨度（天），默认 30（与 collector 回填窗口一致）")
    args = parser.parse_args()

    db_path = resolve_db_path()
    conn = connect_db(db_path)
    cur = conn.cursor()

    users = [
        row[0]
        for row in cur.execute(
            "SELECT id FROM user WHERE status='正常' AND id NOT IN "
            "(SELECT user_id FROM charge_order WHERE status='待支付')"
        ).fetchall()
    ]
    if not users:
        users = [row[0] for row in cur.execute("SELECT id FROM user WHERE status='正常'").fetchall()]
    if not users:
        print("错误: 没有可用用户", file=sys.stderr)
        return 1

    piles = cur.execute(
        "SELECT p.id, p.station_id, p.power_kw, s.price "
        "FROM pile p JOIN station s ON p.station_id = s.id "
        "WHERE p.status IN ('闲置', '故障')"
    ).fetchall()
    if not piles:
        print("错误: 没有可用电桩", file=sys.stderr)
        return 1

    stations = {row[0]: row[1] for row in cur.execute("SELECT id, price FROM station").fetchall()}

    existing = {
        row[0]
        for row in cur.execute("SELECT order_no FROM charge_order WHERE order_no LIKE 'CDSYN%'").fetchall()
    }
    seq = len(existing)

    end_day = datetime.now().replace(hour=22, minute=0, second=0, microsecond=0)
    # 历史日窗口为 [today-(days-1), today-1]，今天由下面的 today_quota 单独铺。
    # 原写法 days 天再取 randint(0, days-1)，最早一天落到 today-days，
    # 正好在 collector/网格窗口（today-(windowDays-1)）之外，那 1/30 的单白造。
    start_day = end_day - timedelta(days=args.days - 1)

    def write_order(start: datetime) -> None:
        nonlocal seq, inserted
        user_id = random.choice(users)
        pile_id, station_id, power_kw, price = random.choice(piles)
        if price is None or price <= 0:
            price = stations.get(station_id, 1.2)
        duration_min = random.randint(20, 90)
        end = start + timedelta(minutes=duration_min)
        now = datetime.now()
        if end > now:
            # 只保证 start 不在未来还不够：end = start + 20~90min 仍可能越过 now，
            # 会变成"已完成但尚未发生"的占用，虚高 occ_min / utilization
            start = now - timedelta(minutes=duration_min)
            end = now
        kwh = round(float(power_kw) * duration_min / 60.0, 2)
        amount = round(kwh * float(price), 2)

        seq += 1
        order_no = f"CDSYN{seq:06d}"
        while order_no in existing:
            seq += 1
            order_no = f"CDSYN{seq:06d}"
        existing.add(order_no)

        reserve_at = (start - timedelta(minutes=random.randint(3, 8))).strftime("%Y-%m-%d %H:%M:%S")
        ts = start.strftime("%Y-%m-%d %H:%M:%S")
        end_ts = end.strftime("%Y-%m-%d %H:%M:%S")
        cur.execute(
            """
            INSERT INTO charge_order
                (order_no, user_id, station_id, pile_id, status,
                 reserve_at, start_at, end_at, kwh, amount, created_at)
            VALUES (?, ?, ?, ?, '已完成', ?, ?, ?, ?, ?, ?)
            """,
            (order_no, user_id, station_id, pile_id, reserve_at, ts, end_ts, kwh, amount, ts),
        )
        inserted += 1

    inserted = 0
    today_base = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    today_quota = max(120, args.count // 15)
    for i in range(today_quota):
        hour = 7 + (i % 16)
        minute = random.randint(0, 59)
        start = today_base.replace(hour=hour, minute=minute)
        if start > datetime.now():
            start = datetime.now() - timedelta(minutes=random.randint(20, 240))
        write_order(start)

    for _ in range(args.count):
        day_offset = random.randint(0, max(args.days - 2, 0))  # 只铺历史日，今天见 today_quota
        hour = random.randint(7, 22)
        minute = random.randint(0, 59)
        start = (start_day + timedelta(days=day_offset)).replace(hour=hour, minute=minute)
        write_order(start)

    conn.commit()
    conn.close()
    print(f"已写入 {inserted} 条模拟订单 -> {db_path}")
    print("下一步（Linux）: cd collector && ./ads-collector")
    print("  或一键: bash ml/run_pipeline.sh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
