"""Dashboard 只读 SQLite 访问（与 server/collector 共用 charge.db）。"""

from __future__ import annotations

import os
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "db" / "charge.db"


def db_path() -> Path:
    env = os.environ.get("CHARGE_DB") or os.environ.get("ADS_DB")
    if env:
        return Path(env)
    return DEFAULT_DB


def connect() -> sqlite3.Connection:
    path = db_path()
    if not path.is_file():
        raise FileNotFoundError(f"数据库不存在: {path}")
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    return conn


# 避免浮点累加产生 29.000000003 等展示问题
_ROUND_2 = frozenset(
    {
        "total_revenue",
        "revenue",
        "total_kwh",
        "kwh",
        "amount",
        "predicted_load",
        "predicted_avg_duration_min",
        "mae",
        "rmse",
        "mape_pct",
    }
)
_ROUND_4 = frozenset({"utilization", "fault_rate", "avg_util", "avg_turnover", "busy_ratio"})


def _round_num(key: str, value):
    if value is None:
        return None
    try:
        num = float(value)
    except (TypeError, ValueError):
        return value
    if key in _ROUND_4:
        return round(num, 4)
    if key in _ROUND_2:
        return round(num, 2)
    if isinstance(value, float):
        return round(num, 2)
    return value


def sanitize_row(row: dict) -> dict:
    return {k: _round_num(k, v) for k, v in row.items()}


def sanitize_value(value):
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            if isinstance(v, dict):
                out[k] = sanitize_row(v)
            elif isinstance(v, list):
                out[k] = sanitize_value(v)
            else:
                out[k] = _round_num(k, v)
        return out
    if isinstance(value, list):
        return [sanitize_value(x) for x in value]
    return value


def rows_to_dicts(rows) -> list[dict]:
    return [sanitize_row(dict(r)) for r in rows]
