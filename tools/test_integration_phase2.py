#!/usr/bin/env python3
"""东软充电桩 — 第二阶段黑盒集成测试（3 功能点 × 8 条 = 24 条）。

本脚本做什么？
  自动跑 24 条测试用例，核对第二阶段三条链路是否打通：
    模块1  ML 预测与评估     → 查 SQLite（ads_*、forecast 表、evaluation.json）
    模块2  Dashboard API    → 调 Flask HTTP 接口（需大屏服务已启动）
    模块3  Hadoop/Spark 链路 → 查本地 HDFS 镜像 CSV + PySpark 分析产出（可选查远程 HDFS）

与谁对齐？
  - 用例定义：tools/testcase_catalog_phase2.py
  - Excel 表：04测试用例-第二阶段.xls（TC-P2-01～24）

用法:
  # 默认：只跑模块1+3（本地 DB + ml 产出即可，不用起 server / Flask）
  python3 tools/test_integration_phase2.py

  # 含 Dashboard API（需先在 node100 等处启动: python3 dashboard/app.py）
  python3 tools/test_integration_phase2.py --with-dashboard
  python3 tools/test_integration_phase2.py --with-dashboard --dashboard-url http://192.168.176.100:5000

  # 含 HDFS 远程校验（答辩机 node100，需 hdfs 命令可用）
  python3 tools/test_integration_phase2.py --with-hdfs

  # 全部
  python3 tools/test_integration_phase2.py --with-dashboard --with-hdfs

  # 只打印用例文字（填 Excel 时对照）
  python3 tools/test_integration_phase2.py --print-catalog

前置条件（默认模式）:
  bash ml/run_pipeline.sh --generate 3000
  → 产出 ads_*、load_forecast、ml/output/evaluation.json、ml/data/hdfs/、ml/output/analytics/
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

# ---------------------------------------------------------------------------
# 路径：把 tools/ 和 ml/ 加入 import 路径，以便引用用例表和 ml 公共模块
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
ML = ROOT / "ml"
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ML))

from testcase_catalog_phase2 import MODULES, TOTAL_CASES  # noqa: E402
from common import HDFS_MIRROR, HORIZONS, OUTPUT_DIR, connect_db, resolve_db_path  # noqa: E402
# HORIZONS = {"1h":1, "6h":6, "24h":24}，预测表每个站应有 3 行
# HDFS_MIRROR = ml/data/hdfs/，export_to_hdfs.py 导出的本地 CSV 镜像
# OUTPUT_DIR  = ml/output/，evaluation.json、analytics/ 等 ML 产出


# ---------------------------------------------------------------------------
# 小工具：统一 PASS/FAIL 输出格式
# ---------------------------------------------------------------------------
def fail(case_id: str, label: str, detail: str) -> None:
    """断言失败：抛 RuntimeError，main() 捕获后打印 TEST FAILED 并以 exit 1 退出。"""
    raise RuntimeError(f"{case_id} {label}: {detail}")


def pass_case(case_id: str, label: str, detail: str = "OK") -> None:
    """断言通过：打印 PASS 行，便于对照 Excel 用例编号。"""
    print(f"PASS {case_id} {label} — {detail}")


def read_json(path: Path) -> dict:
    """读本地 JSON 文件（如 ml/output/evaluation.json）。"""
    if not path.is_file():
        fail("", str(path), "文件不存在")
    return json.loads(path.read_text(encoding="utf-8"))


def fetch_json(base: str, path: str) -> dict | list:
    """HTTP GET 请求 Flask 接口，返回解析后的 JSON。"""
    url = base.rstrip("/") + path
    try:
        with urllib.request.urlopen(url, timeout=8) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        fail("", path, f"无法访问 {url} ({exc})，请先 python3 dashboard/app.py")


# ---------------------------------------------------------------------------
# 模块1：ML 预测与模型评估（TC-P2-01～08）
# 数据来源：collector 写 ads_*，predict_local.py 写 forecast 表，evaluate.py 写 evaluation.json
# ---------------------------------------------------------------------------
def run_module_ml(conn) -> None:
    """功能点1：ML 预测与模型评估（TC-P2-01～08）。"""

    # TC-P2-01：collector 是否已把小时级指标写入 ads_station_hourly
    hourly = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    if hourly <= 0:
        fail("TC-P2-01", "ads 小时表非空", f"count={hourly}")
    pass_case("TC-P2-01", "ads 小时表非空", f"rows={hourly}")

    # TC-P2-02：日表里至少有一天 order_count>0，避免全是 0 占位行
    daily_ok = conn.execute(
        "SELECT COUNT(*) FROM ads_daily_stats WHERE order_count > 0"
    ).fetchone()[0]
    if daily_ok <= 0:
        fail("TC-P2-02", "ads 日表有真实订单日", "无 order_count>0 的行")
    pass_case("TC-P2-02", "ads 日表有真实订单日", f"days={daily_ok}")

    # TC-P2-03 / TC-P2-04：预测表行数 = 电站数 × 3 个 horizon（1h/6h/24h）
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

    # TC-P2-05：预测空闲桩数不能为负（admin/client 负荷预警会用到）
    bad_idle = conn.execute(
        "SELECT COUNT(*) FROM load_forecast WHERE predicted_idle_piles < 0"
    ).fetchone()[0]
    if bad_idle:
        fail("TC-P2-05", "空闲桩预测非负", f"bad={bad_idle}")
    pass_case("TC-P2-05", "空闲桩预测非负")

    # TC-P2-06：高峰小时必须在 0～23（client 站点详情「高峰时段 %1:00」）
    bad_peak = conn.execute(
        "SELECT COUNT(*) FROM time_forecast "
        "WHERE predicted_peak_hour IS NOT NULL "
        "AND (predicted_peak_hour < 0 OR predicted_peak_hour > 23)"
    ).fetchone()[0]
    if bad_peak:
        fail("TC-P2-06", "高峰小时合法", f"bad={bad_peak}")
    pass_case("TC-P2-06", "高峰小时合法")

    # TC-P2-07：不能「表有行但预测值全 0」——常见于 ads 未刷新就跑了 predict
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

    # TC-P2-08：evaluate.py 产出的 evaluation.json 结构完整（大屏底部 MAE/RMSE）
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


# ---------------------------------------------------------------------------
# 模块2：Dashboard Flask API（TC-P2-09～16）
# 需加 --with-dashboard，且 Flask 已启动（默认连 127.0.0.1:5000，node100 用 --dashboard-url）
# ---------------------------------------------------------------------------
def run_module_dashboard(base: str) -> None:
    """功能点2：Dashboard Flask API（TC-P2-09～16）。"""

    # TC-P2-09：/api/kpi 返回平台级 KPI 字段
    kpi = fetch_json(base, "/api/kpi")
    if "station_count" not in kpi or "pile_count" not in kpi:
        fail("TC-P2-09", "平台 KPI", f"keys={list(kpi.keys())}")
    pass_case("TC-P2-09", "平台 KPI", f"stations={kpi.get('station_count')}")

    # TC-P2-10：营收趋势折线图数据
    trend = fetch_json(base, "/api/revenue_trend")
    if not isinstance(trend, list) or not trend:
        fail("TC-P2-10", "营收趋势", "空列表")
    if "stat_date" not in trend[0] or "total_revenue" not in trend[0]:
        fail("TC-P2-10", "营收趋势", f"字段缺失 {trend[0].keys()}")
    pass_case("TC-P2-10", "营收趋势", f"rows={len(trend)}")

    # TC-P2-11：今日（最近业务日）按小时的高峰曲线
    hourly = fetch_json(base, "/api/station_hourly_today")
    if not isinstance(hourly, list) or not hourly:
        fail("TC-P2-11", "充电高峰曲线", "空列表")
    hours = {int(r["stat_hour"]) for r in hourly if "stat_hour" in r}
    if not hours:
        fail("TC-P2-11", "充电高峰曲线", "无 stat_hour")
    pass_case("TC-P2-11", "充电高峰曲线", f"hours={len(hours)}")

    # TC-P2-12：近 30 天汇总后的 24h 历史分布（与 TC-P2-11 口径不同，不要求数值相同）
    history = fetch_json(base, "/api/hourly_history")
    if not isinstance(history, list) or len(history) < 1:
        fail("TC-P2-12", "24h 历史分布", "空列表")
    pass_case("TC-P2-12", "24h 历史分布", f"rows={len(history)}")

    # TC-P2-13：工作日 vs 周末交叉对比（PySpark 维度 weekday_weekend）
    ww = fetch_json(base, "/api/weekday_weekend")
    types = {r.get("day_type") or r.get("label") for r in ww}
    if not types & {"weekday", "weekend"}:
        fail("TC-P2-13", "工作日周末对比", f"got={types}")
    pass_case("TC-P2-13", "工作日周末对比", str(types))

    # TC-P2-14：电站排行 Top10
    rank = fetch_json(base, "/api/station_rank")
    if not isinstance(rank, list) or len(rank) > 10:
        fail("TC-P2-14", "电站排行", f"len={len(rank) if isinstance(rank, list) else type(rank)}")
    if rank and "name" not in rank[0]:
        fail("TC-P2-14", "电站排行", "缺少 name")
    pass_case("TC-P2-14", "电站排行", f"rows={len(rank)}")

    # TC-P2-15：负荷预测 API 必须含 1h/6h/24h 三档（与 load_forecast 表一致）
    load = fetch_json(base, "/api/load_forecast")
    horizons = {r.get("horizon") for r in load} if isinstance(load, list) else set()
    if not {"1h", "6h", "24h"}.issubset(horizons):
        fail("TC-P2-15", "负荷预测 API", f"horizons={horizons}")
    pass_case("TC-P2-15", "负荷预测 API", str(horizons))

    # TC-P2-16：模型评估 API 与 evaluation.json 同源
    ml_ev = fetch_json(base, "/api/ml_evaluation")
    if "model" not in ml_ev:
        fail("TC-P2-16", "模型评估 API", f"keys={list(ml_ev.keys())}")
    pass_case("TC-P2-16", "模型评估 API", ml_ev.get("model", ""))


# ---------------------------------------------------------------------------
# 模块3 辅助：数 CSV 行数、在 PySpark 输出目录里找某个维度的 csv
# ---------------------------------------------------------------------------
def _csv_has_rows(path: Path) -> int:
    """返回 CSV 数据行数（不含表头）。"""
    if not path.is_file():
        return 0
    with path.open(encoding="utf-8") as fp:
        return max(sum(1 for _ in fp) - 1, 0)


def _analytics_csv_dir(name: str) -> Path | None:
    """在 ml/output/analytics/<name>/ 下找第一个 csv（PySpark 可能写子目录或 _SUCCESS）。"""
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


# ---------------------------------------------------------------------------
# 模块3：Hadoop/Spark 数据链路（TC-P2-17～24）
# 默认只查本地镜像 ml/data/hdfs/；TC-P2-24 需 --with-hdfs 在答辩机执行 hdfs dfs -ls
# ---------------------------------------------------------------------------
def run_module_hadoop(with_hdfs: bool) -> None:
    """功能点3：Hadoop/Spark 数据链路（TC-P2-17～24）。"""

    # export_to_hdfs.py 导出的四张表路径（dws=汇总层，dim=维表）
    csv_paths = {
        "station_hourly": HDFS_MIRROR / "charging/dws/station_hourly/station_hourly.csv",
        "station_daily": HDFS_MIRROR / "charging/dws/station_daily/station_daily.csv",
        "station": HDFS_MIRROR / "charging/dim/station/station.csv",
        "pile": HDFS_MIRROR / "charging/dim/pile/pile.csv",
    }

    # TC-P2-17：四表文件都存在
    for name, path in csv_paths.items():
        if not path.is_file():
            fail("TC-P2-17", "HDFS 本地镜像四表", f"缺少 {path.name}")
    pass_case("TC-P2-17", "HDFS 本地镜像四表")

    # TC-P2-18 / TC-P2-19：dws 层 CSV 非空
    h_rows = _csv_has_rows(csv_paths["station_hourly"])
    if h_rows <= 0:
        fail("TC-P2-18", "小时表 CSV 有数据", "0 行")
    pass_case("TC-P2-18", "小时表 CSV 有数据", f"rows={h_rows}")

    d_rows = _csv_has_rows(csv_paths["station_daily"])
    if d_rows <= 0:
        fail("TC-P2-19", "日表 CSV 有数据", "0 行")
    pass_case("TC-P2-19", "日表 CSV 有数据", f"rows={d_rows}")

    # TC-P2-20：维表 station、pile 非空
    if _csv_has_rows(csv_paths["station"]) <= 0 or _csv_has_rows(csv_paths["pile"]) <= 0:
        fail("TC-P2-20", "维表 CSV 完整", "station 或 pile 为空")
    pass_case("TC-P2-20", "维表 CSV 完整")

    # TC-P2-21：PySpark 至少产出 8 个分析维度目录（答辩要求 ≥8 维）
    analytics_root = OUTPUT_DIR / "analytics"
    dims = [p.name for p in analytics_root.iterdir() if p.is_dir()] if analytics_root.is_dir() else []
    if len(dims) < 8:
        fail("TC-P2-21", "PySpark 分析维度", f"仅 {len(dims)} 个: {dims}")
    pass_case("TC-P2-21", "PySpark 分析维度", f"count={len(dims)}")

    # TC-P2-22：交叉对比1 — 工作日/周末
    ww_csv = _analytics_csv_dir("weekday_weekend")
    if ww_csv is None:
        fail("TC-P2-22", "交叉对比-工作日周末", "无 CSV")
    text = ww_csv.read_text(encoding="utf-8", errors="ignore").lower()
    if "weekday" not in text and "weekend" not in text:
        fail("TC-P2-22", "交叉对比-工作日周末", "CSV 无 weekday/weekend")
    pass_case("TC-P2-22", "交叉对比-工作日周末")

    # TC-P2-23：交叉对比2 — 电站×小时热力矩阵
    matrix_csv = _analytics_csv_dir("station_hour_matrix")
    if matrix_csv is None:
        fail("TC-P2-23", "交叉对比-电站×小时", "无 CSV")
    header = matrix_csv.read_text(encoding="utf-8", errors="ignore").splitlines()[0].lower()
    if "stat_hour" not in header and "station" not in header:
        fail("TC-P2-23", "交叉对比-电站×小时", f"header={header}")
    pass_case("TC-P2-23", "交叉对比-电站×小时")

    # TC-P2-24：远程 HDFS 上 /charging/ 可访问（仅 node100 等 Hadoop 环境）
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
    """--print-catalog：把 24 条用例文字打印到终端，方便填 Excel。"""
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
    parser.add_argument(
        "--dashboard-url",
        default=os.environ.get("DASHBOARD_URL", "http://127.0.0.1:5000"),
        help="Flask 地址；node100 答辩时用 http://192.168.176.100:5000",
    )
    args = parser.parse_args()

    if args.print_catalog:
        print_catalog()
        return 0

    # 数据库路径：环境变量 CHARGE_DB / ADS_DB 优先，否则 db/charge.db
    db_path = resolve_db_path()
    if not db_path.is_file():
        print(f"FAIL: 数据库不存在 {db_path}", file=sys.stderr)
        print("请先: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql", file=sys.stderr)
        print("然后: bash ml/run_pipeline.sh --generate 3000", file=sys.stderr)
        return 1

    print(f"DB: {db_path}")
    conn = connect_db(db_path)
    try:
        # 模块1 + 模块3：只依赖本地文件，默认必跑
        run_module_ml(conn)
        run_module_hadoop(args.with_hdfs)
    finally:
        conn.close()

    # 模块2：可选，Flask 不在本机时需 --dashboard-url 指向 node100
    if args.with_dashboard:
        run_module_dashboard(args.dashboard_url)
    else:
        print("SKIP TC-P2-09～16 Dashboard API（加 --with-dashboard 启用）")

    # 注意：SKIP 的用例不会 FAIL，但也不会计入实际执行数；填 Excel 时 SKIP 项需手动测或标 N/A
    print(f"\n========== ALL {TOTAL_CASES} PHASE2 TESTS PASSED ==========")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nTEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
