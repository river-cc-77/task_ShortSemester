# ads-collector（大屏数据采集 + 预处理 + 指标聚合）

大屏可视化的数据管道：**数据采集与存储 + 数据预处理（去重/补全/异常过滤） + 多级指标聚合**。独立运行的 Qt6 / C++ 控制台进程，定时从业务库采集「订单 / 电桩状态 / 用户 / 营收」，先**清洗**再聚合成**分析表**，避免大屏直接高频扫业务大表压库。

- 启动立即执行一次全量回填（近 30 天），之后默认每 60 秒重算一次（窗口内汇总幂等、单事务）。
- 只**读**业务表（`charge_order` / `user` / `pile` / `station`），只**写** `ads_*` 分析表，不改写业务数据。

## 预处理与口径

清洗阶段先把每个订单写入 **`ads_order_fact`**（每单一行，带剔除/补全/归属标记），所有订单族指标统一读它，保证「去重+补全+过滤」只做一次、口径唯一；异常明细同步写入 **`ads_order_issue`** 供审计。规则摘要：

- **归属时钟**：统计日/小时取 `start_at`（充电真实发生时刻），缺失回退 `end_at -> created_at`。
- **去重**：同 `order_no` 保留更早单（剔除）；同 `user+桩+start 到分钟` 标 `near_dup`（仅告警）。
- **补全**：已完成/待支付单缺金额/电量时按 `station.price`、`pile.power_kw×时长` 估算，来源记入 `est_source`。
- **过滤（剔除 excluded=1，不进任何对外指标）**：引用缺失、时间倒挂、时间在未来、负值、已完成且不可估算、预约/充电中滞留超 12h。
- **价差可疑**（金额与 电量×电价 偏差>5%）仅告警，仍入指标。

**指标口径（分表注意）**：
- **订单族列** `orders / revenue / kwh` = 已完成 且 `excluded=0`（平台/站/小时/区域一致）。
- **占用族列** `occ_min / duration_min / utilization` = 已完成+待支付 且时间完整（`ts_missing=0`）、时长>0。
- 两处例外：`ads_pile_daily` 的 `orders/kwh/duration` 采用桩级「服务占用」口径（含待支付的有效占用，语义为"桩正在为订单服务"），故**桩日 orders 与平台/站 orders（仅已完成）不同源**；小时表的 `duration_min` 仅已完成有效占用，而日级 `occ_min` 含待支付——跨表比较前先按上两行归类。
- 利用率双口径：`utilization`（占用口径 = 占用分钟/(桩数×1440)），`busy_ratio`（快照口径 = 在用+预约 时间加权）。
- 故障率 `fault_rate` = 快照「故障」时间加权。口径：运行日/有快照日按真实快照逐点加权；**无快照的历史日**（collector 首轮回填期）以当前桩态稳态**近似**回填，使"回填近 30 天"的派生指标逐日有值；该日一旦积累真实快照即由时间加权接管（近似不写入快照表）。
- 高峰时段：小时级表物化出每平台/每站每日 `peak_hour`（无单日保持 NULL）。

## 数据来源与分析表

「对外 7 张」供大屏直读；`ads_order_fact / ads_order_issue` 是 collector 内部清洗/审计台账（指标取数源与预处理核对，**非对外 KPI**）。各表列语义见上文"指标口径"。

| 分析表 | 内容 | 供大屏图表 |
|--------|------|-----------|
| `ads_daily_stats` | 平台级日 KPI（营收/电量/单量/活跃/新增累计用户 + 完成率/人均/平均时长 + 双利用率/故障率/高峰小时） | KPI 卡、营收趋势、用户增长、完成率 |
| `ads_station_daily` | 电站 × 日（营收族 + 占用利用率/繁忙/故障/周转/高峰小时；站×日全量铺开，静默日填 0） | 电站排行、按站趋势、按站利用率 |
| `ads_status_snapshot` | 电桩状态周期快照（每周期按 站×状态 追加一条） | 电桩状态分布、状态趋势 |
| `ads_pile_daily` | 桩 × 日（服务单量、电量、占用分钟、占用利用率；桩×日全量铺开） | 单桩钻取、桩利用率 |
| `ads_hourly_stats` | 平台 × 小时（订单/营收/电量/占用分钟/活跃用户；24h 铺开） | 高峰曲线、全天忙闲 |
| `ads_station_hourly` | 站 × 小时（同上） | 每站高峰定位 |
| `ads_region_daily` | 区域 × 日（由电站地址提取“XX区”分组；区域×日全量铺开） | 区域分布 |
| `ads_order_fact` | 清洗后订单事实台账（每单一行，内部中间层，非对外 KPI） | 指标取数/审计底稿 |
| `ads_order_issue` | 清洗问题台账（剔除/告警明细，内部审计，不参与指标计算） | 预处理效果核对 |

**口径说明**：平台/站/小时/区域的营收/电量/单量/活跃均统计 `excluded=0` 的已完成订单，按 `stat_date`（默认 = `start_at` 所在日，`localtime`）聚合；桩×日为服务占用口径（含待支付有效占用），见上"指标口径"。因演示数据下单时刻与充电时刻同日，与 `db/seed.sql` 业务口径一致。

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev libqt6sql6-sqlite

cd collector
qmake6 collector.pro
make -j4
```

## 运行前

需先初始化业务库；**本版扩展了 ads_* 表结构，旧 `charge.db` 必须重建**（`charge.db` 不提交 Git，各人生成）：

```bash
cd db
rm -f charge.db
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

预处理与新增指标抽查（一轮后）：

```bash
# 事实台账：无脏数据时应 行数=窗口内订单数 且全部 excluded=0
sqlite3 db/charge.db \
  "SELECT excluded, COUNT(*) FROM ads_order_fact GROUP BY excluded;
   SELECT COUNT(*) FROM ads_order_fact WHERE stat_date BETWEEN date('now','-29 day') AND date('now');"

# 平台日新增列（完成率/人均/利用率/故障率/高峰小时）
sqlite3 db/charge.db \
  "SELECT stat_date,total_revenue,completion_rate,utilization,busy_ratio,fault_rate,peak_hour
     FROM ads_daily_stats ORDER BY stat_date;"

# 桩 × 日 / 区域 × 日 / 小时 抽查
sqlite3 db/charge.db \
  "SELECT * FROM ads_pile_daily WHERE orders>0 ORDER BY stat_date,pile_id;
   SELECT * FROM ads_region_daily WHERE orders>0 ORDER BY stat_date;
   SELECT * FROM ads_hourly_stats WHERE orders>0 ORDER BY stat_date,stat_hour;"
```

清洗规则验证（构造脏单后重跑，对照 `ads_order_issue`）：

```sql
-- 时间倒挂单 -> exclude_code=time_invalid
INSERT INTO charge_order (order_no,user_id,station_id,pile_id,status,start_at,end_at,kwh,amount,created_at)
VALUES ('CDDIRTY0001',1,1,(SELECT id FROM pile WHERE pile_no='SZ001-01'),'已完成',
        '2026-08-29 10:00:00','2026-08-29 09:00:00',30,36,'2026-08-29 09:00:00');
-- 已完成且电量/金额皆 0 但缺时间依据 -> 若只有时长也可按功率估(est)；无 start/end 且 0/0 会被 zero_unest 剔除
INSERT INTO charge_order (order_no,user_id,station_id,pile_id,status,kwh,amount,created_at)
VALUES ('CDDIRTY0002',1,1,(SELECT id FROM pile WHERE pile_no='SZ001-01'),'已完成',0,0,'2026-08-29 11:00:00');
```
> 说明：`order_no UNIQUE` 与 `kwh/amount >= 0` 等约束由 schema 保证，异常用例多在合法范围内构造；如需验证 `dup`/`negative` 请另建临时库去除约束。

## 与业务 server 并发

采集器与 `charge-server` 会同时连接同一 `db/charge.db`。采集器已开启 `PRAGMA busy_timeout = 5000` 并把库切到 **WAL** 日志模式，降低读写互斥；极端高并发写时可能出现短暂等待（`database is locked` 会自动重试等待），属正常。

## 协作规则

- **不提交** `db/charge.db`（本地生成）
- 涉及 `ads_*` 表结构变更须同步改 `db/schema.sql`，并通知全组重建本地库
- 采集器只写分析表，不得改动业务表（`charge_order` / `user` / `pile` / `station`）
