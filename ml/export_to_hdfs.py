#!/usr/bin/env python3
"""将 collector 产出的 ads_* 表导出为 CSV，写入本地 HDFS 镜像目录，可选上传到真实 HDFS。

用法:
  python ml/export_to_hdfs.py
  HDFS_URI=hdfs://localhost:9000 python ml/export_to_hdfs.py --upload

本地镜像目录（模拟 HDFS）:
  ml/data/hdfs/charging/dws/station_hourly/
  ml/data/hdfs/charging/dws/station_daily/
  ml/data/hdfs/charging/dim/pile/
  ml/data/hdfs/charging/dim/station/
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import sqlite3
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import HDFS_MIRROR, connect_db, resolve_db_path

EXPORTS = {
    "charging/dws/station_hourly": "SELECT * FROM ads_station_hourly ORDER BY station_id, stat_date, stat_hour",
    "charging/dws/station_daily": "SELECT * FROM ads_station_daily ORDER BY station_id, stat_date",
    "charging/dim/pile": "SELECT id, pile_no, station_id, type, power_kw, status FROM pile ORDER BY station_id, id",
    "charging/dim/station": "SELECT id, name, address, lat, lng, price FROM station ORDER BY id",
}


def export_table(conn: sqlite3.Connection, sql: str, out_file: Path) -> int:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    cur = conn.execute(sql)
    columns = [desc[0] for desc in cur.description]
    rows = cur.fetchall()
    with out_file.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow(columns)
        writer.writerows(rows)
    return len(rows)


def upload_to_hdfs(local_root: Path, hdfs_uri: str) -> None:
    for rel_dir in EXPORTS:
        local_dir = local_root / rel_dir
        if not local_dir.is_dir():
            continue
        for csv_file in local_dir.glob("*.csv"):
            hdfs_dir = f"{hdfs_uri.rstrip('/')}/{rel_dir}"
            subprocess.run(["hdfs", "dfs", "-mkdir", "-p", hdfs_dir], check=False)
            subprocess.run(["hdfs", "dfs", "-put", "-f", str(csv_file), f"{hdfs_dir}/"], check=True)
            print(f"  上传 {csv_file.name} -> {hdfs_dir}/")


def main() -> int:
    parser = argparse.ArgumentParser(description="导出 ads 表到 HDFS 镜像")
    parser.add_argument("--upload", action="store_true", help="导出后上传到 HDFS_URI")
    parser.add_argument("--clean", action="store_true", help="导出前清空本地镜像目录")
    args = parser.parse_args()

    db_path = resolve_db_path()
    conn = connect_db(db_path)

    if args.clean and HDFS_MIRROR.exists():
        shutil.rmtree(HDFS_MIRROR)

    total = 0
    print(f"导出源: {db_path}")
    for rel_dir, sql in EXPORTS.items():
        table_name = rel_dir.split("/")[-1]
        out_file = HDFS_MIRROR / rel_dir / f"{table_name}.csv"
        count = export_table(conn, sql, out_file)
        total += count
        print(f"  {table_name}: {count} 行 -> {out_file.relative_to(HDFS_MIRROR.parent.parent)}")

    conn.close()
    print(f"合计导出 {total} 行到 {HDFS_MIRROR}")

    if args.upload:
        hdfs_uri = os.environ.get("HDFS_URI", "hdfs://localhost:9000")
        print(f"上传到 {hdfs_uri} ...")
        upload_to_hdfs(HDFS_MIRROR, hdfs_uri)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
