# ads-collector（大屏数据采集）

大屏可视化的**第一阶段：数据采集与存储**。独立运行的 Qt6 / C++ 控制台进程，定时从业务库采集「订单 / 电桩状态 / 用户 / 营收」并聚合成**分析表**，避免大屏直接高频扫业务大表压库。

- 启动立即执行一次全量回填（近 30 天），之后默认每 60 秒重算一次（幂等）。
- 只**读**业务表，只**写** `ads_*` 分析表，不改写业务数据。

## 数据来源与分析表

| 分析表 | 内容 | 供大屏图表 |
|--------|------|-----------|
| `ads_daily_stats` | 平台级日 KPI（营收/电量/订单数/活跃用户/新增与累计用户） | KPI 卡、营收趋势折线、用户增长 |
| `ads_station_daily` | 电站 × 日 营收/电量/订单数 | 电站排行、按站营收趋势 |
| `ads_status_snapshot` | 电桩状态周期快照（每周期按 站 × 状态 追加一条） | 电桩状态分布、按站可用桩数 |

**口径说明**：营收/电量/订单数/活跃用户均统计 `charge_order.status = '已完成'` 的订单，按 `substr(created_at, 1, 10)` 的自然日（`localtime`）聚合，与 `db/seed.sql` 演示口径一致。

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev libqt6sql6-sqlite

cd collector
qmake6 collector.pro
make -j4
```

## 运行前

需先初始化业务库（仅首次；`charge.db` 不提交 Git，各人生成）：

```bash
cd db
sqlite3 charge.db < schema.sql
sqlite3 charge.db < seed.sql
```

## 运行

```bash
cd collector
./ads-collector            # 默认：每 60s 聚合一次，启动回填近 30 天
./ads-collector 10         # 测试用：每 10s 聚合一次
./ads-collector 60 7       # 回填窗口改为近 7 天
```

可通过环境变量 `ADS_DB=/绝对路径/charge.db` 显式指定数据库位置（默认自动探测项目根 `db/charge.db`）。

## 验证（对账）

采集一轮后，分析表数值应等于业务表手工聚合结果：

```bash
# 平台日 KPI 抽查
sqlite3 db/charge.db \
  "SELECT stat_date,total_revenue,order_count,active_user_count,new_user_count,total_users
     FROM ads_daily_stats ORDER BY stat_date;"

# 对照：业务表手工聚合（应与上表相应日期一致）
sqlite3 db/charge.db \
  "SELECT substr(created_at,1,10) d, ROUND(SUM(amount),2), COUNT(*)
     FROM charge_order WHERE status='已完成' GROUP BY d ORDER BY d;"

# 电站 × 日
sqlite3 db/charge.db "SELECT * FROM ads_station_daily ORDER BY stat_date, station_id;"

# 最新电桩状态快照
sqlite3 db/charge.db \
  "SELECT station_id,status,cnt FROM ads_status_snapshot
     WHERE snap_time=(SELECT MAX(snap_time) FROM ads_status_snapshot) ORDER BY station_id,status;"
```

## 与业务 server 并发

采集器与 `charge-server` 会同时连接同一 `db/charge.db`。采集器已开启 `PRAGMA busy_timeout = 5000` 并把库切到 **WAL** 日志模式，降低读写互斥；极端高并发写时可能出现短暂等待（`database is locked` 会自动重试等待），属正常。

## 协作规则

- **不提交** `db/charge.db`（本地生成）
- 涉及 `ads_*` 表结构变更须同步改 `db/schema.sql`，并通知全组重建本地库
- 采集器只写分析表，不得改动业务表（`charge_order` / `user` / `pile` / `station`）
