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


def rows_to_dicts(rows) -> list[dict]:
    return [dict(r) for r in rows]
