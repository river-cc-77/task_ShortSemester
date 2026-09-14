#!/usr/bin/env python3
"""将 Spark SQL 导出的预测 CSV 同步回 SQLite load_forecast / time_forecast。

用法:
  python ml/sync_to_sqlite.py
  python ml/sync_to_sqlite.py --load ml/output/load_forecast.csv --time ml/output/time_forecast.csv
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import OUTPUT_DIR, VALID_HORIZONS, connect_db, ensure_schema, resolve_db_path


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FileNotFoundError(f"找不到 CSV: {path}")
    with path.open(newline="", encoding="utf-8") as fp:
        return list(csv.DictReader(fp))


def check_rows(rows: list[dict[str, str]], label: str) -> None:
    if not rows:
        raise SystemExit(f"{label} CSV 没有任何数据行，已中止以免清空预测表")
    for i, row in enumerate(rows, start=2):  # 第 1 行是表头
        horizon = row.get("horizon", "")
        if horizon not in VALID_HORIZONS:
            raise SystemExit(
                f"{label} CSV 第 {i} 行 horizon={horizon!r} 非法，"
                f"只允许 {sorted(VALID_HORIZONS)}（会撞 schema 的 CHECK 约束）"
            )


def sync(load_file: Path, time_file: Path) -> None:
    db_path = resolve_db_path()
    conn = connect_db(db_path)
    ensure_schema(conn)

    load_rows = read_csv(load_file)
    time_rows = read_csv(time_file)
    check_rows(load_rows, "load_forecast")
    check_rows(time_rows, "time_forecast")

    conn.execute("DELETE FROM load_forecast")
    conn.execute("DELETE FROM time_forecast")

    for row in load_rows:
        conn.execute(
            """
            INSERT INTO load_forecast
                (station_id, forecast_hour, predicted_load, predicted_idle_piles, horizon, created_at)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                int(row["station_id"]),
                row["forecast_hour"],
                float(row["predicted_load"]),
                int(float(row["predicted_idle_piles"])),
                row["horizon"],
                row.get("created_at") or row["forecast_hour"],
            ),
        )

    for row in time_rows:
        peak = row.get("predicted_peak_hour")
        conn.execute(
            """
            INSERT INTO time_forecast
                (station_id, forecast_hour, predicted_avg_duration_min, predicted_peak_hour, horizon, created_at)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                int(row["station_id"]),
                row["forecast_hour"],
                float(row["predicted_avg_duration_min"]),
                int(peak) if peak not in (None, "", "null", "NULL") else None,
                row["horizon"],
                row.get("created_at") or row["forecast_hour"],
            ),
        )

    conn.commit()
    conn.close()
    print(f"已同步 {len(load_rows)} 条负荷预测、{len(time_rows)} 条时间预测 -> {db_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Spark 预测结果回写 SQLite")
    parser.add_argument("--load", type=Path, default=OUTPUT_DIR / "load_forecast.csv")
    parser.add_argument("--time", type=Path, default=OUTPUT_DIR / "time_forecast.csv")
    args = parser.parse_args()

    sync(args.load, args.time)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
