#!/usr/bin/env python3
"""导出未清洗的业务原始表（ODS），供数据集提交；不含 ads_* / 预测表。

用法:
  python3 ml/export_raw_dataset.py
  python3 ml/export_raw_dataset.py --copy-db   # 同时复制 charge.db 到 ml/data/raw/

产出目录:
  ml/data/raw/charging/ods/charge_order/charge_order.csv
  ml/data/raw/charging/ods/user/user.csv
  ml/data/raw/charging/dim/station/station.csv
  ml/data/raw/charging/dim/pile/pile.csv
  ml/data/raw/charging/ods/wallet_log/wallet_log.csv  （有数据时）
"""

from __future__ import annotations

import argparse
import csv
import shutil
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import DEFAULT_DB, ROOT, connect_db, resolve_db_path

RAW_ROOT = Path(__file__).resolve().parent / "data" / "raw"

EXPORTS: dict[str, str] = {
    "charging/ods/charge_order": "SELECT * FROM charge_order ORDER BY id",
    "charging/ods/user": "SELECT * FROM user ORDER BY id",
    "charging/dim/station": "SELECT * FROM station ORDER BY id",
    "charging/dim/pile": "SELECT * FROM pile ORDER BY station_id, id",
    "charging/ods/wallet_log": "SELECT * FROM wallet_log ORDER BY id",
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


def main() -> int:
    parser = argparse.ArgumentParser(description="导出原始业务表 CSV（未经过 collector 清洗）")
    parser.add_argument("--clean", action="store_true", help="导出前清空 ml/data/raw/")
    parser.add_argument("--copy-db", action="store_true", help="复制 db/charge.db 到 ml/data/raw/")
    args = parser.parse_args()

    db_path = resolve_db_path()
    if not db_path.is_file():
        print(f"FAIL: 数据库不存在 {db_path}", file=sys.stderr)
        print("请先: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql", file=sys.stderr)
        return 1

    if args.clean and RAW_ROOT.exists():
        shutil.rmtree(RAW_ROOT)

    conn = connect_db(db_path)
    total = 0
    print(f"导出源: {db_path}")
    for rel_dir, sql in EXPORTS.items():
        table_name = rel_dir.split("/")[-1]
        out_file = RAW_ROOT / rel_dir / f"{table_name}.csv"
        try:
            count = export_table(conn, sql, out_file)
        except sqlite3.OperationalError as exc:
            print(f"  跳过 {table_name}: {exc}")
            continue
        total += count
        print(f"  {table_name}: {count} 行 -> {out_file.relative_to(RAW_ROOT.parent.parent)}")

    conn.close()

    order_cnt = 0
    order_csv = RAW_ROOT / "charging/ods/charge_order/charge_order.csv"
    if order_csv.is_file():
        with order_csv.open(encoding="utf-8") as fp:
            order_cnt = max(sum(1 for _ in fp) - 1, 0)
    if order_cnt <= 0:
        print("WARN: charge_order 为空，可先: python3 ml/generate_orders.py 3000", file=sys.stderr)

    if args.copy_db:
        dest = RAW_ROOT / "charge.db"
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(db_path, dest)
        print(f"  charge.db -> {dest.relative_to(RAW_ROOT.parent.parent)}")

    schema_dest = RAW_ROOT / "schema.sql"
    seed_dest = RAW_ROOT / "seed.sql"
    for src_name, dest in (("schema.sql", schema_dest), ("seed.sql", seed_dest)):
        src = ROOT / "db" / src_name
        if src.is_file():
            shutil.copy2(src, dest)
            print(f"  {src_name} -> {dest.relative_to(RAW_ROOT.parent.parent)}")

    print(f"合计导出 {total} 行到 {RAW_ROOT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
