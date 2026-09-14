#!/usr/bin/env python3
"""离线校验 ML 预测结果（无需启动 server）。"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import HORIZONS, connect_db, ensure_schema, resolve_db_path


def main() -> int:
    db_path = resolve_db_path()
    conn = connect_db(db_path)
    ensure_schema(conn)  # 旧库可能缺预测表，先补齐再校验（否则直接 traceback）
    errors: list[str] = []
    warnings: list[str] = []

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

    # 只校验行数与取值范围时，一整表全 0 的预测也会打印 OK——而"全 0"恰是本项目最怕的失效形态
    zero_load = conn.execute(
        "SELECT COUNT(*) FROM load_forecast WHERE predicted_load > 0"
    ).fetchone()[0]
    if load_cnt > 0 and zero_load == 0:
        errors.append("load_forecast 的 predicted_load 全为 0（ads_* 可能只有当天占位行）")

    zero_dur = conn.execute(
        "SELECT COUNT(*) FROM time_forecast WHERE predicted_avg_duration_min > 0"
    ).fetchone()[0]
    if time_cnt > 0 and zero_dur == 0:
        errors.append("time_forecast 的 predicted_avg_duration_min 全为 0")

    latest = conn.execute(
        "SELECT MAX(stat_date) FROM ads_daily_stats WHERE order_count > 0"
    ).fetchone()[0]
    if latest is None:
        errors.append("ads_daily_stats 没有任何 order_count > 0 的日期（大屏 KPI 会为空）")
    else:
        # 最新业务日若明显落后于今天，说明 ads_* 没跟上最新订单
        behind = conn.execute(
            "SELECT CAST(julianday(date('now','localtime')) - julianday(?) AS INTEGER)", (latest,)
        ).fetchone()[0]
        if behind and behind > 1:
            warnings.append(f"ads_daily_stats 最新有单日为 {latest}，落后今天 {behind} 天，建议重跑 collector")

    conn.close()

    print(f"DB: {db_path}")
    print(f"  stations={stations}, ads_station_hourly={hourly}")
    print(f"  load_forecast={load_cnt}, time_forecast={time_cnt}")
    print(f"  最新有单业务日: {latest}")
    for warn in warnings:
        print(f"WARN: {warn}", file=sys.stderr)

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1

    print("OK: ML 预测表结构校验通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
