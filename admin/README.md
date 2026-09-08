# charge-admin（PC 管理端）

组员 B 在此目录继续开发 KPI 图表、电桩/电站/用户管理等页面。

## 当前骨架

- `apiclient.cpp` — TCP JSON 通信
- `loginwindow.cpp` — 管理员登录（`admin.login`）
- `mainwindow.cpp` — 宽屏主界面壳子（960×600）

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev qt6-charts-dev

cd admin
qmake6 charge-admin.pro
make -j4
```

## 运行前

1. 启动 server（见 `../server/README.md`）
2. 运行 `./charge-admin`

## 演示账号

- admin / 123456

## B 后续任务

在 `MainWindow` 上增加侧边栏/Tab，接入 `stats.overview`、`pile.list` 等 API。  
需要 QChart 时在 `.pro` 中已预留 `qt6-charts-dev` 依赖，可在 `.pro` 增加 `QT += charts`。
