#!/usr/bin/env python3
"""ML 预测流水线一键入口（新手推荐）。

步骤:
  1. （可选）生成模拟订单
  2. 提醒/检测 ads_* 是否就绪
  3. 导出到 HDFS 镜像
  4. 本地 Spark SQL 等价预测（predict_local）并写回 SQLite

用法:
  python ml/run_pipeline.py
  python ml/run_pipeline.py --generate 3000
  python ml/run_pipeline.py --skip-export
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ML = Path(__file__).resolve().parent
COLLECTOR_BIN = ROOT / "collector" / "ads-collector"
sys.path.insert(0, str(ML))

from common import connect_db, resolve_db_path


def run_py(script: str, *args: str) -> None:
    cmd = [sys.executable, str(ML / script), *args]
    print(f"\n>>> {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def check_ads_ready() -> bool:
    conn = connect_db()
    hourly = conn.execute("SELECT COUNT(*) FROM ads_station_hourly").fetchone()[0]
    conn.close()
    return hourly > 0


def try_run_collector() -> bool:
    """Linux 验收环境：ads_* 为空时自动跑一次 ads-collector。"""
    if platform.system() != "Linux":
        return False
    if not (COLLECTOR_BIN.is_file() and os.access(COLLECTOR_BIN, os.X_OK)):
        return False
    print("\n>>> ads_* 为空，运行 collector/ads-collector ...")
    subprocess.run([str(COLLECTOR_BIN)], cwd=ROOT / "collector", check=True)
    return check_ads_ready()


def main() -> int:
    parser = argparse.ArgumentParser(description="ML 预测流水线（Linux / Ubuntu 22.04）")
    parser.add_argument("--generate", type=int, default=0, help="先生成 N 条模拟订单")
    parser.add_argument("--skip-export", action="store_true", help="跳过 HDFS 导出")
    parser.add_argument(
        "--bootstrap-ads",
        action="store_true",
        help="[仅应急] 跳过 collector，用 Python 最小聚合（勿在验收环境使用）",
    )
    args = parser.parse_args()

    db_path = resolve_db_path()
    print(f"数据库: {db_path}")

    if args.generate > 0:
        run_py("generate_orders.py", str(args.generate))
        if platform.system() == "Linux" and COLLECTOR_BIN.is_file():
            try_run_collector()

    if not check_ads_ready():
        if args.bootstrap_ads:
            run_py("bootstrap_ads.py")
        elif not try_run_collector():
            print("\n[!] ads_station_hourly 为空。Linux 验收环境请:")
            print("    cd collector && qmake6 collector.pro && make -j4 && ./ads-collector")
            print("    或一键: bash ml/run_pipeline.sh --generate 3000")
            return 1

    if not args.skip_export:
        run_py("export_to_hdfs.py", "--clean")

    run_py("predict_local.py")

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
