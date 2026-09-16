# 第二阶段技术栈对照

本目录说明答辩/验收所需的 **Python + Hadoop/Spark + Flask + Vue3/DataV** 栈，与 Phase 1（Qt client/server/admin/collector）解耦，主要改动在 `ml/`、`dashboard/`、`dashboard-web/`。

## 技术栈清单

| 要求 | 实现位置 | 说明 |
|------|----------|------|
| Python 3.11 / 3.12 | 全项目 Python 脚本 | Ubuntu: `python3.11` 或 `python3.12` |
| Hadoop 3.x 存储 | `ml/export_to_hdfs.py`, `ml/hive/hive_ddl.sql` | 本地镜像 `ml/data/hdfs/`；答辩机设 `HDFS_URI` 上传 |
| Spark-SQL / PySpark | `ml/spark/*.sql`, `ml/pyspark_*.py` | ≥10 维分析 + 2 组交叉对比 |
| Flask Web API | `dashboard/app.py` | REST `/api/*` |
| Node ≥23 + Vue3 | `dashboard-web/` | Vite 构建产物 → `dashboard/static/dist/` |
| DataV 大屏 | `@kjgl77/datav-vue3` | BorderBox / Decoration 装饰 + ECharts 多图表 |
| ML + 模型评估 | `ml/predict_local.py`, `ml/evaluate.py` | WMA 预测；MAE/RMSE/MAPE 留一日验证 |

## 一键流水线（Linux 推荐）

```bash
# Python 依赖
pip install -r ml/requirements.txt
pip install -r dashboard/requirements.txt

# ML：造数 → 导出 HDFS 镜像 → PySpark 分析 → 预测 → 评估
bash ml/run_pipeline.sh --generate 3000

# 或 Python 入口（Windows 开发可加 --bootstrap-ads）
python3 ml/run_pipeline.py --generate 3000

# 校验
python3 ml/verify.py
python3 ml/evaluate.py   # 单独重跑评估
cat ml/output/evaluation.json
```

## Hadoop / Spark 答辩环境

```bash
export HDFS_URI=hdfs://namenode:8020
python3 ml/export_to_hdfs.py --upload
hive -f ml/hive/hive_ddl.sql
spark-sql -f ml/spark/forecast.sql
spark-sql -f ml/spark/analytics.sql
```

PySpark 本地（无 Hadoop，读 CSV 镜像）：

```bash
pip install pyspark
python3 ml/pyspark_clean.py
python3 ml/pyspark_analytics.py
# 输出 -> ml/output/analytics/
```

## 多维分析维度（≥8）

1. 平台日 KPI — `platform_daily`
2. 电站排行 — `station_rank`
3. 24h 负荷曲线 — `hourly_profile`
4. 区域分布 — `region_stats`
5. 电桩状态 — `pile_status`
6. 电站利用率 — `station_util`
7. 用户活跃 — `user_activity`
8. 故障率 — `fault_rate`
9. **交叉对比 1** 工作日 vs 周末 — `weekday_weekend`
10. **交叉对比 2** 电站 × 小时 — `station_hour_matrix`

## 可视化大屏

```bash
# 1. 构建 Vue3 + DataV 前端（需 Node ≥23）
cd dashboard-web
npm install
npm run build

# 2. 启动 Flask（优先服务 static/dist，无构建则回退 templates/index.html）
cd ..
python3 dashboard/app.py

# 3. 测试
python3 tools/test_dashboard.py
```

浏览器：`http://127.0.0.1:5000`

图表类型：折线/面积、玫瑰饼、仪表盘、柱+折双轴、环形饼、横向柱、热力图、雷达、散点、分组柱。

## API 列表

- `/api/kpi` — 平台 KPI
- `/api/revenue_trend` — 营收趋势
- `/api/pile_status` — 电桩状态
- `/api/station_hourly_today` — 今日高峰
- `/api/station_rank` — 电站排行
- `/api/hourly_history` — 24h 历史
- `/api/weekday_weekend` — 工作日/周末
- `/api/station_util` — 利用率雷达
- `/api/region_stats` — 区域分布
- `/api/station_hour_matrix` — 热力矩阵
- `/api/load_forecast` / `/api/time_forecast` — ML 预测
- `/api/ml_evaluation` — 模型评估指标

## 测试用例（3×8 = 24 条）

与第一阶段 `tools/test_integration.py` 对齐，第二阶段用例见：

- 用例表：`python3 tools/export_testcase_phase2_xlsx.py` → `04测试用例-第二阶段.xls`（版式对齐第一阶段 `03测试用例.xls`）
- 自动化：`python3 tools/test_integration_phase2.py --with-dashboard --with-hdfs`

| 功能点 | 编号 | 内容 |
|--------|------|------|
| ML 预测与评估 | TC-P2-01～08 | ads_*、forecast 表、evaluation.json |
| Dashboard API | TC-P2-09～16 | Flask `/api/*` 黑盒 |
| Hadoop/Spark 链路 | TC-P2-17～24 | HDFS 镜像、PySpark 10 维、HDFS 远程 |

## 数据集提交

### A. 原始数据（未清洗，推荐答辩演示「采集→处理」）

```bash
cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql && cd ..
python3 ml/generate_orders.py 3000          # 只造业务订单，不跑 collector
python3 ml/export_raw_dataset.py --clean --copy-db
bash ml/package_dataset.sh --raw
```

提交内容：`ml/delivery/phase2_raw_dataset_YYYYMMDD.zip`  
内含 `charge.db`、`schema.sql`、`raw/charging/ods/`（charge_order 等原始表 CSV）。

### B. 处理后数据（collector + ML 全链路结果）

```bash
bash ml/run_pipeline.sh --generate 3000
python3 ml/export_to_hdfs.py --clean --upload   # 答辩机
bash ml/package_dataset.sh
bash ml/package_dataset.sh --upload             # 答辩机上传 HDFS
python3 tools/test_integration_phase2.py --with-dashboard --with-hdfs
```

提交内容：`ml/delivery/phase2_dataset_YYYYMMDD.zip`（`hdfs/charging/` ads 镜像 + `output/` ML 产出）。

## 模型评估说明

`ml/evaluate.py` 对最近 N 日做**留一日前向验证**：仅用测试日之前的历史同 hour 做 WMA，与当日实际 `kwh` / `duration_min` 对比，输出 MAE、RMSE、MAPE 到 `ml/output/evaluation.json`，大屏底部展示。
