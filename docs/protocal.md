# Socket 通信协议

> 东软电动汽车充电桩应用管理平台  
> 版本：v0.1  
> 维护：全员改协议须先改本文档，再改 `server/`，最后改 `client/` / `admin/`

---

## 1. 总体约定

| 项 | 约定 |
|----|------|
| 传输 | TCP |
| 地址 | `127.0.0.1`（本机）或虚拟机 IP |
| 端口 | `9000` |
| 编码 | UTF-8 |
| 格式 | JSON |
| 帧结构 | **4 字节大端长度** + JSON 正文 |

### 1.1 帧格式

```
┌────────────────┬──────────────────────────────┐
│ length (uint32)│ JSON body (UTF-8, length 字节) │
└────────────────┴──────────────────────────────┘
```

- `length`：仅 JSON 正文字节数，不含自身 4 字节
- 单次 JSON 建议不超过 64KB

### 1.2 请求通用格式

```json
{
  "id": "1",
  "cmd": "user.login",
  "token": "",
  "data": { }
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | 是 | 客户端生成的请求 ID，响应原样带回 |
| `cmd` | 是 | 命令名，见下文 |
| `token` | 否 | 登录后必填（`ping` 除外） |
| `data` | 否 | 业务参数对象 |

### 1.3 响应通用格式

**成功：**

```json
{
  "id": "1",
  "ok": true,
  "data": { }
}
```

**失败：**

```json
{
  "id": "1",
  "ok": false,
  "error": {
    "code": "USER_FROZEN",
    "message": "账号已被冻结，请联系客服"
  }
}
```

### 1.4 鉴权

- 登录成功返回 `token`（UUID 字符串）
- 除 `ping`、`user.login`、`admin.login` 外，其余命令须带有效 `token`
- 服务端内存维护 `token → { role, user_id/admin_id }`
- 用户端 token 仅可操作本人数据；管理端 token 可操作管理命令

### 1.5 错误码

| code | 含义 |
|------|------|
| `INVALID_JSON` | JSON 解析失败 |
| `UNKNOWN_CMD` | 未知命令 |
| `UNAUTHORIZED` | 未登录或 token 无效 |
| `FORBIDDEN` | 无权限 |
| `NOT_FOUND` | 资源不存在 |
| `INVALID_PARAM` | 参数错误 |
| `USER_FROZEN` | 用户已冻结 |
| `PILE_BUSY` | 电桩不可用 |
| `PILE_FAULT` | 电桩故障 |
| `ORDER_EXISTS` | 存在未完成订单 |
| `BALANCE_NOT_ENOUGH` | 余额不足 |
| `DB_ERROR` | 数据库错误 |

---

## 2. 命令列表概览

| cmd | 调用方 | 阶段 | 说明 |
|-----|--------|------|------|
| `ping` | 全部 | **P0** | 连通测试 |
| `user.login` | 用户端 | **P0** | 手机号登录/注册 |
| `admin.login` | 管理端 | **P0** | 管理员登录 |
| `announcement.list` | 用户端 | P1 | 首页公告 |
| `station.list` | 用户端 | P1 | 附近充电站列表 |
| `station.detail` | 用户端 | P1 | 电站详情含电桩 |
| `station.favorite.add` | 用户端 | P2 | 收藏电站 |
| `station.favorite.remove` | 用户端 | P2 | 取消收藏 |
| `station.favorite.list` | 用户端 | P2 | 收藏列表 |
| `order.check_open` | 用户端 | P1 | 检查未完成订单 |
| `order.list` | 用户端/管理端 | P1 | 订单列表 |
| `charge.reserve` | 用户端 | P1 | 预约电桩 |
| `charge.start` | 用户端 | P1 | 开始充电 |
| `charge.stop` | 用户端 | P1 | 停止充电 |
| `charge.settle` | 用户端/管理端 | P1 | 结算订单 |
| `charge.progress` | 用户端 | P1 | 查询充电进度 |
| `user.profile.update` | 用户端 | P1 | 改昵称/头像 |
| `user.recharge` | 用户端 | P1 | 余额充值 |
| `stats.overview` | 管理端 | P1 | 数据总览 KPI + 图表 |
| `pile.list` | 管理端 | P1 | 电桩列表 |
| `pile.restart` | 管理端 | P1 | 远程重启 |
| `pile.update` | 管理端 | P2 | 修改电桩 |
| `pile.delete` | 管理端 | P2 | 删除电桩 |
| `station.admin.list` | 管理端 | P1 | 电站管理列表 |
| `station.create` | 管理端 | P1 | 新增电站 |
| `user.admin.list` | 管理端 | P1 | 用户列表 |
| `user.freeze` | 管理端 | P1 | 冻结/解冻 |
| `order.admin.settle` | 管理端 | P1 | 代结算 |
| `forecast.list` | 用户端/管理端 | P2 | 负荷预测 |
| `event.push` | 服务端→客户端 | P2 | 服务端主动推送 |

> **P0** = 第 1 周必须实现；**P1** = 第 2–3 周；**P2** = 加分项

---

## 3. P0 命令详细定义（第 1 周）

### 3.1 `ping`

**请求：**

```json
{ "id": "1", "cmd": "ping", "data": {} }
```

**响应：**

```json
{
  "id": "1",
  "ok": true,
  "data": { "pong": true, "server_time": "2026-08-28 14:00:00" }
}
```

---

### 3.2 `user.login`

手机号免密登录。11 位手机号；不存在则自动注册，默认昵称 `用户+后4位`。

**请求：**

```json
{
  "id": "2",
  "cmd": "user.login",
  "data": { "phone": "13800138001" }
}
```

**成功响应：**

```json
{
  "id": "2",
  "ok": true,
  "data": {
    "token": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "user_id": 1,
    "phone": "13800138001",
    "nickname": "用户8001",
    "avatar_path": "",
    "balance": 92.5,
    "status": "正常",
    "created_at": "2026-08-26 10:24:00"
  }
}
```

**失败示例（冻结）：**

```json
{
  "id": "2",
  "ok": false,
  "error": { "code": "USER_FROZEN", "message": "账号已被冻结，请联系客服" }
}
```

**服务端逻辑：**

1. 校验手机号 11 位数字
2. 查询 `user` 表
3. 不存在 → `INSERT`，昵称 `用户` + 后 4 位
4. `status = 冻结` → 返回 `USER_FROZEN`
5. 生成 token 并缓存

---

### 3.3 `admin.login`

**请求：**

```json
{
  "id": "3",
  "cmd": "admin.login",
  "data": { "username": "admin", "password": "123456" }
}
```

**成功响应：**

```json
{
  "id": "3",
  "ok": true,
  "data": {
    "token": "admin-token-uuid",
    "admin_id": 1,
    "username": "admin"
  }
}
```

**服务端逻辑：**

1. 查 `admin` 表
2. 密码做 SHA256 后与 `password_hash` 比较（见 `seed.sql`）
3. 失败返回 `UNAUTHORIZED`，message：`账号或密码错误`

---

## 4. P1 命令详细定义（第 2–3 周）

### 4.1 `announcement.list`

**请求：** `{ "id":"10", "cmd":"announcement.list", "token":"...", "data":{} }`

**响应 data：**

```json
{
  "items": [
    { "id": 1, "title": "系统上线通知", "content": "...", "created_at": "2026-08-26 10:00:00" }
  ]
}
```

---

### 4.2 `station.list`

附近充电站，按距离升序；可结合 `load_forecast` 标记推荐。

**请求 data：**

```json
{
  "lat": 22.5431,
  "lng": 114.0579,
  "keyword": ""
}
```

- `lat/lng`：当前定位（地理编码结果）
- `keyword`：可选，按站名/地址模糊过滤

**响应 data.items[] 元素：**

```json
{
  "id": 1,
  "name": "深圳市民中心充电站",
  "address": "深圳市福田区福中三路市民中心停车场",
  "lat": 22.5431,
  "lng": 114.0579,
  "price": 1.2,
  "total_piles": 6,
  "idle_piles": 5,
  "online_rate": 0.833,
  "distance_km": 0.0,
  "recommended": true
}
```

**距离计算：** Haversine 公式，单位 km，保留 1 位小数。

**推荐规则（P2）：** `predicted_idle_piles / total_piles >= 0.5` 且负荷预测低于站点中位数 → `recommended: true`。

---

### 4.3 `station.detail`

**请求 data：** `{ "station_id": 1 }`

**响应 data：**

```json
{
  "station": { "id": 1, "name": "...", "address": "...", "price": 1.2, "online_rate": 0.833 },
  "piles": [
    {
      "id": 1,
      "pile_no": "SZ001-01",
      "type": "快充",
      "power_kw": 120.0,
      "status": "闲置",
      "can_reserve": true
    },
    {
      "id": 3,
      "pile_no": "SZ001-03",
      "type": "快充",
      "power_kw": 120.0,
      "status": "故障",
      "can_reserve": false
    }
  ]
}
```

- `can_reserve`：`status == 闲置` 时为 true

---

### 4.4 `order.check_open`

进入充电页前调用。存在「充电中」或「待支付」订单则拦截。

**请求：** `{ "cmd":"order.check_open", "token":"..." }`

**响应 data：**

```json
{
  "has_open": true,
  "order": {
    "order_no": "CD20260828002",
    "status": "待支付",
    "station_name": "深圳市民中心充电站",
    "pile_no": "SZ001-05",
    "kwh": 28.5,
    "amount": 34.2
  }
}
```

- `has_open = false` 时 `order` 为 null

---

### 4.5 `charge.reserve`

**请求 data：** `{ "pile_no": "SZ001-01" }`

**逻辑：**

1. 检查用户无「充电中/待支付」订单
2. 检查电桩 `status == 闲置`
3. 创建订单，`status = 预约`，电桩 → `预约`
4. 生成 `order_no`：`CD` + `YYYYMMDD` + 3 位序号

**响应 data：** `{ "order_no": "CD20260828003", "status": "预约" }`

---

### 4.6 `charge.start`

**请求 data：** `{ "order_no": "CD20260828003" }`

**逻辑：**

1. 订单须为「预约」且属于当前用户
2. 电桩 → `在用`，订单 → `充电中`，写 `start_at`
3. 启动服务端计费线程

**响应 data：**

```json
{
  "order_no": "CD20260828003",
  "status": "充电中",
  "start_at": "2026-08-28 15:00:00",
  "price": 1.2,
  "power_kw": 120.0
}
```

---

### 4.7 `charge.progress`

充电中轮询（建议每 1 秒）。

**请求 data：** `{ "order_no": "CD20260828003" }`

**响应 data：**

```json
{
  "order_no": "CD20260828003",
  "status": "充电中",
  "kwh": 12.5,
  "amount": 15.0,
  "elapsed_seconds": 375,
  "estimated_remain_seconds": 225
}
```

**计费公式：**

```
kwh = power_kw × elapsed_seconds / 3600
amount = round(kwh × station.price, 2)
```

> 演示加速：可在服务端配置 `time_scale = 60`（1 真实秒 = 1 模拟分钟）。

---

### 4.8 `charge.stop`

**请求 data：** `{ "order_no": "CD20260828003" }`

**逻辑：**

1. 停止计费线程
2. 订单 → `待支付`，写 `end_at`
3. 电桩 → `闲置`

**响应 data：** `{ "order_no": "...", "status": "待支付", "kwh": 30.0, "amount": 36.0 }`

---

### 4.9 `charge.settle`

**请求 data：** `{ "order_no": "CD20260828003" }`

**逻辑：**

1. 订单须为「待支付」
2. `balance >= amount` → 扣款，订单 → `已完成`，写 `wallet_log`
3. 不足 → 返回 `BALANCE_NOT_ENOUGH`
4. 更新电桩 `charge_count`、`charge_minutes`

**响应 data：** `{ "order_no": "...", "status": "已完成", "balance_after": 56.5 }`

---

### 4.10 `user.profile.update`

**请求 data：**

```json
{ "nickname": "新昵称", "avatar_path": "/home/user/avatar.png" }
```

字段可选，只更新传入的字段。

---

### 4.11 `user.recharge`

模拟充值，无真实支付。

**请求 data：** `{ "amount": 100.0 }`

**逻辑：** `balance += amount`，写 `wallet_log`（reason=`充值`）

**响应 data：** `{ "balance": 192.5 }`

---

### 4.12 `order.list`

**用户端请求 data：** `{ "status": "", "limit": 20 }`  
**管理端可额外传：** `{ "phone": "", "date_from": "2026-08-01", "date_to": "2026-08-28" }`

**响应 items[]：**

```json
{
  "order_no": "CD20260828001",
  "phone": "13800138001",
  "station_name": "深圳市民中心充电站",
  "pile_no": "SZ001-01",
  "status": "已完成",
  "reserve_at": "2026-08-28 11:20:00",
  "start_at": "2026-08-28 11:29:00",
  "end_at": "2026-08-28 12:05:00",
  "kwh": 30.0,
  "amount": 36.0
}
```

---

### 4.13 `stats.overview`

管理端数据总览。

**请求 data：** `{ "days": 7 }`  
`days` 可选 `7` 或 `30`。

**响应 data：**

```json
{
  "today_revenue": 36.0,
  "month_revenue": 2011.08,
  "total_revenue": 2225.61,
  "today_orders": 1,
  "user_count": 5,
  "revenue_trend": [
    { "date": "2026-08-22", "revenue": 39.12 },
    { "date": "2026-08-23", "revenue": 58.50 }
  ],
  "pile_status": {
    "在用": 0,
    "闲置": 24,
    "故障": 4
  },
  "station_rank": [
    { "name": "深圳市民中心充电站", "revenue": 520.0 }
  ]
}
```

---

### 4.14 `pile.list`

**请求 data：**

```json
{ "station_id": 0, "status": "", "keyword": "" }
```

`station_id = 0` 表示全部电站。

**响应 items[]：**

```json
{
  "pile_no": "SZ001-03",
  "station_name": "深圳市民中心充电站",
  "type": "快充",
  "power_kw": 120.0,
  "status": "故障",
  "charge_count": 412,
  "charge_minutes": 14800
}
```

---

### 4.15 `pile.restart`

**请求 data：** `{ "pile_no": "SZ001-03" }`

**逻辑：**

1. 电桩须为「故障」
2. 改为「闲置」
3. 写 `operation_log`

**响应 data：** `{ "pile_no": "SZ001-03", "status": "闲置" }`

---

### 4.16 `station.admin.list` / `station.create`

**list 响应 items[]：** id, name, address, lat, lng, price, total_piles, idle_piles, online_rate, created_at

**create 请求 data：**

```json
{
  "name": "新充电站",
  "address": "深圳市...",
  "lat": 22.54,
  "lng": 114.05,
  "price": 1.30,
  "fast_count": 4,
  "slow_count": 2
}
```

服务端按数量自动生成电桩编号。

---

### 4.17 `user.admin.list` / `user.freeze`

**list 请求 data：** `{ "phone_keyword": "138" }`

**freeze 请求 data：** `{ "user_id": 1, "freeze": true }`

- `freeze: true` → status 改「冻结」
- `freeze: false` → status 改「正常」

---

### 4.18 `order.admin.settle`

管理员代用户结算待支付订单。逻辑同 `charge.settle`，须写 `operation_log`。

---

## 5. P2 命令（加分）

### 5.1 收藏

- `station.favorite.add` — data: `{ "station_id": 1 }`
- `station.favorite.remove` — data: `{ "station_id": 1 }`
- `station.favorite.list` — 返回收藏站列表（格式同 `station.list` 单项）

### 5.2 电桩 CRUD

- `pile.update` — 修改 type / power_kw / status
- `pile.delete` — 删除空闲电桩（在用/预约中禁止删除）

### 5.3 `forecast.list`

**请求 data：** `{ "horizon": "1h" }`

**响应 items[]：**

```json
{
  "station_id": 1,
  "station_name": "深圳市民中心充电站",
  "forecast_hour": "2026-08-28 15:00",
  "predicted_load": 85.0,
  "predicted_idle_piles": 4
}
```

### 5.4 `event.push`（服务端主动推送）

服务端在充电进度变化、电桩状态变化时推送：

```json
{
  "cmd": "event.push",
  "data": {
    "type": "charge.progress",
    "payload": { "order_no": "CD...", "kwh": 12.5, "amount": 15.0 }
  }
}
```

客户端无需回包。

---

## 6. 订单与电桩状态机

```
电桩:  闲置 ──预约──► 预约 ──开始──► 在用 ──停止──► 闲置
                 故障 ◄──远程重启── 故障

订单:  预约 ──开始──► 充电中 ──停止──► 待支付 ──结算──► 已完成
```

**拦截规则：**

- 用户存在「充电中」或「待支付」订单 → 禁止新预约（`order.check_open` 返回 has_open=true）
- 故障电桩 → 禁止预约（`PILE_FAULT`）
- 非闲置电桩 → 禁止预约（`PILE_BUSY`）

---

## 7. 客户端实现要点

### 7.1 公共类 `ApiClient`（A/B 共用思路）

```cpp
// 伪代码
bool connectToServer(const QString &host, quint16 port);
QJsonObject request(const QString &cmd, const QJsonObject &data);
// 内部: 写 length+JSON → 读 length+JSON → 解析
```

### 7.2 数据库路径

服务端统一使用相对路径：

```
../db/charge.db
```

各端**不直接读写**数据库（大屏 Flask 只读除外）。

---

## 8. 测试用例（验收）

| 步骤 | 命令 | 预期 |
|------|------|------|
| 1 | `ping` | ok=true |
| 2 | `user.login` 13800138001 | 返回 balance=92.5 |
| 3 | `user.login` 13800138006 | USER_FROZEN |
| 4 | `admin.login` admin/123456 | 返回 token |
| 5 | `admin.login` wrong pwd | UNAUTHORIZED |
| 6 | `order.check_open`（8002 登录后） | has_open=true, status=待支付 |
| 7 | `station.list` lat/lng 深圳 | 5 条，按 distance 排序 |
| 8 | `charge.reserve` → `start` → `progress` → `stop` → `settle` | 全流程订单已完成 |
| 9 | `pile.restart` SZ001-03 | 故障→闲置 |

---

## 9. 变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v0.1 | 2026-09-02 | 初稿：P0/P1/P2 命令、状态机、错误码 |
