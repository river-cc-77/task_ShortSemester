#!/usr/bin/env python3
"""从 charge_order 快速生成 ads_* 分析表（应急脚本）。

产出: ads_station_hourly、ads_station_daily、ads_daily_stats（大屏 KPI / 趋势）。

本项目验收环境为 Linux + collector/ads-collector，请优先使用 collector。
本脚本仅在无法编译/运行 ads-collector 时临时使用，口径简于正式 collector。

用法:
  python3 ml/bootstrap_ads.py
"""

from __future__ import annotations

import sqlite3
import sys
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import connect_db, resolve_db_path


def parse_ts(ts: str | None) -> datetime | None:
    if not ts:
        return None
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M"):
        try:
            return datetime.strptime(ts, fmt)
        except ValueError:
            continue
    return None


def duration_minutes(start: str | None, end: str | None) -> float:
    s, e = parse_ts(start), parse_ts(end)
    if not s or not e or e <= s:
        return 0.0
    return (e - s).total_seconds() / 60.0


def main() -> int:
    conn = connect_db()
    cur = conn.cursor()

    stations = [row[0] for row in cur.execute("SELECT id FROM station ORDER BY id")]
    if not stations:
        print("无充电站", file=sys.stderr)
        return 1

    end_day = datetime.now().date()
    start_day = end_day - timedelta(days=29)

    cur.execute("DELETE FROM ads_station_hourly")
    cur.execute("DELETE FROM ads_station_daily")
    cur.execute("DELETE FROM ads_daily_stats")

    pile_total = cur.execute("SELECT COUNT(*) FROM pile").fetchone()[0]

    # 全量网格 0 初始化
    day = start_day
    while day <= end_day:
        ds = day.isoformat()
        for sid in stations:
            pile_cnt = cur.execute(
                "SELECT COUNT(*) FROM pile WHERE station_id=?", (sid,)
            ).fetchone()[0]
            cur.execute(
                """
                INSERT INTO ads_station_daily
                    (station_id, stat_date, orders, revenue, kwh, pile_cnt,
                     occ_min, utilization, busy_ratio, fault_rate,
                     avg_session_min, turnover, peak_hour, updated_at)
                VALUES (?, ?, 0, 0, 0, ?, 0, 0, 0, 0, 0, 0, NULL, datetime('now','localtime'))
                """,
                (sid, ds, pile_cnt),
            )
            for hour in range(24):
                cur.execute(
                    """
                    INSERT INTO ads_station_hourly
                        (station_id, stat_date, stat_hour, orders, revenue, kwh,
                         duration_min, active_users, updated_at)
                    VALUES (?, ?, ?, 0, 0, 0, 0, 0, datetime('now','localtime'))
                    """,
                    (sid, ds, hour),
                )
        day += timedelta(days=1)

    orders = cur.execute(
        """
        SELECT station_id, start_at, end_at, kwh, amount, user_id
        FROM charge_order
        WHERE status='已完成' AND start_at IS NOT NULL
        """
    ).fetchall()

    hourly_acc: dict[tuple, dict] = {}
    daily_acc: dict[tuple, dict] = {}

    def new_daily_acc() -> dict:
        return {
            "orders": 0,
            "revenue": 0.0,
            "kwh": 0.0,
            "occ": 0.0,       # 时间族占用分钟（已完成 + 待支付），对应 collector 的 occ_min
            "dur_done": 0.0,  # 已完成且时长有效的分钟（avg_session_min 的分子）
            "sess_done": 0,   # 已完成且时长有效的单数（avg_session_min 的分母）
            "hour_orders": {},
        }

    for station_id, start_at, end_at, kwh, amount, user_id in orders:
        start = parse_ts(start_at)
        if not start:
            continue
        if start.date() < start_day or start.date() > end_day:
            continue  # 与 collector 一致：仅近 30 天窗口
        ds = start.date().isoformat()
        hour = start.hour
        dur = duration_minutes(start_at, end_at)
        key_h = (station_id, ds, hour)
        key_d = (station_id, ds)

        h = hourly_acc.setdefault(key_h, {"orders": 0, "revenue": 0.0, "kwh": 0.0, "duration": 0.0, "users": set()})
        h["orders"] += 1
        h["revenue"] += float(amount or 0)
        h["kwh"] += float(kwh or 0)
        h["duration"] += dur
        h["users"].add(user_id)

        d = daily_acc.setdefault(key_d, new_daily_acc())
        d["orders"] += 1
        d["revenue"] += float(amount or 0)
        d["kwh"] += float(kwh or 0)
        d["occ"] += dur
        if dur > 0:  # 缺 end_at / end<=start 的单不计入有效时长与分母（对齐 collector 的 ts_missing=0）
            d["dur_done"] += dur
            d["sess_done"] += 1
        d["hour_orders"][hour] = d["hour_orders"].get(hour, 0) + 1

    # 时间族另一半：待支付单同样占用充电桩（collector: status IN ('已完成','待支付')）
    pending_time = cur.execute(
        """
        SELECT station_id, start_at, end_at
        FROM charge_order
        WHERE status='待支付' AND start_at IS NOT NULL
        """
    ).fetchall()
    for station_id, start_at, end_at in pending_time:
        start = parse_ts(start_at)
        if not start or start.date() < start_day or start.date() > end_day:
            continue
        d = daily_acc.setdefault((station_id, start.date().isoformat()), new_daily_acc())
        d["occ"] += duration_minutes(start_at, end_at)

    for (station_id, ds, hour), h in hourly_acc.items():
        cur.execute(
            """
            UPDATE ads_station_hourly
            SET orders=?, revenue=?, kwh=?, duration_min=?, active_users=?, updated_at=datetime('now','localtime')
            WHERE station_id=? AND stat_date=? AND stat_hour=?
            """,
            (
                h["orders"],
                round(h["revenue"], 2),
                round(h["kwh"], 2),
                round(h["duration"], 2),
                len(h["users"]),
                station_id,
                ds,
                hour,
            ),
        )

    platform_day: dict[str, dict] = {}
    platform_hour_orders: dict[str, dict[int, int]] = {}

    def new_platform_day() -> dict:
        return {
            "orders": 0,
            "revenue": 0.0,
            "kwh": 0.0,
            "occ": 0.0,
            "dur_done": 0.0,
            "sess_done": 0,
            "users": set(),
        }

    for (station_id, ds), d in daily_acc.items():
        peak = max(d["hour_orders"], key=d["hour_orders"].get) if d["hour_orders"] else None
        avg_session = d["dur_done"] / d["sess_done"] if d["sess_done"] else 0
        row = cur.execute(
            "SELECT pile_cnt FROM ads_station_daily WHERE station_id=? AND stat_date=?",
            (station_id, ds),
        ).fetchone()
        if not row:
            continue
        pile_cnt = row[0]
        turnover = d["orders"] / pile_cnt if pile_cnt else 0
        occ_min = d["occ"]
        utilization = min(occ_min / (pile_cnt * 1440.0), 1.0) if pile_cnt > 0 else 0.0
        cur.execute(
            """
            UPDATE ads_station_daily
            SET orders=?, revenue=?, kwh=?, occ_min=?, utilization=?,
                avg_session_min=?, turnover=?, peak_hour=?,
                updated_at=datetime('now','localtime')
            WHERE station_id=? AND stat_date=?
            """,
            (
                d["orders"],
                round(d["revenue"], 2),
                round(d["kwh"], 2),
                round(occ_min, 2),
                round(utilization, 4),
                round(avg_session, 2),
                round(turnover, 4),
                peak,
                station_id,
                ds,
            ),
        )

        p = platform_day.setdefault(ds, new_platform_day())
        p["orders"] += d["orders"]
        p["revenue"] += d["revenue"]
        p["kwh"] += d["kwh"]
        p["occ"] += d["occ"]
        p["dur_done"] += d["dur_done"]
        p["sess_done"] += d["sess_done"]
        for hour, cnt in d["hour_orders"].items():
            platform_hour_orders.setdefault(ds, {})
            platform_hour_orders[ds][hour] = platform_hour_orders[ds].get(hour, 0) + cnt

    for (station_id, ds, hour), h in hourly_acc.items():
        p = platform_day.setdefault(ds, new_platform_day())
        p["users"].update(h["users"])

    pending_by_day = {
        row[0]: row[1]
        for row in cur.execute(
            """
            SELECT substr(created_at, 1, 10) AS d, COUNT(*)
            FROM charge_order
            WHERE status='待支付'
            GROUP BY d
            """
        )
    }
    new_users_by_day = {
        row[0]: row[1]
        for row in cur.execute(
            """
            SELECT substr(created_at, 1, 10) AS d, COUNT(*)
            FROM user
            GROUP BY d
            """
        )
    }

    day = start_day
    while day <= end_day:
        ds = day.isoformat()
        p = platform_day.get(ds, new_platform_day())
        order_count = p["orders"]
        revenue = p["revenue"]
        kwh = p["kwh"]
        active_users = len(p["users"])
        pending = pending_by_day.get(ds, 0)
        new_users = new_users_by_day.get(ds, 0)
        total_users = cur.execute(
            "SELECT COUNT(*) FROM user WHERE substr(created_at, 1, 10) <= ?",
            (ds,),
        ).fetchone()[0]
        occ_min = p["occ"]  # 时间族：已完成 + 待支付
        completion = (
            order_count / (order_count + pending) if (order_count + pending) > 0 else 0.0
        )
        active_ratio = active_users / total_users if total_users > 0 else 0.0
        per_user_orders = order_count / active_users if active_users > 0 else 0.0
        per_user_kwh = kwh / active_users if active_users > 0 else 0.0
        # 与 collector 对齐：分母只算时长有效的已完成单
        avg_session = p["dur_done"] / p["sess_done"] if p["sess_done"] > 0 else 0.0
        avg_kwh = kwh / order_count if order_count > 0 else 0.0
        utilization = min(occ_min / (pile_total * 1440.0), 1.0) if pile_total > 0 else 0.0
        hour_orders = platform_hour_orders.get(ds, {})
        peak_hour = max(hour_orders, key=hour_orders.get) if hour_orders else None

        cur.execute(
            """
            INSERT INTO ads_daily_stats
                (stat_date, total_revenue, total_kwh, order_count, active_user_count,
                 new_user_count, total_users, pending_cnt, completion_rate, active_ratio,
                 per_user_orders, per_user_kwh, avg_session_min, avg_kwh_order,
                 occ_min, utilization, busy_ratio, fault_rate, peak_hour, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, ?,
                    datetime('now','localtime'))
            """,
            (
                ds,
                round(revenue, 2),
                round(kwh, 2),
                order_count,
                active_users,
                new_users,
                total_users,
                pending,
                round(completion, 4),
                round(active_ratio, 4),
                round(per_user_orders, 4),
                round(per_user_kwh, 4),
                round(avg_session, 2),
                round(avg_kwh, 4),
                round(occ_min, 2),
                round(utilization, 4),
                peak_hour,
            ),
        )
        day += timedelta(days=1)

    conn.commit()
    hourly_cnt = cur.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    daily_cnt = cur.execute("SELECT COUNT(*) FROM ads_station_daily").fetchone()[0]
    platform_cnt = cur.execute("SELECT COUNT(*) FROM ads_daily_stats").fetchone()[0]
    conn.close()
    print(
        f"bootstrap_ads 完成: ads_station_hourly={hourly_cnt}, "
        f"ads_station_daily={daily_cnt}, ads_daily_stats={platform_cnt}"
    )
    print(f"数据库: {resolve_db_path()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
