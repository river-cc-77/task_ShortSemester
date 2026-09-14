#!/usr/bin/env python3
"""本地预测引擎（纯 Python + SQLite，与 Spark SQL 算法一致）。

算法（加权移动平均 WMA）:
  - 负荷/时长: 过去 N 天同 hour 的加权平均，权重 w = 1/(days_ago+1)，近期权重更大
  - 空闲桩: total_piles - CEIL(predicted_load / avg_power_kw)
  - 高峰小时: ads_station_daily.peak_hour，缺失时用 orders 最大的 hour

用法:
  python ml/predict_local.py
"""

from __future__ import annotations

import math
import sys
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import (
    HISTORY_DAYS,
    HORIZONS,
    OUTPUT_DIR,
    connect_db,
    ensure_schema,
    forecast_target,
    format_hour,
    now_local,
    resolve_db_path,
    wma_weight,
)
from features import duration_adjustment_factor, feature_tags, load_adjustment_factor


def fetch_station_meta(conn) -> dict[int, dict[str, float]]:
    rows = conn.execute(
        """
        SELECT station_id,
               COUNT(*) AS total_piles,
               AVG(power_kw) AS avg_power_kw
        FROM pile
        GROUP BY station_id
        """
    ).fetchall()
    return {
        int(row["station_id"]): {
            "total_piles": int(row["total_piles"]),
            "avg_power_kw": float(row["avg_power_kw"] or 0),
        }
        for row in rows
    }


def wma_same_hour(
    conn,
    station_id: int,
    stat_hour: int,
    column: str,
    *,
    positive_only: bool = False,
    ref_date: date | None = None,
) -> float:
    """同 hour 加权移动平均：w_i = 1 / (days_ago + 1)。

    窗口为 [ref_date - HISTORY_DAYS, ref_date)，**不含 ref_date 当天**——
    当天可能只过了一半，甚至只有 collector/bootstrap 物化出来的全 0 占位行，
    而它的权重最大（days_ago=0 -> w=1.0），会把预测系统性拉低约 37%。
    口径与 evaluate.wma_backtest 保持一致。
    """
    ref_date = ref_date or date.today()
    window_start = (ref_date - timedelta(days=HISTORY_DAYS)).isoformat()
    rows = conn.execute(
        f"""
        SELECT stat_date, {column} AS v
        FROM ads_station_hourly
        WHERE station_id = ?
          AND stat_hour = ?
          AND stat_date < ?
          AND stat_date >= ?
        """,
        (station_id, stat_hour, ref_date.isoformat(), window_start),
    ).fetchall()

    weighted_sum = 0.0
    weight_sum = 0.0
    for row in rows:
        value = row["v"]
        if value is None:
            continue
        if positive_only and float(value) <= 0:
            continue
        stat_date = date.fromisoformat(str(row["stat_date"]))
        days_ago = (ref_date - stat_date).days
        weight = wma_weight(days_ago)
        if weight <= 0:
            continue
        weighted_sum += weight * float(value)
        weight_sum += weight

    if weight_sum <= 0:
        return 0.0
    return round(weighted_sum / weight_sum, 2)


def resolve_peak_hour(conn, station_id: int) -> int | None:
    """高峰小时；无任何历史订单时返回 None（schema 约定：无历史为 NULL）。

    注意第一分支的 `peak_hour IS NOT NULL` 天然跳过「当日无单」的全 0 行，
    不要改成 MAX(stat_date)——那会命中占位行。
    """
    row = conn.execute(
        """
        SELECT peak_hour
        FROM ads_station_daily
        WHERE station_id = ? AND peak_hour IS NOT NULL
        ORDER BY stat_date DESC
        LIMIT 1
        """,
        (station_id,),
    ).fetchone()
    if row and row["peak_hour"] is not None:
        return int(row["peak_hour"])

    # 网格里每站每天 24 行恒存在，故必须用 HAVING 排除全 0 占位，
    # 否则 GROUP BY 一定返回一行，且按 stat_hour ASC 取到 0 -> 误报 00:00 为高峰。
    row = conn.execute(
        """
        SELECT stat_hour
        FROM ads_station_hourly
        WHERE station_id = ?
        GROUP BY stat_hour
        HAVING SUM(orders) > 0
        ORDER BY AVG(orders) DESC, stat_hour ASC
        LIMIT 1
        """,
        (station_id,),
    ).fetchone()
    if row:
        return int(row["stat_hour"])
    return None


def predict_idle_piles(predicted_load: float, total_piles: int, avg_power_kw: float) -> int:
    if avg_power_kw <= 0:
        return total_piles
    busy = math.ceil(predicted_load / avg_power_kw)
    return max(0, total_piles - busy)


def run_predict(conn, now: datetime | None = None) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    now = now or now_local()
    created_at = now.strftime("%Y-%m-%d %H:%M:%S")
    station_meta = fetch_station_meta(conn)
    station_ids = [row[0] for row in conn.execute("SELECT id FROM station ORDER BY id").fetchall()]

    load_rows: list[dict[str, Any]] = []
    time_rows: list[dict[str, Any]] = []

    for station_id in station_ids:
        meta = station_meta.get(station_id, {"total_piles": 0, "avg_power_kw": 0.0})
        peak_hour = resolve_peak_hour(conn, station_id)

        for horizon in HORIZONS:
            target = forecast_target(now, horizon)
            target_hour = target.hour
            forecast_hour = format_hour(target)

            base_load = wma_same_hour(conn, station_id, target_hour, "kwh", ref_date=now.date())
            load_factor = load_adjustment_factor(target)
            predicted_load = round(base_load * load_factor, 2)
            predicted_idle = predict_idle_piles(
                predicted_load,
                int(meta["total_piles"]),
                float(meta["avg_power_kw"]),
            )
            base_duration = wma_same_hour(
                conn,
                station_id,
                target_hour,
                "duration_min",
                positive_only=True,
                ref_date=now.date(),
            )
            predicted_duration = round(base_duration * duration_adjustment_factor(target), 2)
            _ = feature_tags(target)  # 天气/节假日特征已融入 predicted_* 数值

            load_rows.append(
                {
                    "station_id": station_id,
                    "forecast_hour": forecast_hour,
                    "predicted_load": predicted_load,
                    "predicted_idle_piles": predicted_idle,
                    "horizon": horizon,
                    "created_at": created_at,
                }
            )
            time_rows.append(
                {
                    "station_id": station_id,
                    "forecast_hour": forecast_hour,
                    "predicted_avg_duration_min": predicted_duration,
                    "predicted_peak_hour": peak_hour,
                    "horizon": horizon,
                    "created_at": created_at,
                }
            )

    return load_rows, time_rows


def write_sqlite(conn, load_rows, time_rows) -> None:
    ensure_schema(conn)  # 旧库可能缺 time_forecast（见 common.ensure_schema）
    conn.execute("DELETE FROM load_forecast")
    conn.execute("DELETE FROM time_forecast")

    conn.executemany(
        """
        INSERT INTO load_forecast
            (station_id, forecast_hour, predicted_load, predicted_idle_piles, horizon, created_at)
        VALUES (:station_id, :forecast_hour, :predicted_load, :predicted_idle_piles, :horizon, :created_at)
        """,
        load_rows,
    )
    conn.executemany(
        """
        INSERT INTO time_forecast
            (station_id, forecast_hour, predicted_avg_duration_min, predicted_peak_hour, horizon, created_at)
        VALUES (:station_id, :forecast_hour, :predicted_avg_duration_min, :predicted_peak_hour, :horizon, :created_at)
        """,
        time_rows,
    )
    conn.commit()


def write_csv(load_rows, time_rows) -> None:
    import csv

    if not load_rows:
        return

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, rows in (
        ("load_forecast.csv", load_rows),
        ("time_forecast.csv", time_rows),
    ):
        path = OUTPUT_DIR / name
        with path.open("w", newline="", encoding="utf-8") as fp:
            writer = csv.DictWriter(fp, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print(f"  CSV -> {path}")


def main() -> int:
    db_path = resolve_db_path()
    conn = connect_db(db_path)

    hourly_count = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    if hourly_count == 0:
        print("警告: ads_station_hourly 为空，请先运行 collector", file=sys.stderr)
        print("  cd collector && ./ads-collector", file=sys.stderr)

    load_rows, time_rows = run_predict(conn)
    write_sqlite(conn, load_rows, time_rows)
    write_csv(load_rows, time_rows)
    conn.close()

    print(f"预测完成: load_forecast {len(load_rows)} 行, time_forecast {len(time_rows)} 行 -> {db_path}")
    print("已应用天气/节假日/周末修正因子（见 ml/features.py）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
