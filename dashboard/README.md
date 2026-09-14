# 数据可视化大屏

Flask + ECharts，只读 `db/charge.db` 中的 `ads_*` 与 ML 预测表。

## 启动

```bash
# 1. 确保 ads_* 与预测表有数据（Linux 优先 collector，Windows 可用 run_pipeline）
python3 ml/run_pipeline.py --generate 3000    # 造单 + bootstrap/collector + 预测
# 或仅补 ads 表: python3 ml/bootstrap_ads.py && python3 ml/predict_local.py

# 2. 启动大屏
pip install -r dashboard/requirements.txt
python3 dashboard/app.py

# 3. 冒烟测试（另开终端，dashboard 已启动）
python3 tools/test_dashboard.py
```

浏览器打开：http://127.0.0.1:5000

管理端 **charge-admin** 侧边栏「数据大屏」可直接内嵌本页（需 Qt WebEngine）；环境变量 `DASHBOARD_URL` 可改地址。

## 展示内容

- 平台 KPI（营收、订单、利用率）
- 近 30 日营收趋势
- 今日（或最近一日）充电高峰曲线
- 各站负荷预测（1h/6h/24h）
- 充电时长与高峰时段预测
- 近 30 天 24 小时历史充电量分布
- 工作日 vs 周末充电对比
- 电站排行、电桩状态分布

## 环境变量

- `CHARGE_DB` — 数据库路径，默认 `db/charge.db`
- `DASHBOARD_PORT` — 端口，默认 5000
