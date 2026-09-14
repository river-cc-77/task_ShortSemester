#!/usr/bin/env python3
"""PySpark 数据清洗（第二阶段）。

本地：读 ml/data/hdfs 镜像 CSV，清洗后写 ml/output/clean/
Hadoop：设置 SPARK_MASTER、HDFS_URI 后写 HDFS /charging/clean/

用法:
  python ml/pyspark_clean.py
"""

from __future__ import annotations

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
    builder = SparkSession.builder.appName("charge-clean")
    master = __import__("os").environ.get("SPARK_MASTER", "local[*]")
    builder = builder.master(master)
    return builder.getOrCreate()


def main() -> int:
    spark = spark_session()
    src = HDFS_MIRROR / "charging" / "dws" / "station_hourly" / "station_hourly.csv"
    if not src.is_file():
        print(f"缺少数据源: {src}，请先运行 ml/export_to_hdfs.py", file=sys.stderr)
        return 1

    df = (
        spark.read.option("header", True)
        .option("inferSchema", True)
        .csv(str(src))
        .filter(F.col("stat_date").isNotNull())
        .filter(F.col("kwh").isNull() | (F.col("kwh") >= 0))
        .filter(F.col("orders").isNull() | (F.col("orders") >= 0))
        .dropDuplicates(["station_id", "stat_date", "stat_hour"])
        .withColumn("kwh", F.coalesce(F.col("kwh"), F.lit(0.0)))
        .withColumn("orders", F.coalesce(F.col("orders"), F.lit(0)))
    )

    out_dir = OUTPUT_DIR / "clean"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_csv = out_dir / "station_hourly_clean.csv"
    df.coalesce(1).write.mode("overwrite").option("header", True).csv(str(out_dir / "_tmp"))
    # 取 part-*.csv 重命名
    parts = list((out_dir / "_tmp").glob("part-*.csv"))
    if parts:
        parts[0].replace(out_csv)
    print(f"清洗完成: {df.count()} 行 -> {out_csv}")
    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
