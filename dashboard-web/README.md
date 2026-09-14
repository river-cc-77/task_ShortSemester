# 数据大屏前端（Vue3 + DataV）

第二阶段可视化前端，构建后由 Flask `dashboard/app.py` 托管。

## 要求

- **Node.js ≥ 23**
- npm 或 pnpm

## 开发

```bash
cd dashboard-web
npm install
npm run dev    # http://127.0.0.1:5173 ，API 代理到 Flask :5000
```

另开终端启动 Flask：`python3 dashboard/app.py`

## 生产构建

```bash
npm run build
```

产物输出到 `dashboard/static/dist/`，Flask 根路径 `/` 优先服务该目录。

## 技术

- Vue 3 + Vite 6
- `@kjgl77/datav-vue3` — BorderBox、Decoration 等 DataV 组件
- ECharts + vue-echarts — 折线、柱、饼、热力、雷达、仪表盘、散点等

详见 [docs/phase2.md](../docs/phase2.md)
