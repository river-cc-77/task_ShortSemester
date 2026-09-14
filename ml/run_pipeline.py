#!/usr/bin/env python3
"""ML 预测流水线一键入口（新手推荐）。

步骤:
  1. （可选）生成模拟订单
  2. 提醒/检测 ads_* 是否就绪
  3. 导出到 HDFS 镜像
  4. PySpark 清洗 + 多维分析（第二阶段）
  5. 本地 Spark SQL 等价预测（predict_local）并写回 SQLite
  6. 模型离线评估（MAE/RMSE/MAPE）

用法:
  python ml/run_pipeline.py
  python ml/run_pipeline.py --generate 3000
  python ml/run_pipeline.py --skip-export
  python ml/run_pipeline.py --skip-pyspark
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ML = Path(__file__).resolve().parent
COLLECTOR_BIN = ROOT / "collector" / "ads-collector"
sys.path.insert(0, str(ML))

from common import connect_db, ensure_schema, resolve_db_path


def run_py(script: str, *args: str) -> None:
    cmd = [sys.executable, str(ML / script), *args]
    print(f"\n>>> {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def ads_ready() -> bool:
    """ads_* 里是否已经有聚合结果（只说明历史上有过数据）。"""
    conn = connect_db()
    ensure_schema(conn)  # 全新库可能还没建表，先补齐再探测，避免 "no such table"
    hourly = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    conn.close()
    return hourly > 0


def ads_current() -> bool:
    """今日那行是否已经有真实订单数据。

    单看 ads_ready() 不够：今日这行即使存在，也可能是上一次留下的全 0 占位
    （刚过零点、或 ensure_today_orders 补完单但还没重新聚合），
    此时大屏的高峰/排行会静默变空。
    """
    conn = connect_db()
    ensure_schema(conn)
    today_orders = conn.execute(
        "SELECT COALESCE(SUM(orders), 0) FROM ads_station_hourly "
        "WHERE stat_date = date('now', 'localtime')"
    ).fetchone()[0]
    conn.close()
    return int(today_orders) > 0


def ads_fingerprint() -> tuple[int, str, int]:
    """ads_station_hourly 的指纹：(行数, 最新 updated_at, 订单总数)。

    判断「collector 是否真算完一轮」必须看指纹变化，而不是看表是否非空——
    表非空只说明历史上有过数据，无法区分是不是本轮刚写的。
    """
    conn = connect_db()
    row = conn.execute(
        "SELECT COUNT(*), COALESCE(MAX(updated_at), ''), COALESCE(SUM(orders), 0) "
        "FROM ads_station_hourly"
    ).fetchone()
    conn.close()
    return (int(row[0]), str(row[1]), int(row[2]))


def try_run_collector() -> bool:
    """Linux 验收环境：跑 ads-collector，等它算完一轮后再结束进程。

    旧实现是「表非空即视为就绪」，在已有数据的库上会立刻 terminate，
    而此刻 collector 连首轮 aggregate 都还没提交——SIGTERM 直接把该事务回滚，
    于是 ensure_today_orders / generate_orders 刚补的单永远进不了 ads_*。
    """
    if platform.system() != "Linux":
        return False
    if not (COLLECTOR_BIN.is_file() and os.access(COLLECTOR_BIN, os.X_OK)):
        return False

    before = ads_fingerprint()
    print("\n>>> 运行 collector/ads-collector（首轮回填约 10~60s）...")
    proc = subprocess.Popen([str(COLLECTOR_BIN)], cwd=ROOT / "collector")
    changed = False
    try:
        for _ in range(180):
            # 先等待再检查：至少给 collector 1 秒完成首轮，避免刚启动就被判定为"已就绪"
            time.sleep(1)
            if ads_fingerprint() != before:
                changed = True
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
    if changed:
        print("    ads_* 已刷新，collector 已停止")
    else:
        print("    [!] 180s 内未见 ads_* 变化，collector 可能未成功", file=sys.stderr)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="ML 预测流水线（Linux / Ubuntu 22.04）")
    parser.add_argument("--generate", type=int, default=0, help="先生成 N 条模拟订单")
    parser.add_argument("--skip-export", action="store_true", help="跳过 HDFS 导出")
    parser.add_argument(
        "--bootstrap-ads",
        action="store_true",
        help="[仅应急] 跳过 collector，用 Python 最小聚合（勿在验收环境使用）",
    )
    parser.add_argument("--skip-pyspark", action="store_true", help="跳过 PySpark 清洗/分析")
    parser.add_argument("--skip-evaluate", action="store_true", help="跳过模型离线评估")
    args = parser.parse_args()

    db_path = resolve_db_path()
    print(f"数据库: {db_path}")

    if args.generate > 0:
        run_py("generate_orders.py", str(args.generate))
        run_py("ensure_today_orders.py")

    # 需要重新聚合的三种情形：刚补过单 / ads_* 是空的 / 今日那行仍是 0 占位。
    # 只判断"表是否为空"会漏掉后两种里最常见的一种 —— 表有历史数据但今天的没算进去。
    if args.generate > 0 or not ads_ready() or not ads_current():
        if args.bootstrap_ads or platform.system() != "Linux":
            if platform.system() != "Linux":
                print("\n>>> 非 Linux 环境，使用 bootstrap_ads.py 生成 ads_*（演示/开发）")
            run_py("bootstrap_ads.py")
        elif not try_run_collector():
            print("\n[!] ads_station_hourly 为空或今日无数据。Linux 验收环境请:")
            print("    cd collector && qmake6 collector.pro && make -j4 && ./ads-collector")
            print("    或一键: bash ml/run_pipeline.sh --generate 3000")
            print("    或应急: python ml/run_pipeline.py --bootstrap-ads")
            return 1

    if not args.skip_export:
        run_py("export_to_hdfs.py", "--clean")

    if not args.skip_pyspark:
        try:
            run_py("pyspark_clean.py")
            run_py("pyspark_analytics.py")
        except subprocess.CalledProcessError as exc:
            print(f"\n[!] PySpark 步骤失败（需 pip install pyspark）: {exc}", file=sys.stderr)
            print("    可稍后单独运行: python ml/pyspark_analytics.py", file=sys.stderr)
            print("    或加 --skip-pyspark 跳过", file=sys.stderr)

    run_py("predict_local.py")

    if not args.skip_evaluate:
        run_py("evaluate.py")

    conn = connect_db()
    load_cnt = conn.execute("SELECT COUNT(*) FROM load_forecast").fetchone()[0]
    time_cnt = conn.execute("SELECT COUNT(*) FROM time_forecast").fetchone()[0]
    conn.close()

    print(f"\n完成: load_forecast={load_cnt}, time_forecast={time_cnt}")
    print("业务验证: 启动 charge-server 后调用 forecast.list / timeforecast.list")
    print("Spark/Hive 环境: 见 ml/README.md 中的 spark-sql 命令")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
