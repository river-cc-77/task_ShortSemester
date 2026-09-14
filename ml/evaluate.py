#!/usr/bin/env python3
"""ML 模型离线评估（WMA 负荷/时长预测 vs 实际值）。

留一日前向验证：对每个测试日，仅用该日之前的历史做同 hour 加权移动平均，
与当日实际 kwh / duration_min 对比，输出 MAE / RMSE / MAPE。

用法:
  python ml/evaluate.py
  python ml/evaluate.py --days 3

结果写入 ml/output/evaluation.json，供大屏 /api/ml_evaluation 读取。
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import date, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import HISTORY_DAYS, OUTPUT_DIR, connect_db, resolve_db_path, wma_weight


def wma_backtest(
    conn,
    station_id: int,
    stat_hour: int,
    column: str,
    ref_date: date,
    *,
    positive_only: bool = False,
) -> float:
    rows = conn.execute(
        f"""
        SELECT stat_date, {column} AS v
        FROM ads_station_hourly
        WHERE station_id = ?
          AND stat_hour = ?
          AND stat_date < ?
          AND stat_date >= date(?, '-{HISTORY_DAYS} day')
        """,
        (station_id, stat_hour, ref_date.isoformat(), ref_date.isoformat()),
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
    return round(weighted_sum / weight_sum, 4)


def mape(actuals: list[float], preds: list[float]) -> float:
    pairs = [(a, p) for a, p in zip(actuals, preds) if a > 0]
    if not pairs:
        return 0.0
    return round(sum(abs(a - p) / a for a, p in pairs) / len(pairs) * 100, 2)


def rmse(actuals: list[float], preds: list[float]) -> float:
    if not actuals:
        return 0.0
    return round(math.sqrt(sum((a - p) ** 2 for a, p in zip(actuals, preds)) / len(actuals)), 4)


def mae(actuals: list[float], preds: list[float]) -> float:
    if not actuals:
        return 0.0
    return round(sum(abs(a - p) for a, p in zip(actuals, preds)) / len(actuals), 4)


def evaluate_metric(conn, test_dates: list[date], column: str, *, positive_only: bool = False) -> dict:
    actuals: list[float] = []
    preds: list[float] = []
    stations = [row[0] for row in conn.execute("SELECT id FROM station ORDER BY id").fetchall()]

    for test_date in test_dates:
        for station_id in stations:
            for hour in range(24):
                row = conn.execute(
                    f"""
                    SELECT {column} AS v
                    FROM ads_station_hourly
                    WHERE station_id = ? AND stat_date = ? AND stat_hour = ?
                    """,
                    (station_id, test_date.isoformat(), hour),
                ).fetchone()
                if row is None or row["v"] is None:
                    continue
                actual = float(row["v"])
                if positive_only and actual <= 0:
                    continue
                pred = wma_backtest(
                    conn, station_id, hour, column, test_date, positive_only=positive_only
                )
                actuals.append(actual)
                preds.append(pred)

    return {
        "samples": len(actuals),
        "mae": mae(actuals, preds),
        "rmse": rmse(actuals, preds),
        "mape_pct": mape(actuals, preds),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="WMA 预测模型离线评估")
    parser.add_argument("--days", type=int, default=3, help="留几日前向验证（默认 3）")
    args = parser.parse_args()

    db_path = resolve_db_path()
    conn = connect_db(db_path)
    hourly_cnt = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    if hourly_cnt == 0:
        print("ads_station_hourly 为空，请先运行 collector / run_pipeline", file=sys.stderr)
        return 1

    max_date_row = conn.execute("SELECT MAX(stat_date) FROM ads_station_hourly").fetchone()
    max_date = date.fromisoformat(str(max_date_row[0]))
    test_dates = [max_date - timedelta(days=i) for i in range(args.days, 0, -1)]

    load_metrics = evaluate_metric(conn, test_dates, "kwh")
    duration_metrics = evaluate_metric(conn, test_dates, "duration_min", positive_only=True)
    conn.close()

    report = {
        "model": "WMA (weighted moving average, same-hour)",
        "history_days": HISTORY_DAYS,
        "test_dates": [d.isoformat() for d in test_dates],
        "load_kwh": load_metrics,
        "duration_min": duration_metrics,
    }

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUTPUT_DIR / "evaluation.json"
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"DB: {db_path}")
    print(f"测试日: {report['test_dates']}")
    print(
        f"负荷 kWh  — samples={load_metrics['samples']}, "
        f"MAE={load_metrics['mae']}, RMSE={load_metrics['rmse']}, MAPE={load_metrics['mape_pct']}%"
    )
    print(
        f"时长 min  — samples={duration_metrics['samples']}, "
        f"MAE={duration_metrics['mae']}, RMSE={duration_metrics['rmse']}, MAPE={duration_metrics['mape_pct']}%"
    )
    print(f"报告 -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
