# 东软电动汽车充电桩应用管理平台

## 目录分工

| 目录 | 负责人 | 说明 |
|------|--------|------|
| `client/` | 组员 A | Qt 用户端 |
| `admin/` | 组员 B | Qt 管理端 |
| `server/` | 组员 C | Socket 业务服务 |
| `dashboard/` | 组员 D | Web 大屏 |
| `ml/` | 组员 D | 机器学习脚本 |
| `docs/` | 全员 | 架构与协议文档 |

## 快速启动（Linux 虚拟机）

### 1. 拉代码

```bash
git clone https://github.com/river-cc-77/task_ShortSemester.git
cd task_ShortSemester
```

### 2. 安装依赖

```bash
sudo apt update
sudo apt install -y qt6-base-dev qt6-charts-dev libqt6sql6-sqlite g++ make python3
```

### 3. 创建数据库

```bash
cd db
sqlite3 charge.db < schema.sql
sqlite3 charge.db < seed.sql
cd ..
```

### 4. 启动 server（必须先启动）

```bash
cd server
qmake6 charge-server.pro && make -j4
./charge-server
```

应看到 `Server listening on port 9000`。

### 5. 自动化测试

另开终端（server 已启动）：

```bash
# Server API 集成测试（约 50 项，含 65s 低余额用例）
python3 tools/test_server.py

# Collector 对账测试（需先跑一轮 ads-collector）
cd collector && ./ads-collector 10
# 另开终端：
python3 tools/test_collector.py
```

### 6. 启动客户端

**用户端（A）：**

```bash
cd client
qmake6 charge-client.pro && make -j4
./charge-client
```

**管理端（B）：**

```bash
cd admin
qmake6 charge-admin.pro && make -j4
./charge-admin
```

## 当前可用 API

完整协议见 `docs/protocal.md`，架构见 `docs/architecture.md`。

| 阶段 | 命令 | 说明 |
|------|------|------|
| P0 | `ping` / `user.login` / `admin.login` | 连通与登录 |
| P1 | `station.list` / `station.detail` | 找桩 |
| P1 | `charge.*` / `order.*` | 预约、充电、结算 |
| P1 | `user.profile.update` / `user.recharge` | 资料与充值 |
| P1 | `station.favorite.*` / `announcement.list` | 收藏与公告 |
| P1 | `station.admin.list` / `station.create` | 管理端电站 |
| P1 | `pile.list` / `pile.restart` | 管理端电桩 |
| P1 | `user.admin.list` / `user.freeze` | 管理端用户 |
| P1 | `stats.overview` / `order.list` | 统计与订单 |

## 演示账号

| 类型 | 账号 | 密码/说明 |
|------|------|-----------|
| 用户 | 13800138001 | 有余额，可正常充电 |
| 用户 | 13800138002 | 有待支付订单（`order.check_open` 演示） |
| 用户 | 13800138003 | 普通用户 |
| 用户 | 13800138004 | 低余额（结算不足演示） |
| 用户 | 13800138006 | 已冻结 |
| 管理员 | admin | 123456 |

## 测试清单

### 自动化（`tools/test_server.py`）

| 类别 | 覆盖内容 |
|------|----------|
| P0 | ping、用户/管理员登录、错误密码、冻结账号 |
| 找桩 | 距离排序、`keyword` 过滤、详情、不存在站点 |
| 充电 | 预约→启动→进度→停止→结算全流程 |
| 业务规则 | 待支付拦截、ORDER_EXISTS、PILE_FAULT、PILE_BUSY、BALANCE_NOT_ENOUGH、越权 FORBIDDEN |
| 管理端 | 列表、建站、重启桩、冻结/解冻、统计 |
| 异常 | 非法充值、未知命令、无效 token |

运行：`python3 tools/test_server.py`（server 监听 9000）

### 自动化（`tools/test_collector.py`）

| 类别 | 覆盖内容 |
|------|----------|
| 对账 | `ads_daily_stats` 营收/单量 vs 业务表 |
| 台账 | `ads_order_fact` 行数与 excluded 分布 |
| 派生指标 | completion_rate、utilization、peak_hour 等列非空 |
| 多维表 | station/pile/hour/region 有数据 |

运行前须重建库并跑 collector：`collector/README.md`

### 待实现后补测（代码尚未完成）

| 功能 | 预期测试 |
|------|----------|
| 预约超 3h 未启动自动取消 | 改 `reserve_at` 为 3h 前 → 订单作废、桩回闲置、提示「预约已超时，请重新预约」 |
| `order.admin.settle` | 管理员代结算待支付单 + 写 operation_log |
| `station.create` 站名重复 | 返回「站名已存在」 |
| `pile.update` / `pile.delete` | 改桩信息、删闲置桩 |
| `forecast.list` / `event.push` | 第二阶段 ML 与推送 |

### 手工 / UI 联调

| 模块 | 负责人 | 要点 |
|------|--------|------|
| `client/` | 潘尹涛 | 登录、找桩、充电页拦截待支付、断网提示 |
| `admin/` | 韦迪安 | KPI 图表、建站/冻结 UI、桩重启 |
| `dashboard/` | 俞莫凡 | 读 `ads_*` 大屏展示 |
| 并发 | 全员 | server + collector 同时跑，无长时间 locked |

## 协作规则

1. **不要提交** `db/charge.db`（各人生成）
2. 改接口顺序：**protocal.md → server → client/admin**
3. 每人只改自己负责的目录，用分支开发后合并到 `main`
4. 进度表：`TaskArrangement.xlsx`

## 常见问题

**client/admin 提示无法连接服务器**  
→ 先确认 `charge-server` 在运行，端口 9000 未被占用。

**admin 登录失败**  
→ 重建数据库：`cd db && rm -f charge.db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql`

**qmake6 不存在**  
→ 尝试 `qmake`，或安装 `qt6-base-dev`。

**GitHub连接被拒绝**
→没开梯子/尝试切换梯子节点/直接登录GitHub下载zip文件