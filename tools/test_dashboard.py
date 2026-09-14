#!/usr/bin/env python3
"""Dashboard API 冒烟测试（无需启动 charge-server）。"""

from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "dashboard"))

from db import connect  # noqa: E402

BASE = "http://127.0.0.1:5000"
ENDPOINTS = (
    "/api/kpi",
    "/api/revenue_trend",
    "/api/pile_status",
    "/api/station_hourly_today",
    "/api/station_rank",
    "/api/load_forecast",
    "/api/time_forecast",
    "/api/hourly_history",
    "/api/weekday_weekend",
    "/api/station_util",
    "/api/region_stats",
    "/api/station_hour_matrix",
    "/api/ml_evaluation",
)


def check_db() -> None:
    conn = connect()
    tables = {
        "ads_daily_stats": conn.execute("SELECT COUNT(*) FROM ads_daily_stats").fetchone()[0],
        "load_forecast": conn.execute("SELECT COUNT(*) FROM load_forecast").fetchone()[0],
        "time_forecast": conn.execute("SELECT COUNT(*) FROM time_forecast").fetchone()[0],
    }
    conn.close()
    print("DB tables:", tables)
    if tables["load_forecast"] == 0 or tables["time_forecast"] == 0:
        raise RuntimeError("load/time_forecast 为空，请先运行: python ml/run_pipeline.py")


def fetch_json(path: str) -> dict | list:
    url = BASE + path
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        raise RuntimeError(
            f"无法访问 {url}，请先启动: python dashboard/app.py ({exc})"
        ) from exc


def main() -> int:
    check_db()
    for path in ENDPOINTS:
        data = fetch_json(path)
        if isinstance(data, list):
            print(f"OK {path} -> {len(data)} rows")
        else:
            print(f"OK {path} -> keys={list(data.keys())}")
    kpi = fetch_json("/api/kpi")
    if not kpi.get("latest_daily") and kpi.get("station_count", 0) > 0:
        print("WARN ads_daily_stats 为空，营收 KPI 将显示 '-'")
        print("     修复: python ml/bootstrap_ads.py  或 Linux 下跑 ads-collector")
    print("\nAll dashboard API checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
