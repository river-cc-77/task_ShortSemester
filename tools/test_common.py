"""Shared helpers for tools/test_server*.py."""

from __future__ import annotations

from datetime import date, timedelta


def admin_default_date_range() -> tuple[str, str]:
    """与 Admin 订单/日志页默认区间对齐：约 today-2月 ~ today+1月。

    同时保证覆盖 seed.sql 中 2026-08 的演示订单，避免系统日期偏离 seed 时测不到数据。
    """
    today = date.today()
    date_from = (today - timedelta(days=62)).isoformat()
    date_to = (today + timedelta(days=31)).isoformat()
    if date_from > "2026-08-01":
        date_from = "2026-08-01"
    if date_to < "2026-09-30":
        date_to = "2026-09-30"
    return date_from, date_to


def seed_order_sample_range() -> tuple[str, str]:
    """seed 中批量订单所在日期区间（order.list 日期筛选专项测试用）。"""
    return "2026-08-26", "2026-08-28"
