#!/usr/bin/env python3
"""从 charge_order 快速生成 ads_station_hourly / ads_station_daily（应急脚本）。

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

        d = daily_acc.setdefault(key_d, {"orders": 0, "revenue": 0.0, "kwh": 0.0, "duration": 0.0, "hour_orders": {}})
        d["orders"] += 1
        d["revenue"] += float(amount or 0)
        d["kwh"] += float(kwh or 0)
        d["duration"] += dur
        d["hour_orders"][hour] = d["hour_orders"].get(hour, 0) + 1

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

    for (station_id, ds), d in daily_acc.items():
        peak = max(d["hour_orders"], key=d["hour_orders"].get) if d["hour_orders"] else None
        avg_session = d["duration"] / d["orders"] if d["orders"] else 0
        row = cur.execute(
            "SELECT pile_cnt FROM ads_station_daily WHERE station_id=? AND stat_date=?",
            (station_id, ds),
        ).fetchone()
        if not row:
            continue
        pile_cnt = row[0]
        turnover = d["orders"] / pile_cnt if pile_cnt else 0
        cur.execute(
            """
            UPDATE ads_station_daily
            SET orders=?, revenue=?, kwh=?, avg_session_min=?, turnover=?, peak_hour=?,
                updated_at=datetime('now','localtime')
            WHERE station_id=? AND stat_date=?
            """,
            (
                d["orders"],
                round(d["revenue"], 2),
                round(d["kwh"], 2),
                round(avg_session, 2),
                round(turnover, 4),
                peak,
                station_id,
                ds,
            ),
        )

    conn.commit()
    hourly_cnt = cur.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    daily_cnt = cur.execute("SELECT COUNT(*) FROM ads_station_daily").fetchone()[0]
    conn.close()
    print(f"bootstrap_ads 完成: ads_station_hourly={hourly_cnt}, ads_station_daily={daily_cnt}")
    print(f"数据库: {resolve_db_path()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
