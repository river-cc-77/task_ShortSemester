#!/usr/bin/env python3
"""东软充电桩 — 数据可视化大屏（Flask + ECharts）。

用法:
  pip install -r dashboard/requirements.txt
  python3 dashboard/app.py

环境变量:
  CHARGE_DB=/path/to/charge.db
  DASHBOARD_PORT=5000
"""

from __future__ import annotations

import os
from datetime import datetime

import sys
from pathlib import Path

from flask import Flask, jsonify, render_template

sys.path.insert(0, str(Path(__file__).resolve().parent))
from db import connect, rows_to_dicts

app = Flask(__name__)


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/kpi")
def api_kpi():
    conn = connect()
    row = conn.execute(
        """
        SELECT stat_date, total_revenue, total_kwh, order_count, active_user_count,
               utilization, peak_hour
        FROM ads_daily_stats
        WHERE order_count > 0
        ORDER BY stat_date DESC LIMIT 1
        """
    ).fetchone()
    if row is None:
        row = conn.execute(
            """
            SELECT stat_date, total_revenue, total_kwh, order_count, active_user_count,
                   utilization, peak_hour
            FROM ads_daily_stats
            ORDER BY stat_date DESC LIMIT 1
            """
        ).fetchone()
    stations = conn.execute("SELECT COUNT(*) AS c FROM station").fetchone()["c"]
    piles = conn.execute("SELECT COUNT(*) AS c FROM pile").fetchone()["c"]
    idle = conn.execute("SELECT COUNT(*) AS c FROM pile WHERE status='闲置'").fetchone()["c"]
    conn.close()
    return jsonify(
        {
            "latest_daily": dict(row) if row else {},
            "station_count": stations,
            "pile_count": piles,
            "idle_piles": idle,
            "updated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }
    )


@app.route("/api/revenue_trend")
def api_revenue_trend():
    conn = connect()
    rows = conn.execute(
        """
        SELECT stat_date, total_revenue, order_count, total_kwh
        FROM ads_daily_stats
        ORDER BY stat_date DESC LIMIT 30
        """
    ).fetchall()
    conn.close()
    data = rows_to_dicts(rows)
    data.reverse()
    return jsonify(data)


@app.route("/api/load_forecast")
def api_load_forecast():
    conn = connect()
    rows = conn.execute(
        """
        SELECT f.station_id, s.name AS station_name, f.horizon, f.forecast_hour,
               f.predicted_load, f.predicted_idle_piles, f.created_at
        FROM load_forecast f
        JOIN station s ON s.id = f.station_id
        ORDER BY f.horizon, f.station_id
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/time_forecast")
def api_time_forecast():
    conn = connect()
    rows = conn.execute(
        """
        SELECT f.station_id, s.name AS station_name, f.horizon, f.forecast_hour,
               f.predicted_avg_duration_min, f.predicted_peak_hour, f.created_at
        FROM time_forecast f
        JOIN station s ON s.id = f.station_id
        ORDER BY f.horizon, f.station_id
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/station_hourly_today")
def api_station_hourly_today():
    conn = connect()
    today = datetime.now().strftime("%Y-%m-%d")
    rows = conn.execute(
        """
        SELECT h.stat_hour, SUM(h.kwh) AS kwh, SUM(h.orders) AS orders
        FROM ads_station_hourly h
        WHERE h.stat_date = ?
        GROUP BY h.stat_hour
        ORDER BY h.stat_hour
        """,
        (today,),
    ).fetchall()
    if not rows:
        rows = conn.execute(
            """
            SELECT h.stat_hour, SUM(h.kwh) AS kwh, SUM(h.orders) AS orders
            FROM ads_station_hourly h
            WHERE h.stat_date = (SELECT MAX(stat_date) FROM ads_station_hourly)
            GROUP BY h.stat_hour
            ORDER BY h.stat_hour
            """
        ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/station_rank")
def api_station_rank():
    conn = connect()
    rows = conn.execute(
        """
        SELECT s.name, d.orders, d.revenue, d.kwh, d.utilization, d.peak_hour, d.fault_rate
        FROM ads_station_daily d
        JOIN station s ON s.id = d.station_id
        WHERE d.stat_date = (SELECT MAX(stat_date) FROM ads_station_daily)
        ORDER BY d.revenue DESC
        LIMIT 10
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/pile_status")
def api_pile_status():
    conn = connect()
    rows = conn.execute(
        """
        SELECT status, COUNT(*) AS cnt
        FROM pile
        GROUP BY status
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


if __name__ == "__main__":
    port = int(os.environ.get("DASHBOARD_PORT", "5000"))
    app.run(host="0.0.0.0", port=port, debug=False)
