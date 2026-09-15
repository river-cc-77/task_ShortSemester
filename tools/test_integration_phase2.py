#!/usr/bin/env python3
"""东软充电桩 — 第二阶段黑盒集成测试（3 功能点 × 8 条 = 24 条）。

用法:
  # 模块1+3 仅需本地 DB 与 ml 产出（无需 server / Flask）
  python3 tools/test_integration_phase2.py

  # 含 Dashboard API（需先启动 Flask）
  python3 tools/test_integration_phase2.py --with-dashboard

  # 含 HDFS 远程校验（答辩机 node100）
  python3 tools/test_integration_phase2.py --with-hdfs

  # 全部
  python3 tools/test_integration_phase2.py --with-dashboard --with-hdfs

与 tools/testcase_catalog_phase2.py / 04测试用例-第二阶段.xls 对齐（TC-P2-01～24）。
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ML = ROOT / "ml"
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ML))

from testcase_catalog_phase2 import MODULES, TOTAL_CASES  # noqa: E402
from common import HDFS_MIRROR, HORIZONS, OUTPUT_DIR, connect_db, resolve_db_path  # noqa: E402


def fail(case_id: str, label: str, detail: str) -> None:
    raise RuntimeError(f"{case_id} {label}: {detail}")


def pass_case(case_id: str, label: str, detail: str = "OK") -> None:
    print(f"PASS {case_id} {label} — {detail}")


def read_json(path: Path) -> dict:
    if not path.is_file():
        fail("", str(path), "文件不存在")
    return json.loads(path.read_text(encoding="utf-8"))


def fetch_json(base: str, path: str) -> dict | list:
    url = base.rstrip("/") + path
    try:
        with urllib.request.urlopen(url, timeout=8) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        fail("", path, f"无法访问 {url} ({exc})，请先 python3 dashboard/app.py")


def run_module_ml(conn) -> None:
    """功能点1：ML 预测与模型评估（TC-P2-01～08）。"""
    hourly = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    if hourly <= 0:
        fail("TC-P2-01", "ads 小时表非空", f"count={hourly}")
    pass_case("TC-P2-01", "ads 小时表非空", f"rows={hourly}")

    daily_ok = conn.execute(
        "SELECT COUNT(*) FROM ads_daily_stats WHERE order_count > 0"
    ).fetchone()[0]
    if daily_ok <= 0:
        fail("TC-P2-02", "ads 日表有真实订单日", "无 order_count>0 的行")
    pass_case("TC-P2-02", "ads 日表有真实订单日", f"days={daily_ok}")

    stations = conn.execute("SELECT COUNT(*) FROM station").fetchone()[0]
    expected = stations * len(HORIZONS)
    load_cnt = conn.execute("SELECT COUNT(*) FROM load_forecast").fetchone()[0]
    time_cnt = conn.execute("SELECT COUNT(*) FROM time_forecast").fetchone()[0]
    if load_cnt != expected:
        fail("TC-P2-03", "负荷预测行数", f"{load_cnt} != {expected}")
    pass_case("TC-P2-03", "负荷预测行数", str(load_cnt))
    if time_cnt != expected:
        fail("TC-P2-04", "时长预测行数", f"{time_cnt} != {expected}")
    pass_case("TC-P2-04", "时长预测行数", str(time_cnt))

    bad_idle = conn.execute(
        "SELECT COUNT(*) FROM load_forecast WHERE predicted_idle_piles < 0"
    ).fetchone()[0]
    if bad_idle:
        fail("TC-P2-05", "空闲桩预测非负", f"bad={bad_idle}")
    pass_case("TC-P2-05", "空闲桩预测非负")

    bad_peak = conn.execute(
        "SELECT COUNT(*) FROM time_forecast "
        "WHERE predicted_peak_hour IS NOT NULL "
        "AND (predicted_peak_hour < 0 OR predicted_peak_hour > 23)"
    ).fetchone()[0]
    if bad_peak:
        fail("TC-P2-06", "高峰小时合法", f"bad={bad_peak}")
    pass_case("TC-P2-06", "高峰小时合法")

    zero_load = conn.execute(
        "SELECT COUNT(*) FROM load_forecast WHERE predicted_load > 0"
    ).fetchone()[0]
    zero_dur = conn.execute(
        "SELECT COUNT(*) FROM time_forecast WHERE predicted_avg_duration_min > 0"
    ).fetchone()[0]
    if load_cnt > 0 and zero_load == 0:
        fail("TC-P2-07", "预测非全零", "load_forecast 全为 0")
    if time_cnt > 0 and zero_dur == 0:
        fail("TC-P2-07", "预测非全零", "time_forecast 全为 0")
    pass_case("TC-P2-07", "预测非全零", f"load+={zero_load}, time+={zero_dur}")

    eval_path = OUTPUT_DIR / "evaluation.json"
    ev = read_json(eval_path)
    if "model" not in ev:
        fail("TC-P2-08", "模型评估指标", "缺少 model")
    for block in ("load_kwh", "duration_min"):
        if block not in ev:
            fail("TC-P2-08", "模型评估指标", f"缺少 {block}")
        for metric in ("mae", "rmse"):
            if metric not in ev[block]:
                fail("TC-P2-08", "模型评估指标", f"{block} 缺少 {metric}")
    pass_case("TC-P2-08", "模型评估指标", ev.get("model", ""))


def run_module_dashboard(base: str) -> None:
    """功能点2：Dashboard Flask API（TC-P2-09～16）。"""
    kpi = fetch_json(base, "/api/kpi")
    if "station_count" not in kpi or "pile_count" not in kpi:
        fail("TC-P2-09", "平台 KPI", f"keys={list(kpi.keys())}")
    pass_case("TC-P2-09", "平台 KPI", f"stations={kpi.get('station_count')}")

    trend = fetch_json(base, "/api/revenue_trend")
    if not isinstance(trend, list) or not trend:
        fail("TC-P2-10", "营收趋势", "空列表")
    if "stat_date" not in trend[0] or "total_revenue" not in trend[0]:
        fail("TC-P2-10", "营收趋势", f"字段缺失 {trend[0].keys()}")
    pass_case("TC-P2-10", "营收趋势", f"rows={len(trend)}")

    hourly = fetch_json(base, "/api/station_hourly_today")
    if not isinstance(hourly, list) or not hourly:
        fail("TC-P2-11", "充电高峰曲线", "空列表")
    hours = {int(r["stat_hour"]) for r in hourly if "stat_hour" in r}
    if not hours:
        fail("TC-P2-11", "充电高峰曲线", "无 stat_hour")
    pass_case("TC-P2-11", "充电高峰曲线", f"hours={len(hours)}")

    history = fetch_json(base, "/api/hourly_history")
    if not isinstance(history, list) or len(history) < 1:
        fail("TC-P2-12", "24h 历史分布", "空列表")
    pass_case("TC-P2-12", "24h 历史分布", f"rows={len(history)}")

    ww = fetch_json(base, "/api/weekday_weekend")
    types = {r.get("day_type") or r.get("label") for r in ww}
    if not types & {"weekday", "weekend"}:
        fail("TC-P2-13", "工作日周末对比", f"got={types}")
    pass_case("TC-P2-13", "工作日周末对比", str(types))

    rank = fetch_json(base, "/api/station_rank")
    if not isinstance(rank, list) or len(rank) > 10:
        fail("TC-P2-14", "电站排行", f"len={len(rank) if isinstance(rank, list) else type(rank)}")
    if rank and "name" not in rank[0]:
        fail("TC-P2-14", "电站排行", "缺少 name")
    pass_case("TC-P2-14", "电站排行", f"rows={len(rank)}")

    load = fetch_json(base, "/api/load_forecast")
    horizons = {r.get("horizon") for r in load} if isinstance(load, list) else set()
    if not {"1h", "6h", "24h"}.issubset(horizons):
        fail("TC-P2-15", "负荷预测 API", f"horizons={horizons}")
    pass_case("TC-P2-15", "负荷预测 API", str(horizons))

    ml_ev = fetch_json(base, "/api/ml_evaluation")
    if "model" not in ml_ev:
        fail("TC-P2-16", "模型评估 API", f"keys={list(ml_ev.keys())}")
    pass_case("TC-P2-16", "模型评估 API", ml_ev.get("model", ""))


def _csv_has_rows(path: Path) -> int:
    if not path.is_file():
        return 0
    with path.open(encoding="utf-8") as fp:
        return max(sum(1 for _ in fp) - 1, 0)


def _analytics_csv_dir(name: str) -> Path | None:
    d = OUTPUT_DIR / "analytics" / name
    if not d.is_dir():
        return None
    for p in d.glob("*.csv"):
        if p.name != "_SUCCESS":
            return p
    for sub in d.iterdir():
        if sub.is_dir():
            for p in sub.glob("*.csv"):
                return p
    return None


def run_module_hadoop(with_hdfs: bool) -> None:
    """功能点3：Hadoop/Spark 数据链路（TC-P2-17～24）。"""
    csv_paths = {
        "station_hourly": HDFS_MIRROR / "charging/dws/station_hourly/station_hourly.csv",
        "station_daily": HDFS_MIRROR / "charging/dws/station_daily/station_daily.csv",
        "station": HDFS_MIRROR / "charging/dim/station/station.csv",
        "pile": HDFS_MIRROR / "charging/dim/pile/pile.csv",
    }
    for name, path in csv_paths.items():
        if not path.is_file():
            fail("TC-P2-17", "HDFS 本地镜像四表", f"缺少 {path.name}")
    pass_case("TC-P2-17", "HDFS 本地镜像四表")

    h_rows = _csv_has_rows(csv_paths["station_hourly"])
    if h_rows <= 0:
        fail("TC-P2-18", "小时表 CSV 有数据", "0 行")
    pass_case("TC-P2-18", "小时表 CSV 有数据", f"rows={h_rows}")

    d_rows = _csv_has_rows(csv_paths["station_daily"])
    if d_rows <= 0:
        fail("TC-P2-19", "日表 CSV 有数据", "0 行")
    pass_case("TC-P2-19", "日表 CSV 有数据", f"rows={d_rows}")

    if _csv_has_rows(csv_paths["station"]) <= 0 or _csv_has_rows(csv_paths["pile"]) <= 0:
        fail("TC-P2-20", "维表 CSV 完整", "station 或 pile 为空")
    pass_case("TC-P2-20", "维表 CSV 完整")

    analytics_root = OUTPUT_DIR / "analytics"
    dims = [p.name for p in analytics_root.iterdir() if p.is_dir()] if analytics_root.is_dir() else []
    if len(dims) < 8:
        fail("TC-P2-21", "PySpark 分析维度", f"仅 {len(dims)} 个: {dims}")
    pass_case("TC-P2-21", "PySpark 分析维度", f"count={len(dims)}")

    ww_csv = _analytics_csv_dir("weekday_weekend")
    if ww_csv is None:
        fail("TC-P2-22", "交叉对比-工作日周末", "无 CSV")
    text = ww_csv.read_text(encoding="utf-8", errors="ignore").lower()
    if "weekday" not in text and "weekend" not in text:
        fail("TC-P2-22", "交叉对比-工作日周末", "CSV 无 weekday/weekend")
    pass_case("TC-P2-22", "交叉对比-工作日周末")

    matrix_csv = _analytics_csv_dir("station_hour_matrix")
    if matrix_csv is None:
        fail("TC-P2-23", "交叉对比-电站×小时", "无 CSV")
    header = matrix_csv.read_text(encoding="utf-8", errors="ignore").splitlines()[0].lower()
    if "stat_hour" not in header and "station" not in header:
        fail("TC-P2-23", "交叉对比-电站×小时", f"header={header}")
    pass_case("TC-P2-23", "交叉对比-电站×小时")

    if not with_hdfs:
        print("SKIP TC-P2-24 HDFS 远程目录（加 --with-hdfs 启用）")
        return

    proc = subprocess.run(
        ["hdfs", "dfs", "-ls", "/charging/"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        fail("TC-P2-24", "HDFS 远程目录", proc.stderr.strip() or "hdfs dfs 失败")
    out = proc.stdout
    if "dws" not in out and "station_hourly" not in out:
        fail("TC-P2-24", "HDFS 远程目录", f"未见 dws: {out[:200]}")
    pass_case("TC-P2-24", "HDFS 远程目录", "/charging/ 可访问")


def print_catalog() -> None:
    print(f"\n{MODULES[0]['module']} / {MODULES[1]['module']} / {MODULES[2]['module']}")
    print(f"共 {TOTAL_CASES} 条用例\n")
    for mod in MODULES:
        print(f"【{mod['module']}】{mod['feature']}")
        print(f"  前置: {mod['precondition']}\n")
        for cid, title, inp, expect, note in mod["cases"]:
            print(f"  {cid}\t{title}\t输入:{inp}\t预期:{expect}\t({note})")
        print()


def main() -> int:
    parser = argparse.ArgumentParser(description="第二阶段 3×8 集成测试")
    parser.add_argument("--with-dashboard", action="store_true", help="执行 TC-P2-09～16（需 Flask）")
    parser.add_argument("--with-hdfs", action="store_true", help="执行 TC-P2-24（需 hdfs 命令）")
    parser.add_argument("--print-catalog", action="store_true", help="仅打印用例表（填 Excel）")
    parser.add_argument("--dashboard-url", default=os.environ.get("DASHBOARD_URL", "http://127.0.0.1:5000"))
    args = parser.parse_args()

    if args.print_catalog:
        print_catalog()
        return 0

    db_path = resolve_db_path()
    if not db_path.is_file():
        print(f"FAIL: 数据库不存在 {db_path}", file=sys.stderr)
        print("请先: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql", file=sys.stderr)
        print("然后: bash ml/run_pipeline.sh --generate 3000", file=sys.stderr)
        return 1

    print(f"DB: {db_path}")
    conn = connect_db(db_path)
    try:
        run_module_ml(conn)
        run_module_hadoop(args.with_hdfs)
    finally:
        conn.close()

    if args.with_dashboard:
        run_module_dashboard(args.dashboard_url)
    else:
        print("SKIP TC-P2-09～16 Dashboard API（加 --with-dashboard 启用）")

    print(f"\n========== ALL {TOTAL_CASES} PHASE2 TESTS PASSED ==========")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nTEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
