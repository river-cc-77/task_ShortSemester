#!/usr/bin/env python3
"""为「今天」补模拟订单，避免 ads_station_* 最新日全 0。

collector / bootstrap_ads 只会聚合已有订单；若今天没有已完成订单，大屏高峰/排行会空。

用法:
  python3 ml/ensure_today_orders.py
  python3 ml/ensure_today_orders.py 180

之后请刷新 ads 表:
  cd collector && ./ads-collector
  或: python3 ml/bootstrap_ads.py
"""

from __future__ import annotations

import argparse
import random
import sys
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import connect_db, resolve_db_path


def main() -> int:
    parser = argparse.ArgumentParser(description="为今日补模拟充电订单")
    parser.add_argument("count", nargs="?", type=int, default=150, help="今日订单数，默认 150")
    args = parser.parse_args()

    db_path = resolve_db_path()
    conn = connect_db(db_path)
    cur = conn.cursor()
    today = datetime.now().strftime("%Y-%m-%d")

    existing = cur.execute(
        """
        SELECT COUNT(*) FROM charge_order
        WHERE status='已完成' AND date(start_at)=?
        """,
        (today,),
    ).fetchone()[0]
    if existing >= args.count:
        print(f"今日已有 {existing} 条已完成订单，无需补数据")
        conn.close()
        return 0

    need = args.count - existing
    users = [row[0] for row in cur.execute("SELECT id FROM user WHERE status='正常'").fetchall()]
    piles = cur.execute(
        """
        SELECT p.id, p.station_id, p.power_kw, s.price
        FROM pile p JOIN station s ON p.station_id = s.id
        WHERE p.status IN ('闲置', '故障')
        """
    ).fetchall()
    if not users or not piles:
        print("缺少用户或电桩，请先导入 schema/seed", file=sys.stderr)
        return 1

    seq = cur.execute("SELECT COUNT(*) FROM charge_order WHERE order_no LIKE 'CDTOD%'").fetchone()[0]
    inserted = 0
    today_base = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)

    for i in range(need):
        user_id = random.choice(users)
        pile_id, station_id, power_kw, price = random.choice(piles)
        price = float(price or 1.2)
        hour = 7 + (i % 16)
        minute = random.randint(0, 59)
        now = datetime.now()
        start = today_base.replace(hour=hour, minute=minute)
        if start > now:
            start = now - timedelta(minutes=random.randint(30, 180))
        duration_min = random.randint(25, 85)
        end = start + timedelta(minutes=duration_min)
        if end > now:
            # start 不在未来还不够：end = start + 25~85min 仍可能越过 now，
            # 会被算成"已完成但尚未发生"的占用，虚高今天的 occ_min / utilization
            start = now - timedelta(minutes=duration_min)
            end = now
        kwh = round(float(power_kw) * duration_min / 60.0, 2)
        amount = round(kwh * price, 2)

        seq += 1
        order_no = f"CDTOD{seq:06d}"
        ts = start.strftime("%Y-%m-%d %H:%M:%S")
        end_ts = end.strftime("%Y-%m-%d %H:%M:%S")
        reserve_at = (start - timedelta(minutes=5)).strftime("%Y-%m-%d %H:%M:%S")

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

    conn.commit()
    conn.close()
    print(f"已为 {today} 写入 {inserted} 条订单 -> {db_path}")
    print("请刷新 ads 表: cd collector && ./ads-collector")
    print("  或: python3 ml/bootstrap_ads.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
