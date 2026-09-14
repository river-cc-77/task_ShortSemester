#!/usr/bin/env python3
"""PySpark 多维分析（第二阶段，≥8 维度 + ≥2 交叉对比）。

与 ml/spark/analytics.sql 口径一致；本地无 Hadoop 时读 CSV 镜像，结果写 ml/output/analytics/。

用法:
  python ml/pyspark_analytics.py
  SPARK_MASTER=local[*] HDFS_URI=hdfs://... python ml/pyspark_analytics.py --upload
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import HDFS_MIRROR, OUTPUT_DIR

try:
    from pyspark.sql import SparkSession
    from pyspark.sql import functions as F
except ImportError:
    print("请先安装 PySpark: pip install pyspark", file=sys.stderr)
    raise SystemExit(1)


def spark_session() -> SparkSession:
    master = os.environ.get("SPARK_MASTER", "local[*]")
    return SparkSession.builder.appName("charge-analytics").master(master).getOrCreate()


def read_csv(spark, rel_path: str):
    path = HDFS_MIRROR / rel_path
    return spark.read.option("header", True).option("inferSchema", True).csv(str(path))


def write_dimension(df, name: str) -> None:
    out = OUTPUT_DIR / "analytics" / name
    out.mkdir(parents=True, exist_ok=True)
    df.coalesce(1).write.mode("overwrite").option("header", True).csv(str(out))
    print(f"  -> analytics/{name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upload", action="store_true", help="上传到 HDFS（需 HDFS_URI）")
    args = parser.parse_args()

    spark = spark_session()
    hourly = read_csv(spark, "charging/dws/station_hourly/station_hourly.csv")
    daily = read_csv(spark, "charging/dws/station_daily/station_daily.csv")
    station = read_csv(spark, "charging/dim/station/station.csv")
    pile = read_csv(spark, "charging/dim/pile/pile.csv")

    # 1 电站排行
    latest = daily.groupBy().agg(F.max("stat_date").alias("mx")).collect()[0]["mx"]
    rank = (
        daily.filter(F.col("stat_date") == latest)
        .join(station, daily.station_id == station.id)
        .select(station.name, "orders", "revenue", "kwh", "utilization", "peak_hour", "fault_rate")
        .orderBy(F.desc("revenue"))
    )
    write_dimension(rank, "station_rank")

    # 2 24h 负荷
    hourly_profile = hourly.groupBy("stat_hour").agg(
        F.sum("kwh").alias("kwh"), F.sum("orders").alias("orders")
    ).orderBy("stat_hour")
    write_dimension(hourly_profile, "hourly_profile")

    # 3 区域
    region = (
        daily.join(station, daily.station_id == station.id)
        .withColumn("region", F.regexp_extract("address", r"([^区]+区)", 1))
        .groupBy("region")
        .agg(F.sum("orders").alias("orders"), F.sum("revenue").alias("revenue"), F.sum("kwh").alias("kwh"))
    )
    write_dimension(region, "region_stats")

    # 4 电桩状态
    pile_status = pile.groupBy("status").count().withColumnRenamed("count", "cnt")
    write_dimension(pile_status, "pile_status")

    # 5 电站利用率
    station_util = (
        daily.join(station, daily.station_id == station.id)
        .groupBy(station.name.alias("name"))
        .agg(F.avg("utilization").alias("avg_util"), F.avg("turnover").alias("avg_turnover"))
    )
    write_dimension(station_util, "station_util")

    # 6 故障率
    fault = (
        daily.join(station, daily.station_id == station.id)
        .groupBy(station.name.alias("name"))
        .agg(F.avg("fault_rate").alias("fault_rate"), F.avg("busy_ratio").alias("busy_ratio"))
    )
    write_dimension(fault, "fault_rate")

    # 7 平台日 KPI（来自 station_daily 聚合近似）
    platform = daily.groupBy("stat_date").agg(
        F.sum("revenue").alias("total_revenue"),
        F.sum("kwh").alias("total_kwh"),
        F.sum("orders").alias("order_count"),
    ).orderBy("stat_date")
    write_dimension(platform, "platform_daily")

    # 8 用户活跃占位（PySpark 读不到 ads_daily_stats 时用订单近似）
    user_act = daily.groupBy("stat_date").agg(F.sum("orders").alias("order_count"))
    write_dimension(user_act, "user_activity")

    # 交叉对比 1：工作日 vs 周末
    weekday = (
        daily.withColumn("dow", F.dayofweek(F.to_date("stat_date")))
        .withColumn("day_type", F.when(F.col("dow").isin(1, 7), "weekend").otherwise("weekday"))
        .groupBy("day_type")
        .agg(F.sum("kwh").alias("kwh"), F.sum("orders").alias("orders"), F.sum("revenue").alias("revenue"))
    )
    write_dimension(weekday, "weekday_weekend")

    # 交叉对比 2：电站 × 小时
    matrix = (
        hourly.join(station, hourly.station_id == station.id)
        .groupBy(station.name.alias("station_name"), "stat_hour")
        .agg(F.sum("kwh").alias("kwh"), F.sum("orders").alias("orders"))
    )
    write_dimension(matrix, "station_hour_matrix")

    print("PySpark 分析完成（10 个维度目录，含 2 组交叉对比）")
    if args.upload and os.environ.get("HDFS_URI"):
        print("提示: 答辩环境请用 hdfs dfs -put ml/output/analytics /charging/ads/")

    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
