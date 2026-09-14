#!/usr/bin/env python3
"""离线校验 ML 预测结果（无需启动 server）。"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import HORIZONS, connect_db, resolve_db_path


def main() -> int:
    db_path = resolve_db_path()
    conn = connect_db(db_path)
    errors: list[str] = []

    stations = conn.execute("SELECT COUNT(*) FROM station").fetchone()[0]
    hourly = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    load_cnt = conn.execute("SELECT COUNT(*) FROM load_forecast").fetchone()[0]
    time_cnt = conn.execute("SELECT COUNT(*) FROM time_forecast").fetchone()[0]

    expected = stations * len(HORIZONS)
    if load_cnt != expected:
        errors.append(f"load_forecast 行数 {load_cnt} != 期望 {expected}（站数×3 horizon）")
    if time_cnt != expected:
        errors.append(f"time_forecast 行数 {time_cnt} != 期望 {expected}")

    bad_idle = conn.execute(
        "SELECT COUNT(*) FROM load_forecast WHERE predicted_idle_piles < 0"
    ).fetchone()[0]
    if bad_idle:
        errors.append(f"load_forecast 有 {bad_idle} 行 predicted_idle_piles < 0")

    bad_peak = conn.execute(
        "SELECT COUNT(*) FROM time_forecast "
        "WHERE predicted_peak_hour IS NOT NULL AND (predicted_peak_hour < 0 OR predicted_peak_hour > 23)"
    ).fetchone()[0]
    if bad_peak:
        errors.append(f"time_forecast 有 {bad_peak} 行 peak_hour 不在 0-23")

    conn.close()

    print(f"DB: {db_path}")
    print(f"  stations={stations}, ads_station_hourly={hourly}")
    print(f"  load_forecast={load_cnt}, time_forecast={time_cnt}")

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1

    print("OK: ML 预测表结构校验通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
