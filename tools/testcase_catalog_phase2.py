"""24 条第二阶段集成测试用例（3 功能点 × 8 条），与 test_integration_phase2.py 对齐。

cases 每项：(编号, 说明, 输入, 预期, 备注)
"""

from __future__ import annotations

PROJECT_NAME = "东软电动汽车充电桩应用管理平台"
VERSION = "V2.0"
AUTHOR = "项目组"
DATE = "2026-09-15"

MODULES = [
    {
        "module": "ML 预测与模型评估",
        "feature": "ads_* 分析表、load_forecast / time_forecast 预测表、evaluation.json 模型评估。",
        "purpose": "黑盒验证 ML 流水线产出完整、预测值合法、评估指标可展示。",
        "precondition": "已执行 schema.sql + seed.sql；bash ml/run_pipeline.sh --generate 3000。",
        "cases": [
            (
                "TC-P2-01",
                "ads 小时表非空",
                "查询 ads_station_hourly 行数",
                "count > 0",
                "collector 或 bootstrap 已写入",
            ),
            (
                "TC-P2-02",
                "ads 日表有真实订单日",
                "查询 ads_daily_stats WHERE order_count > 0",
                "至少 1 行",
                "避免仅占位全 0",
            ),
            (
                "TC-P2-03",
                "负荷预测行数",
                "SELECT COUNT(*) FROM load_forecast",
                "行数 = 站数 × 3（1h/6h/24h）",
                "predict_local 或 Spark 同步",
            ),
            (
                "TC-P2-04",
                "时长预测行数",
                "SELECT COUNT(*) FROM time_forecast",
                "行数 = 站数 × 3",
                "与 load_forecast 对称",
            ),
            (
                "TC-P2-05",
                "空闲桩预测非负",
                "load_forecast.predicted_idle_piles >= 0",
                "无负值行",
                "边界校验",
            ),
            (
                "TC-P2-06",
                "高峰小时合法",
                "time_forecast.predicted_peak_hour",
                "NULL 或 0～23",
                "高峰时段提醒数据源",
            ),
            (
                "TC-P2-07",
                "预测非全零",
                "load_forecast.predicted_load > 0；time_forecast.predicted_avg_duration_min > 0",
                "至少部分行 > 0",
                "防止 ads 未刷新导致失效预测",
            ),
            (
                "TC-P2-08",
                "模型评估指标",
                "读取 ml/output/evaluation.json",
                "含 model、load_kwh/duration_min 的 mae/rmse",
                "evaluate.py 留一验证",
            ),
        ],
    },
    {
        "module": "Dashboard Flask API",
        "feature": "Flask REST `/api/*` 大屏数据接口；读 ads_* 与预测表。",
        "purpose": "黑盒验证可视化大屏后端 API 结构与数据可用性。",
        "precondition": "python3 dashboard/app.py 已启动；CHARGE_DB 指向有效 charge.db。",
        "cases": [
            (
                "TC-P2-09",
                "平台 KPI",
                "GET /api/kpi",
                "含 station_count、pile_count 等字段",
                "KPI 数字翻牌",
            ),
            (
                "TC-P2-10",
                "营收趋势",
                "GET /api/revenue_trend",
                "非空列表，含 stat_date、total_revenue",
                "折线/面积图",
            ),
            (
                "TC-P2-11",
                "充电高峰曲线",
                "GET /api/station_hourly_today",
                "非空，含 stat_hour",
                "今日单业务日高峰",
            ),
            (
                "TC-P2-12",
                "24h 历史分布",
                "GET /api/hourly_history",
                "非空列表",
                "30 日按小时汇总",
            ),
            (
                "TC-P2-13",
                "工作日周末对比",
                "GET /api/weekday_weekend",
                "含 weekday 与 weekend",
                "交叉对比 1",
            ),
            (
                "TC-P2-14",
                "电站排行",
                "GET /api/station_rank",
                "≤10 条，含 name",
                "横向柱图",
            ),
            (
                "TC-P2-15",
                "负荷预测 API",
                "GET /api/load_forecast",
                "horizon 含 1h、6h、24h",
                "与 server forecast.list 同源表",
            ),
            (
                "TC-P2-16",
                "模型评估 API",
                "GET /api/ml_evaluation",
                "含 model 及评估指标",
                "大屏底部展示",
            ),
        ],
    },
    {
        "module": "Hadoop/Spark 数据链路",
        "feature": "HDFS 本地镜像、PySpark 多维分析、可选远程 HDFS 校验。",
        "purpose": "黑盒验证 Hadoop 存储层与 Spark 分析产出满足答辩要求。",
        "precondition": "python3 ml/export_to_hdfs.py；python3 ml/pyspark_analytics.py。",
        "cases": [
            (
                "TC-P2-17",
                "HDFS 本地镜像四表",
                "检查 ml/data/hdfs/charging/ 下 dws + dim CSV",
                "station_hourly、daily、station、pile 均存在",
                "export_to_hdfs.py",
            ),
            (
                "TC-P2-18",
                "小时表 CSV 有数据",
                "station_hourly.csv 行数",
                "> 0",
                "dws 层",
            ),
            (
                "TC-P2-19",
                "日表 CSV 有数据",
                "station_daily.csv 行数",
                "> 0",
                "dws 层",
            ),
            (
                "TC-P2-20",
                "维表 CSV 完整",
                "station.csv、pile.csv 行数",
                "均 > 0",
                "dim 层",
            ),
            (
                "TC-P2-21",
                "PySpark 分析维度",
                "ml/output/analytics/ 子目录数",
                "≥ 8 个维度",
                "含平台/站/区域/用户等",
            ),
            (
                "TC-P2-22",
                "交叉对比-工作日周末",
                "analytics/weekday_weekend/*.csv",
                "含 weekday/weekend 数据",
                "交叉对比 1",
            ),
            (
                "TC-P2-23",
                "交叉对比-电站×小时",
                "analytics/station_hour_matrix/*.csv",
                "含 station、stat_hour 列",
                "交叉对比 2 / 热力图",
            ),
            (
                "TC-P2-24",
                "HDFS 远程目录",
                "hdfs dfs -ls /charging/",
                "可访问且含 dws 等目录",
                "答辩机 node100，加 --with-hdfs",
            ),
        ],
    },
]

TOTAL_CASES = sum(len(m["cases"]) for m in MODULES)
