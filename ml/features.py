"""天气 / 节假日 / 周末特征（用于负荷预测修正因子）。"""

from __future__ import annotations

import hashlib
from datetime import date, datetime

# 2026 年中国大陆常见法定节假日（演示用静态表）
CN_HOLIDAYS_2026 = {
    date(2026, 1, 1),
    date(2026, 1, 2),
    date(2026, 1, 3),
    date(2026, 2, 17),
    date(2026, 2, 18),
    date(2026, 2, 19),
    date(2026, 2, 20),
    date(2026, 2, 21),
    date(2026, 2, 22),
    date(2026, 2, 23),
    date(2026, 4, 4),
    date(2026, 4, 5),
    date(2026, 4, 6),
    date(2026, 5, 1),
    date(2026, 5, 2),
    date(2026, 5, 3),
    date(2026, 5, 4),
    date(2026, 5, 5),
    date(2026, 6, 19),
    date(2026, 6, 20),
    date(2026, 6, 21),
    date(2026, 9, 25),
    date(2026, 9, 26),
    date(2026, 9, 27),
    date(2026, 10, 1),
    date(2026, 10, 2),
    date(2026, 10, 3),
    date(2026, 10, 4),
    date(2026, 10, 5),
    date(2026, 10, 6),
    date(2026, 10, 7),
}


def is_weekend(d: date) -> bool:
    return d.weekday() >= 5


def is_holiday(d: date) -> bool:
    return d in CN_HOLIDAYS_2026


def synthetic_weather_factor(d: date, hour: int) -> float:
    """无外部 API 时，按日期+小时生成稳定伪天气因子 0.85~1.15。"""
    key = f"{d.isoformat()}-{hour:02d}".encode("utf-8")
    digest = hashlib.md5(key).hexdigest()
    bucket = int(digest[:8], 16) % 1000 / 1000.0
    return round(0.85 + bucket * 0.30, 3)


def load_adjustment_factor(target: datetime) -> float:
    """综合修正：基础 1.0 + 周末 + 节假日 + 天气。"""
    d = target.date()
    factor = 1.0
    if is_weekend(d):
        factor += 0.12
    if is_holiday(d):
        factor += 0.18
    weather = synthetic_weather_factor(d, target.hour)
    factor *= weather
    return round(factor, 3)


def duration_adjustment_factor(target: datetime) -> float:
    """充电时长受天气影响更大（雨雪天略长）。"""
    d = target.date()
    weather = synthetic_weather_factor(d, target.hour)
    factor = 1.0
    if weather < 0.95:
        factor += 0.08
    if is_holiday(d):
        factor += 0.05
    return round(factor, 3)


def feature_tags(target: datetime) -> dict[str, object]:
    d = target.date()
    return {
        "is_weekend": is_weekend(d),
        "is_holiday": is_holiday(d),
        "weather_factor": synthetic_weather_factor(d, target.hour),
        "load_factor": load_adjustment_factor(target),
    }
