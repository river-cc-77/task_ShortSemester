#!/usr/bin/env bash
# ML 预测流水线（Ubuntu 22.04 / Linux 验收环境）
#
# 用法（在项目根目录）:
#   bash ml/run_pipeline.sh              # 导出 + 预测（需已跑 collector）
#   bash ml/run_pipeline.sh --generate 3000   # 造单 → collector → 预测
#   bash ml/run_pipeline.sh --spark      # Spark SQL 路径（需 Hadoop/Hive）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GENERATE=0
USE_SPARK=0
SKIP_EXPORT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generate) GENERATE="${2:-3000}"; shift 2 ;;
        --spark)    USE_SPARK=1; shift ;;
        --skip-export) SKIP_EXPORT=1; shift ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# 1. 可选：批量造历史订单
if [[ "$GENERATE" != "0" ]]; then
    python3 ml/generate_orders.py "$GENERATE"
fi

# 2. ads_* 为空则跑 collector（Linux 正式路径）
HOURLY=$(sqlite3 db/charge.db "SELECT COUNT(*) FROM ads_station_hourly;" 2>/dev/null || echo 0)
if [[ "$HOURLY" == "0" ]]; then
    if [[ ! -x collector/ads-collector ]]; then
        echo "[!] ads_station_hourly 为空，且 collector/ads-collector 未编译。"
        echo "    cd collector && qmake6 collector.pro && make -j4"
        exit 1
    fi
    echo ">>> 运行 ads-collector（首轮回填后自动结束，约 10~60 秒）..."
    (cd collector && ./ads-collector) &
    COL_PID=$!
    for _ in $(seq 1 180); do
        HOURLY=$(sqlite3 db/charge.db "SELECT COUNT(*) FROM ads_station_hourly;" 2>/dev/null || echo 0)
        if [[ "$HOURLY" != "0" ]]; then
            kill "$COL_PID" 2>/dev/null || true
            wait "$COL_PID" 2>/dev/null || true
            echo "    ads_station_hourly 已有 ${HOURLY} 行，collector 已停止"
            break
        fi
        sleep 1
    done
    if [[ "$HOURLY" == "0" ]]; then
        kill "$COL_PID" 2>/dev/null || true
        echo "[!] collector 超时（180s）或失败，请手动: cd collector && ./ads-collector"
        exit 1
    fi
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
    spark-sql -f ml/spark/forecast.sql --hiveconf "run_ts=${RUN_TS}"
    spark-sql -f ml/spark/analytics.sql
    # 需先将 Spark 结果导出为 ml/output/*.csv，再:
    python3 ml/sync_to_sqlite.py
else
    python3 ml/predict_local.py
fi

# 6. 模型评估
python3 ml/evaluate.py || echo "[!] evaluate 失败，继续..."

python3 ml/verify.py
echo "完成。启动 charge-server 后可测 forecast.list / timeforecast.list"
