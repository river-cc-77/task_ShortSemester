#!/usr/bin/env python3
"""东软充电桩 — 数据可视化大屏（Flask API + Vue3/DataV 前端）。

用法:
  pip install -r dashboard/requirements.txt
  cd dashboard-web && npm install && npm run build   # 首次构建 Vue 大屏
  python3 dashboard/app.py

环境变量:
  CHARGE_DB=/path/to/charge.db
  DASHBOARD_PORT=5000
"""

from __future__ import annotations

import json
import os
from datetime import datetime

import sys
from pathlib import Path

from flask import Flask, jsonify, render_template, send_from_directory

sys.path.insert(0, str(Path(__file__).resolve().parent))
from db import connect, rows_to_dicts, sanitize_row, sanitize_value

DASH_DIR = Path(__file__).resolve().parent
DIST_DIR = DASH_DIR / "static" / "dist"
ML_OUTPUT = DASH_DIR.parent / "ml" / "output"

app = Flask(__name__)


@app.route("/")
def index():
    vue_index = DIST_DIR / "index.html"
    if vue_index.is_file():
        return send_from_directory(DIST_DIR, "index.html")
    return render_template("index.html")


@app.route("/assets/<path:filename>")
def vue_assets(filename: str):
    assets_dir = DIST_DIR / "assets"
    if assets_dir.is_dir():
        return send_from_directory(assets_dir, filename)
    return ("", 404)


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
    latest = sanitize_row(dict(row)) if row else {}
    return jsonify(
        {
            "latest_daily": latest,
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


def _hourly_from_orders(conn, stat_date: str | None = None):
    """ads_station_hourly 为空时，从 charge_order 按小时聚合。"""
    if stat_date is None:
        row = conn.execute(
            """
            SELECT MAX(date(start_at))
            FROM charge_order
            WHERE start_at IS NOT NULL AND status IN ('已完成', '待支付')
            """
        ).fetchone()
        stat_date = row[0] if row else None
    if not stat_date:
        return []
    return conn.execute(
        """
        SELECT CAST(strftime('%H', start_at) AS INTEGER) AS stat_hour,
               COALESCE(SUM(kwh), 0) AS kwh,
               COUNT(*) AS orders
        FROM charge_order
        WHERE date(start_at) = ?
          AND start_at IS NOT NULL
          AND status IN ('已完成', '待支付')
        GROUP BY stat_hour
        ORDER BY stat_hour
        """,
        (stat_date,),
    ).fetchall()


def _rank_from_orders(conn):
    """ads_station_daily 为空时，从 charge_order 按站汇总。"""
    return conn.execute(
        """
        SELECT s.name,
               COUNT(*) AS orders,
               COALESCE(SUM(o.amount), 0) AS revenue,
               COALESCE(SUM(o.kwh), 0) AS kwh,
               0.0 AS utilization,
               NULL AS peak_hour,
               0.0 AS fault_rate
        FROM charge_order o
        JOIN station s ON s.id = o.station_id
        WHERE o.status IN ('已完成', '待支付')
        GROUP BY s.id, s.name
        ORDER BY revenue DESC
        LIMIT 10
        """
    ).fetchall()


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
    if not rows:
        rows = _hourly_from_orders(conn)
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
    if not rows:
        rows = _rank_from_orders(conn)
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/hourly_history")
def api_hourly_history():
    """近 30 天按小时聚合的历史充电量分布。"""
    conn = connect()
    rows = conn.execute(
        """
        SELECT stat_hour, SUM(kwh) AS kwh, SUM(orders) AS orders
        FROM ads_hourly_stats
        WHERE stat_date >= date('now', '-30 day')
        GROUP BY stat_hour
        ORDER BY stat_hour
        """
    ).fetchall()
    if not rows:
        rows = conn.execute(
            """
            SELECT stat_hour, SUM(kwh) AS kwh, SUM(orders) AS orders
            FROM ads_station_hourly
            WHERE stat_date >= date('now', '-30 day')
            GROUP BY stat_hour
            ORDER BY stat_hour
            """
        ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/weekday_weekend")
def api_weekday_weekend():
    """工作日 vs 周末充电对比（近 30 天）。"""
    conn = connect()
    rows = conn.execute(
        """
        SELECT
            CASE
                WHEN CAST(strftime('%w', stat_date) AS INTEGER) IN (0, 6) THEN 'weekend'
                ELSE 'weekday'
            END AS day_type,
            SUM(total_kwh) AS kwh,
            SUM(order_count) AS orders,
            SUM(total_revenue) AS revenue
        FROM ads_daily_stats
        WHERE stat_date >= date('now', '-30 day')
        GROUP BY day_type
        """
    ).fetchall()
    conn.close()
    data = rows_to_dicts(rows)
    label_map = {"weekday": "工作日", "weekend": "周末"}
    for row in data:
        row["label"] = label_map.get(row.get("day_type"), row.get("day_type"))
    return jsonify(data)


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


@app.route("/api/station_util")
def api_station_util():
    """各站近 30 日平均利用率（雷达图）。"""
    conn = connect()
    rows = conn.execute(
        """
        SELECT s.name, AVG(d.utilization) AS avg_util, AVG(d.turnover) AS avg_turnover,
               AVG(d.fault_rate) AS fault_rate
        FROM ads_station_daily d
        JOIN station s ON s.id = d.station_id
        WHERE d.stat_date >= date('now', '-30 day')
        GROUP BY s.id, s.name
        ORDER BY avg_util DESC
        LIMIT 8
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/region_stats")
def api_region_stats():
    """区域分布（从地址提取区名）。"""
    conn = connect()
    rows = conn.execute(
        """
        SELECT
            CASE
                WHEN instr(s.address, '区') > 0
                THEN substr(s.address, 1, instr(s.address, '区'))
                ELSE '其他'
            END AS region,
            SUM(d.orders) AS orders,
            SUM(d.revenue) AS revenue,
            SUM(d.kwh) AS kwh
        FROM ads_station_daily d
        JOIN station s ON s.id = d.station_id
        WHERE d.stat_date >= date('now', '-30 day')
        GROUP BY region
        ORDER BY revenue DESC
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/station_hour_matrix")
def api_station_hour_matrix():
    """交叉对比：电站 × 小时热力矩阵。"""
    conn = connect()
    rows = conn.execute(
        """
        SELECT s.name AS station_name, h.stat_hour, SUM(h.kwh) AS kwh, SUM(h.orders) AS orders
        FROM ads_station_hourly h
        JOIN station s ON s.id = h.station_id
        WHERE h.stat_date >= date('now', '-30 day')
        GROUP BY s.name, h.stat_hour
        ORDER BY s.name, h.stat_hour
        """
    ).fetchall()
    conn.close()
    return jsonify(rows_to_dicts(rows))


@app.route("/api/ml_evaluation")
def api_ml_evaluation():
    """WMA 模型离线评估指标（ml/evaluate.py 产出）。"""
    path = ML_OUTPUT / "evaluation.json"
    if path.is_file():
        return jsonify(sanitize_value(json.loads(path.read_text(encoding="utf-8"))))
    return jsonify(
        {
            "model": "WMA",
            "message": "尚未评估，请运行: python ml/evaluate.py",
            "load_kwh": {"mae": None, "rmse": None, "mape_pct": None},
            "duration_min": {"mae": None, "rmse": None, "mape_pct": None},
        }
    )


if __name__ == "__main__":
    port = int(os.environ.get("DASHBOARD_PORT", "5000"))
    app.run(host="0.0.0.0", port=port, debug=False)
