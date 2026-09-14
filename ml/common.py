"""ML 子系统公共配置与工具。"""

from __future__ import annotations

import os
import sqlite3
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ML_DIR = Path(__file__).resolve().parent
DEFAULT_DB = ROOT / "db" / "charge.db"
SCHEMA_SQL = ROOT / "db" / "schema.sql"
DATA_DIR = ML_DIR / "data"
HDFS_MIRROR = DATA_DIR / "hdfs"
OUTPUT_DIR = ML_DIR / "output"


HORIZONS = {"1h": 1, "6h": 6, "24h": 24}
VALID_HORIZONS = frozenset(HORIZONS)
HISTORY_DAYS = 7


def wma_weight(days_ago: int) -> float:
    """加权移动平均权重：越近的历史权重越大。w = 1 / (days_ago + 1)。"""
    if days_ago < 0:
        return 0.0
    return 1.0 / (days_ago + 1)


def resolve_db_path() -> Path:
    env = os.environ.get("ADS_DB") or os.environ.get("CHARGE_DB")
    if env:
        return Path(env)
    return DEFAULT_DB


def connect_db(db_path: Path | None = None) -> sqlite3.Connection:
    path = db_path or resolve_db_path()
    if not path.is_file():
        raise FileNotFoundError(f"数据库不存在: {path}")
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def ensure_schema(conn: sqlite3.Connection) -> list[str]:
    """补齐 db/schema.sql 里缺失的表（全部 CREATE ... IF NOT EXISTS，幂等）。

    旧库（如仓库附带的运行期 charge.db）可能早于 schema.sql 的某次新增，
    导致 load_forecast / time_forecast 等表不存在，而写入脚本只 DELETE/INSERT。
    返回本次新建的表名，便于调用方提示。
    """
    if not SCHEMA_SQL.is_file():
        raise FileNotFoundError(f"找不到 schema: {SCHEMA_SQL}")
    before = {
        row[0]
        for row in conn.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()
    }
    conn.executescript(SCHEMA_SQL.read_text(encoding="utf-8"))
    after = {
        row[0]
        for row in conn.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()
    }
    return sorted(after - before)


def now_local() -> datetime:
    return datetime.now().replace(microsecond=0)


def forecast_target(now: datetime, horizon: str) -> datetime:
    hours = HORIZONS[horizon]
    target = now + timedelta(hours=hours)
    return target.replace(minute=0, second=0, microsecond=0)


def format_hour(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%d %H:00")
