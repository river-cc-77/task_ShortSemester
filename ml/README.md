# 机器学习智能分析子系统

**运行环境：Linux（Ubuntu 22.04，与 server/collector 一致）**

负荷预测 + 充电时间预测。`collector` 产出 `ads_*` 特征表 → 导出 HDFS / Hive → **Spark SQL** 预测 → 写回 `load_forecast` / `time_forecast` → `forecast.list` / `timeforecast.list`。

## 数据从哪来

```text
charge.db（业务订单，server 写入）
    ↓ 可选: python3 ml/generate_orders.py 3000
collector/ads-collector（C++，Linux 必跑）
    ↓ ads_station_hourly / ads_station_daily 等
python3 ml/export_to_hdfs.py [--upload]
    ↓ /charging/dws/…（HDFS）+ ml/data/hdfs/ 本地镜像
hive -f ml/hive/hive_ddl.sql
spark-sql -f ml/spark/forecast.sql
    ↓ 或同算法: python3 ml/predict_local.py（无 Hadoop 时的等价实现）
load_forecast + time_forecast
    ↓
charge-server → forecast.list / timeforecast.list
```

## 新手一键跑通（Ubuntu 22.04）

```bash
# 0. 依赖（与 collector 相同）
sudo apt install -y qt6-base-dev libqt6sql6-sqlite sqlite3 python3

# 1. 重建库
cd db
rm -f charge.db
sqlite3 charge.db < schema.sql
sqlite3 charge.db < seed.sql
cd ..

# 2. 编译 collector（若尚未编译）
cd collector
qmake6 collector.pro && make -j4
cd ..

# 3. 造数据 + collector 聚合 + 预测（推荐）
bash ml/run_pipeline.sh --generate 3000

# 4. 校验
python3 ml/verify.py
sqlite3 db/charge.db "SELECT horizon, COUNT(*) FROM load_forecast GROUP BY horizon;"
```

## 日常预测（已有足够订单时）

```bash
cd collector && ./ads-collector    # 刷新 ads_*
cd .. && bash ml/run_pipeline.sh   # 导出 + predict_local
```

## 脚本说明

| 脚本 | 作用 |
|------|------|
| `run_pipeline.sh` | **Linux 主入口**：检测 ads → collector → 导出 → 预测 → verify |
| `generate_orders.py` | 批量插入模拟「已完成」订单 |
| `export_to_hdfs.py` | 导出 ads 表 CSV；`HDFS_URI=hdfs://… --upload` 上传 HDFS |
| `predict_local.py` | 与 Spark SQL 同算法，写 SQLite + `ml/output/*.csv` |
| `sync_to_sqlite.py` | Spark 产出 CSV 回写 SQLite |
| `hive/hive_ddl.sql` | Hive 外部表 DDL |
| `spark/forecast.sql` | Spark SQL 预测（仅 SQL，不用 MLlib） |
| `verify.py` | 离线校验预测表行数与字段 |

`bootstrap_ads.py` 仅作**无法编译 collector 时的应急**（产出 `ads_station_*` + `ads_daily_stats`），**验收/demo 请一律用 ads-collector**。Windows 开发可 `python ml/run_pipeline.py --generate 3000` 自动走 bootstrap。

## 算法（加权移动平均 WMA）

- **负荷** `predicted_load` = 过去 7 天同 `stat_hour` 的 `kwh` **加权平均**  
- **充电时长** `predicted_avg_duration_min` = 同 hour 的 `duration_min` **加权平均**  
- **权重** `w = 1 / (days_ago + 1)`（昨天 0.5，前天 0.33…，越近权重越大）  
- **空闲桩** `predicted_idle_piles` = `total_piles - CEIL(predicted_load / avg_power_kw)`  
- **高峰小时** `predicted_peak_hour` = `ads_station_daily.peak_hour` 或 orders 最大的 hour  

Horizon：`1h` / `6h` / `24h`，每站各一行（共 站数×3 行）。

## Spark + Hive（课程大数据栈）

```bash
# 导出并上传 HDFS
export HDFS_URI=hdfs://localhost:9000
python3 ml/export_to_hdfs.py --clean --upload

# 建 Hive 表
hive -f ml/hive/hive_ddl.sql

# Spark SQL 预测
spark-sql -f ml/spark/forecast.sql \
  --hiveconf run_ts="$(date '+%Y-%m-%d %H:00:00')"

# 将 Spark 结果导出为 ml/output/load_forecast.csv、time_forecast.csv 后:
python3 ml/sync_to_sqlite.py
```

无 Hadoop 集群时，用 `predict_local.py` 即可，算法与 `spark/forecast.sql` 一致。

## 业务展示

| 位置 | 功能 |
|------|------|
| **用户端** `charge-client` | 充电站列表 ⭐推荐、详情页预测负荷/时长 |
| **管理端** `charge-admin` | 电站页 ⚠ 负荷预警、侧边栏「智能预测」页 |
| **大屏** `dashboard/app.py` | ECharts 展示 KPI、预测、高峰曲线 |

## 业务接口

- `forecast.list` — 读 `load_forecast`（管理端负荷预警 ⚠）
- `timeforecast.list` — 读 `time_forecast`（充电时间预测）
- `station.list` — 含 `recommended` / `predicted_*` 智能推荐字段
- `station.detail` — 含 `forecast` 预测摘要

```json
{ "cmd": "forecast.list", "data": { "horizon": "1h", "station_id": 1 } }
{ "cmd": "timeforecast.list", "data": { "horizon": "6h" } }
```

## 与 server / collector 并发

ML 脚本与 `charge-server`、`ads-collector` 共用 `db/charge.db`。建议在 collector 跑完一轮后再执行预测；server 已开 WAL，读写可并发。
