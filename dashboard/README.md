# 数据可视化大屏

**Flask REST API + Vue3/DataV 前端**（第二阶段），只读 `db/charge.db` 中的 `ads_*` 与 ML 预测表。

## 启动

```bash
# 1. 确保 ads_*、预测表、评估报告有数据
python3 ml/run_pipeline.py --generate 3000

# 2. 构建 Vue 大屏（需 Node ≥23，首次或改前端后执行）
cd dashboard-web && npm install && npm run build && cd ..

# 3. 启动 Flask
pip install -r dashboard/requirements.txt
python3 dashboard/app.py

# 4. 冒烟测试（另开终端，dashboard 已启动）
python3 tools/test_dashboard.py
```

浏览器打开：http://127.0.0.1:5000

未构建 Vue 时自动回退到 `templates/index.html`（ECharts 旧版）。

管理端 **charge-admin** 侧边栏「数据大屏」可直接内嵌本页（需 Qt WebEngine）；环境变量 `DASHBOARD_URL` 可改地址。

## 展示内容

- DataV 边框/KPI 数字翻牌
- 平台 KPI、营收趋势、电桩状态、利用率仪表盘
- 充电高峰、区域分布、电站排行、24h 历史
- 交叉对比：工作日/周末、电站×小时热力图
- 雷达图（利用率/周转/故障）、ML 负荷/时长预测
- WMA 模型评估 MAE/RMSE/MAPE

## 环境变量

- `CHARGE_DB` — 数据库路径，默认 `db/charge.db`
- `DASHBOARD_PORT` — 端口，默认 5000

详见 [docs/phase2.md](../docs/phase2.md)
