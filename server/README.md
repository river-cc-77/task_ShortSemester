# charge-server

充电桩平台 Socket 业务服务（第 1 周 P0 + P1 找桩）。

## 已实现命令

- `ping`
- `user.login`
- `admin.login`
- `station.list`
- `station.detail`

协议详见 `../docs/protocal.md`。

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev libqt6sql6-sqlite

cd server
qmake6 charge-server.pro    # 或 qmake（Qt5）
make -j4
```

## 运行前准备数据库

```bash
cd ../db
sqlite3 charge.db < schema.sql
sqlite3 charge.db < seed.sql
```

## 启动

```bash
cd server
./charge-server
```

应输出：

```text
Database: .../db/charge.db
Server listening on port 9000
```

## 快速测试（Python）

```bash
python3 ../tools/test_server.py
```

## 演示账号

| 类型 | 账号 | 说明 |
|------|------|------|
| 用户 | 13800138001 | 正常，有余额 |
| 用户 | 13800138006 | 已冻结 |
| 管理员 | admin / 123456 | 默认管理员 |
