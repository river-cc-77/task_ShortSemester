#!/usr/bin/env bash
# ML 预测流水线（Ubuntu 22.04 / Linux 验收环境）
#
# 用法（在项目根目录）:
#   bash ml/run_pipeline.sh                   # 导出 + 预测（需已跑 collector）
#   bash ml/run_pipeline.sh --generate 3000   # 造单 → collector → 预测
#   bash ml/run_pipeline.sh --generate        # 同上，订单数取默认 3000
#   bash ml/run_pipeline.sh --spark           # Spark SQL 路径（需 Hadoop/Hive）
#
# 数据库路径: ADS_DB / CHARGE_DB 环境变量优先，默认 db/charge.db
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DB_PATH="${ADS_DB:-${CHARGE_DB:-db/charge.db}}"

GENERATE=0
USE_SPARK=0
SKIP_EXPORT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generate)
            # 数量可省略：--generate 等价于 --generate 3000
            # （原写法无条件 shift 2，省略数量时会越界，set -e 下整个脚本静默退出）
            if [[ $# -ge 2 && "$2" =~ ^[0-9]+$ ]]; then
                GENERATE="$2"; shift
            else
                GENERATE=3000
            fi
            shift ;;
        --spark)    USE_SPARK=1; shift ;;
        --skip-export) SKIP_EXPORT=1; shift ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# 查询 ADS 库。库不存在时直接返回 0，避免 sqlite3 顺手建出一个空库文件。
sqlq() {
    if [[ ! -f "$DB_PATH" ]]; then echo 0; return; fi
    sqlite3 "$DB_PATH" "$1" 2>/dev/null || echo 0
}

# 1. 可选：批量造历史订单
if [[ "$GENERATE" != "0" ]]; then
    python3 ml/generate_orders.py "$GENERATE"
fi
# 确保今日有订单（避免 ads 最新日全 0）
python3 ml/ensure_today_orders.py

# 2. ads_* 为空、或今日那行仍是全 0，则跑 collector（Linux 正式路径）
#    今日这行即使存在也可能是上一次留下的 0 占位，光看"表非空"会漏掉这种情况
HOURLY="$(sqlq "SELECT COUNT(*) FROM ads_station_hourly;")"
TODAY_ORDERS="$(sqlq "SELECT COALESCE(SUM(orders),0) FROM ads_station_hourly WHERE stat_date = date('now','localtime');")"
if [[ "$HOURLY" == "0" || "$TODAY_ORDERS" == "0" ]]; then
    if [[ ! -x collector/ads-collector ]]; then
        echo "[!] 需要刷新 ads_*（空库或今日无数据），但 collector/ads-collector 未编译。"
        echo "    cd collector && qmake6 collector.pro && make -j4"
        exit 1
    fi
    # 比对运行前后的指纹，而不是"表是否非空"：表非空只说明历史上有过数据，
    # 据此立刻 kill 会在 collector 首轮 aggregate 提交前把它杀掉——SIGTERM 回滚
    # 未提交事务，刚补的单永远进不了 ads_*。
    BEFORE="$(sqlq "SELECT COALESCE(MAX(updated_at),'') || '|' || COALESCE(SUM(orders),0) FROM ads_station_hourly;")"
    echo ">>> 运行 ads-collector（首轮回填后自动结束，约 10~60 秒）..."
    (cd collector && ./ads-collector) &
    COL_PID=$!
    CHANGED=0
    for _ in $(seq 1 180); do
        sleep 1  # 先等待再检查：至少给 collector 1 秒完成首轮
        NOW_FP="$(sqlq "SELECT COALESCE(MAX(updated_at),'') || '|' || COALESCE(SUM(orders),0) FROM ads_station_hourly;")"
        if [[ "$NOW_FP" != "$BEFORE" ]]; then
            CHANGED=1
            break
        fi
    done
    kill "$COL_PID" 2>/dev/null || true
    wait "$COL_PID" 2>/dev/null || true
    if [[ "$CHANGED" != "1" ]]; then
        echo "[!] collector 超时（180s）或失败，请手动: cd collector && ./ads-collector"
        exit 1
    fi
    echo "    ads_* 已刷新，collector 已停止"
fi

# 3. 导出到 HDFS 镜像（本地 ml/data/hdfs/，可选上传真实 HDFS）
if [[ "$SKIP_EXPORT" == "0" ]]; then
    python3 ml/export_to_hdfs.py --clean
    if [[ -n "${HDFS_URI:-}" ]]; then
        python3 ml/export_to_hdfs.py --upload
    fi
fi

# 4. PySpark 清洗 + 多维分析（第二阶段，本地无 Hadoop 时读 CSV 镜像）
if python3 -c "import pyspark" 2>/dev/null; then
    python3 ml/pyspark_clean.py || echo "[!] pyspark_clean 失败，继续..."
    python3 ml/pyspark_analytics.py || echo "[!] pyspark_analytics 失败，继续..."
else
    echo "[!] 未安装 pyspark，跳过 PySpark 步骤（pip install pyspark）"
fi

# 5. 预测
if [[ "$USE_SPARK" == "1" ]]; then
    RUN_TS="$(date '+%Y-%m-%d %H:00:00')"
    SPARK_START="$(date +%s)"
    spark-sql -f ml/spark/forecast.sql --hiveconf "run_ts=${RUN_TS}"
    spark-sql -f ml/spark/analytics.sql
    # Spark 结果落在 HDFS Parquet，必须先导出成 ml/output/*.csv 才能回写 SQLite。
    # 若不加校验，上一次 predict_local 留下的旧 CSV 会被当成 Spark 结果静默写进库。
    LOAD_CSV=ml/output/load_forecast.csv
    TIME_CSV=ml/output/time_forecast.csv
    if [[ ! -f "$LOAD_CSV" || ! -f "$TIME_CSV" \
          || "$(stat -c %Y "$LOAD_CSV" 2>/dev/null || echo 0)" -lt "$SPARK_START" ]]; then
        echo "[!] $LOAD_CSV 不存在，或早于本次 Spark 运行。"
        echo "    Spark 结果还没导出成 CSV。请先把 /charging/ads/*_result 导出到"
        echo "    ml/output/load_forecast.csv 与 ml/output/time_forecast.csv，再执行:"
        echo "      python3 ml/sync_to_sqlite.py"
        exit 1
    fi
    python3 ml/sync_to_sqlite.py
else
    python3 ml/predict_local.py
fi

# 6. 模型评估
python3 ml/evaluate.py || echo "[!] evaluate 失败，继续..."

python3 ml/verify.py
echo "完成。启动 charge-server 后可测 forecast.list / timeforecast.list"
