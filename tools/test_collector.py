#!/usr/bin/env python3
"""ads-collector 数据采集结果校验 — 对照业务库验证 ads_* 分析表是否正确。

前置：先跑 collector 写入分析表
  cd collector && ./ads-collector

用法：
  python3 tools/test_collector.py

检查项：
  [1/4] 8 张对外 ads 表是否存在且有数据
  [2/4] ads_daily_stats 营收/单量 与 ads_order_fact 清洗底稿是否对账一致
  [3/4] ads_order_fact 台账行数与近 30 天窗口
  [4/4] 有订单的日期是否算出 completion_rate、utilization 等派生指标
"""

import os
import sqlite3
import sys
from pathlib import Path


def db_path() -> Path:
    """业务库路径，可用环境变量 ADS_DB 覆盖。"""
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
    """表必须存在且非空 — 否则说明 schema 未建或 collector 未跑。"""
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
    """平台日 KPI 与订单事实表对账：同一 stat_date 的 revenue、order_count 应一致。

    口径：ads_order_fact 中 excluded=0 且 status=已完成 的订单。
    """
    ads_rows = fetch_all(
        cur,
        """
        SELECT stat_date, ROUND(total_revenue, 2), order_count
          FROM ads_daily_stats
         ORDER BY stat_date
        """,
    )
    # 与 collector aggregator 相同口径
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
                continue  # 静默日填 0，fact 无行是正常的
            mismatches.append(f"{stat_date}: no fact rows but ads has data")
            continue
        fact_rev, fact_cnt = fact_map[stat_date]
        if abs((revenue or 0) - (fact_rev or 0)) > 0.02 or orders != fact_cnt:
            mismatches.append(
                f"{stat_date}: ads({revenue},{orders}) vs fact({fact_rev},{fact_cnt})"
            )
    if mismatches:
        raise RuntimeError("ads_daily_stats reconciliation failed:\n  " + "\n  ".join(mismatches))

    # 被清洗剔除的已完成单（如 test_server 瞬间充电金额为 0）仅提示，不算失败
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
    """ads_order_fact 每单一行；统计 excluded 分布与近 30 天窗口行数。"""
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
    """有已完成订单的日期，ads_daily_stats 应算出 completion_rate、utilization。"""
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
        # [1/4] collector 应填充的 8 张对外分析表（供 dashboard 直读）
        print("\n[1/4] ads_* tables populated")
        for table, label in [
            ("ads_daily_stats", "平台日 KPI — KPI 卡、营收趋势"),
            ("ads_order_fact", "订单清洗台账 — 指标取数底稿（内部）"),
            ("ads_status_snapshot", "桩状态周期快照 — 状态分布/趋势"),
            ("ads_station_daily", "电站×日 — 站排行、站趋势"),
            ("ads_pile_daily", "桩×日 — 单桩钻取、桩利用率"),
            ("ads_hourly_stats", "平台×小时 — 高峰曲线"),
            ("ads_station_hourly", "站×小时 — 每站高峰"),
            ("ads_region_daily", "区域×日 — 区域分布"),
        ]:
            check_table_has_rows(cur, table, label)

        # [2/4] 日营收与 fact 表对账，防止聚合逻辑错误
        print("\n[2/4] daily revenue reconciliation")
        check_daily_reconciliation(cur)

        # [3/4] fact 表规模 sanity check
        print("\n[3/4] order fact ledger")
        check_order_fact(cur)

        # [4/4] 派生指标非空
        print("\n[4/4] derived metric columns")
        check_derived_columns(cur)

        issue_cnt = fetch_one(cur, "SELECT COUNT(*) FROM ads_order_issue")[0]
        print(
            f"\n  info ads_order_issue: {issue_cnt} issue row(s) "
            f"(0 expected on clean seed; >0 after test_server 异常单)"
        )

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
