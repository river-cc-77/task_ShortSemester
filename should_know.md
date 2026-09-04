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

### 5. 测试协议（可选）

另开终端：

```bash
python3 tools/test_server.py
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

| 阶段 | 命令 | 说明 |
|------|------|------|
| P0 | `ping` | 连通测试 |
| P0 | `user.login` | 用户手机号登录 |
| P0 | `admin.login` | 管理员登录 |
| P1 | `station.list` | 附近充电站（需用户 token） |
| P1 | `station.detail` | 电站详情（需用户 token） |

完整协议：`docs/protocal.md`  
架构说明：`docs/architecture.md`

## 演示账号

| 类型 | 账号 | 密码/说明 |
|------|------|-----------|
| 用户 | 13800138001 | 有余额 |
| 用户 | 13800138006 | 已冻结 |
| 管理员 | admin | 123456 |

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