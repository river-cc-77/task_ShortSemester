#!/usr/bin/env python3
"""Verify ads-collector output against charge.db business tables."""

import os
import sqlite3
import sys
from pathlib import Path


def db_path() -> Path:
    env = os.environ.get("ADS_DB")
    if env:
        return Path(env)
    root = Path(__file__).resolve().parent.parent
    return root / "db" / "charge.db"


def fetch_one(cur: sqlite3.Cursor, sql: str, params=()) -> tuple:
    cur.execute(sql, params)
    return cur.fetchone()


def fetch_all(cur: sqlite3.Cursor, sql: str, params=()) -> list:
    cur.execute(sql, params)
    return cur.fetchall()


def check_table_exists(cur: sqlite3.Cursor, table: str) -> bool:
    row = fetch_one(
        cur,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    )
    return row is not None


def check_table_has_rows(cur: sqlite3.Cursor, table: str, label: str) -> None:
    if not check_table_exists(cur, table):
        raise RuntimeError(
            f"{label}: table {table} missing — rebuild db from schema.sql "
            f"(cd db && rm -f charge.db && sqlite3 charge.db < schema.sql && "
            f"sqlite3 charge.db < seed.sql)"
        )
    row = fetch_one(cur, f"SELECT COUNT(*) FROM {table}")
    count = row[0] if row else 0
    if count == 0:
        raise RuntimeError(
            f"{label}: {table} is empty — run ads-collector first "
            f"(cd collector && ./ads-collector)"
        )
    print(f"  OK {table}: {count} rows")


def check_daily_reconciliation(cur: sqlite3.Cursor) -> None:
    ads_rows = fetch_all(
        cur,
        """
        SELECT stat_date, ROUND(total_revenue, 2), order_count
          FROM ads_daily_stats
         ORDER BY stat_date
        """,
    )
    # 与 aggregator 相同口径：读 ads_order_fact 中 excluded=0 的已完成单
    fact_rows = fetch_all(
        cur,
        """
        SELECT stat_date, ROUND(SUM(amount_eff), 2), COUNT(*)
          FROM ads_order_fact
         WHERE excluded = 0 AND status = '已完成'
         GROUP BY stat_date
         ORDER BY stat_date
        """,
    )
    fact_map = {r[0]: (r[1], r[2]) for r in fact_rows}
    mismatches = []
    for stat_date, revenue, orders in ads_rows:
        if stat_date not in fact_map:
            if revenue == 0 and orders == 0:
                continue
            mismatches.append(f"{stat_date}: no fact rows but ads has data")
            continue
        fact_rev, fact_cnt = fact_map[stat_date]
        if abs((revenue or 0) - (fact_rev or 0)) > 0.02 or orders != fact_cnt:
            mismatches.append(
                f"{stat_date}: ads({revenue},{orders}) vs fact({fact_rev},{fact_cnt})"
            )
    if mismatches:
        raise RuntimeError("ads_daily_stats reconciliation failed:\n  " + "\n  ".join(mismatches))

    excluded = fetch_one(
        cur,
        """
        SELECT COUNT(*) FROM ads_order_fact
         WHERE excluded = 1 AND status = '已完成'
        """,
    )[0]
    if excluded:
        print(
            f"  info: {excluded} completed order(s) excluded by cleaner "
            f"(e.g. zero_unest from instant test charges — expected after test_server.py)"
        )
    print(f"  OK ads_daily_stats reconciled ({len(ads_rows)} days)")


def check_order_fact(cur: sqlite3.Cursor) -> None:
    excluded = fetch_all(
        cur, "SELECT excluded, COUNT(*) FROM ads_order_fact GROUP BY excluded ORDER BY excluded"
    )
    total_fact = sum(r[1] for r in excluded)
    window_cnt = fetch_one(
        cur,
        """
        SELECT COUNT(*) FROM ads_order_fact
         WHERE stat_date BETWEEN date('now', '-29 day') AND date('now')
        """,
    )[0]
    print(f"  OK ads_order_fact: {total_fact} rows (window {window_cnt}), excluded={excluded}")


def check_derived_columns(cur: sqlite3.Cursor) -> None:
    row = fetch_one(
        cur,
        """
        SELECT COUNT(*)
          FROM ads_daily_stats
         WHERE stat_date IN (
               SELECT substr(COALESCE(start_at, created_at), 1, 10)
                 FROM charge_order WHERE status = '已完成'
               GROUP BY 1 HAVING COUNT(*) > 0
             )
           AND completion_rate IS NOT NULL
           AND utilization IS NOT NULL
        """,
    )
    if not row or row[0] == 0:
        raise RuntimeError("ads_daily_stats missing completion_rate/utilization on active days")
    print(f"  OK derived metrics on {row[0]} active day(s)")


def main() -> int:
    path = db_path()
    if not path.is_file():
        print(f"DB not found: {path}", file=sys.stderr)
        print("Run: cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql")
        return 1

    print(f"Checking {path}")
    conn = sqlite3.connect(path)
    cur = conn.cursor()

    try:
        print("\n[1/4] ads_* tables populated")
        for table, label in [
            ("ads_daily_stats", "platform daily"),
            ("ads_order_fact", "order fact"),
            ("ads_status_snapshot", "status snapshot"),
            ("ads_station_daily", "station daily"),
            ("ads_pile_daily", "pile daily"),
            ("ads_hourly_stats", "hourly"),
            ("ads_station_hourly", "station hourly"),
            ("ads_region_daily", "region daily"),
        ]:
            check_table_has_rows(cur, table, label)

        print("\n[2/4] daily revenue reconciliation")
        check_daily_reconciliation(cur)

        print("\n[3/4] order fact ledger")
        check_order_fact(cur)

        print("\n[4/4] derived metric columns")
        check_derived_columns(cur)

        issue_cnt = fetch_one(cur, "SELECT COUNT(*) FROM ads_order_issue")[0]
        print(f"\n  info ads_order_issue: {issue_cnt} issue row(s) (0 expected on clean seed)")

    finally:
        conn.close()

    print("\nAll collector checks passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"\nCOLLECTOR TEST FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
