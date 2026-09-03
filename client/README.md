# charge-client（用户端）

组员 A 在此目录继续开发找桩、充电、我的等页面。

## 当前骨架

- `apiclient.cpp` — TCP JSON 通信（与 `tools/test_server.py` 相同帧格式）
- `loginwindow.cpp` — 手机号登录
- `mainwindow.cpp` — 登录后主界面 + 附近充电站列表（`station.list`）

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev

cd client
qmake6 charge-client.pro
make -j4
```

## 运行前

1. 启动 server（见 `../server/README.md`）
2. 运行 `./charge-client`

## 演示账号

- 13800138001（正常，有余额）
- 13800138006（冻结，登录应失败）

## A 后续任务

在现有骨架上扩展：电站详情、充电流程、个人资料、地图导航等。  
协议见 `../docs/protocal.md`，勿直接访问数据库。
