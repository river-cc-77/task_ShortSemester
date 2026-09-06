-- 东软电动汽车充电桩应用管理平台
-- SQLite schema (QSQLITE)
-- 执行: sqlite3 charge.db < schema.sql

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- 管理员
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS admin (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    password_hash TEXT    NOT NULL,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- ---------------------------------------------------------------------------
-- 用户（手机号免密登录）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    phone       TEXT    NOT NULL UNIQUE,
    nickname    TEXT    NOT NULL,
    avatar_path TEXT    NOT NULL DEFAULT '',
    balance     REAL    NOT NULL DEFAULT 0 CHECK (balance >= 0),
    status      TEXT    NOT NULL DEFAULT '正常'
                        CHECK (status IN ('正常', '冻结')),
    created_at  TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_user_phone ON user (phone);
CREATE INDEX IF NOT EXISTS idx_user_status ON user (status);

-- ---------------------------------------------------------------------------
-- 充电站
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS station (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT NOT NULL,
    address    TEXT NOT NULL,
    lat        REAL NOT NULL,
    lng        REAL NOT NULL,
    price      REAL NOT NULL CHECK (price > 0),  -- 元/度
    created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_station_geo ON station (lat, lng);

-- ---------------------------------------------------------------------------
-- 充电桩
-- status: 闲置 | 在用 | 故障 | 预约
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS pile (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_no        TEXT    NOT NULL UNIQUE,
    station_id     INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    type           TEXT    NOT NULL CHECK (type IN ('快充', '慢充')),
    power_kw       REAL    NOT NULL CHECK (power_kw > 0),
    status         TEXT    NOT NULL DEFAULT '闲置'
                           CHECK (status IN ('闲置', '在用', '故障', '预约')),
    charge_count   INTEGER NOT NULL DEFAULT 0 CHECK (charge_count >= 0),
    charge_minutes INTEGER NOT NULL DEFAULT 0 CHECK (charge_minutes >= 0),
    updated_at     TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_pile_station ON pile (station_id);
CREATE INDEX IF NOT EXISTS idx_pile_status ON pile (status);

-- ---------------------------------------------------------------------------
-- 充电订单
-- status: 预约 -> 充电中 -> 待支付 -> 已完成
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS charge_order (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no   TEXT    NOT NULL UNIQUE,
    user_id    INTEGER NOT NULL REFERENCES user (id),
    station_id INTEGER NOT NULL REFERENCES station (id),
    pile_id    INTEGER NOT NULL REFERENCES pile (id),
    status     TEXT    NOT NULL
                       CHECK (status IN ('预约', '充电中', '待支付', '已完成')),
    reserve_at TEXT,
    start_at   TEXT,
    end_at     TEXT,
    kwh        REAL    NOT NULL DEFAULT 0 CHECK (kwh >= 0),
    amount     REAL    NOT NULL DEFAULT 0 CHECK (amount >= 0),
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_order_user ON charge_order (user_id);
CREATE INDEX IF NOT EXISTS idx_order_status ON charge_order (status);
CREATE INDEX IF NOT EXISTS idx_order_station ON charge_order (station_id);
CREATE INDEX IF NOT EXISTS idx_order_created ON charge_order (created_at);

-- ---------------------------------------------------------------------------
-- 钱包流水（充值、结算扣款）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS wallet_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES user (id),
    delta      REAL    NOT NULL,  -- 正数充值，负数扣款
    reason     TEXT    NOT NULL,
    order_id   INTEGER REFERENCES charge_order (id),
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_wallet_user ON wallet_log (user_id);

-- ---------------------------------------------------------------------------
-- 收藏充电站
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS favorite_station (
    user_id    INTEGER NOT NULL REFERENCES user (id) ON DELETE CASCADE,
    station_id INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (user_id, station_id)
);

-- ---------------------------------------------------------------------------
-- 管理员操作日志
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS operation_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    admin_id    INTEGER REFERENCES admin (id),
    action      TEXT NOT NULL,
    target_type TEXT,
    target_id   TEXT,
    detail      TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_oplog_created ON operation_log (created_at);

-- ---------------------------------------------------------------------------
-- 运维公告（用户端首页展示）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS announcement (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT    NOT NULL,
    content    TEXT    NOT NULL,
    is_active  INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- ---------------------------------------------------------------------------
-- 负荷预测结果（ML 脚本写入，用户端/管理端/大屏读取）
-- horizon: 1h | 6h | 24h
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS load_forecast (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id           INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    forecast_hour        TEXT    NOT NULL,  -- 预测目标时段，如 2026-08-28 15:00
    predicted_load       REAL    NOT NULL DEFAULT 0,  -- 预测负荷 (kWh)
    predicted_idle_piles INTEGER NOT NULL DEFAULT 0,
    horizon              TEXT    NOT NULL CHECK (horizon IN ('1h', '6h', '24h')),
    created_at           TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_forecast_station ON load_forecast (station_id, horizon);

-- ---------------------------------------------------------------------------
-- 平台级 日KPI 快照（collector/ 定时任务写入，避免大屏频繁扫订单表）
-- stat_date 为业务时间（localtime）的自然日
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_daily_stats (
    stat_date          TEXT PRIMARY KEY,  -- YYYY-MM-DD
    total_revenue      REAL    NOT NULL DEFAULT 0,  -- 当日已完成订单营收（清洗有效单）
    total_kwh          REAL    NOT NULL DEFAULT 0,  -- 当日已完成订单充电量
    order_count        INTEGER NOT NULL DEFAULT 0,  -- 当日已完成订单数
    active_user_count  INTEGER NOT NULL DEFAULT 0,  -- 当日有已完成订单的去重用户数
    new_user_count     INTEGER NOT NULL DEFAULT 0,  -- 当日注册用户数
    total_users        INTEGER NOT NULL DEFAULT 0,  -- 截至当日的累计用户数
    pending_cnt        INTEGER NOT NULL DEFAULT 0,  -- 当日有效"待支付"订单数（终态未结算）
    completion_rate    REAL    NOT NULL DEFAULT 0,  -- 完成率 = order_count/(order_count+pending_cnt)
    active_ratio       REAL    NOT NULL DEFAULT 0,  -- 活跃占比 = active_user_count/total_users
    per_user_orders    REAL    NOT NULL DEFAULT 0,  -- 人均单量 = order_count/active_user_count
    per_user_kwh       REAL    NOT NULL DEFAULT 0,  -- 人均电量 = total_kwh/active_user_count
    avg_session_min    REAL    NOT NULL DEFAULT 0,  -- 平均单次有效充电时长（分，已完成单）
    avg_kwh_order      REAL    NOT NULL DEFAULT 0,  -- 平均单次充电量 = total_kwh/order_count
    occ_min            REAL    NOT NULL DEFAULT 0,  -- 当日有效占用分钟（时间族，含待支付）
    utilization        REAL    NOT NULL DEFAULT 0,  -- 订单占用口径利用率 = occ_min/(桩总数×1440)
    busy_ratio         REAL    NOT NULL DEFAULT 0,  -- 快照口径繁忙率（在用+预约时间加权）
    fault_rate         REAL    NOT NULL DEFAULT 0,  -- 故障率（快照时间加权；依赖快照累积，无快照日为 0）
    peak_hour          INTEGER,                    -- 物化峰值小时 0-23；当日无单为 NULL
    updated_at         TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- ---------------------------------------------------------------------------
-- 电站 × 日 营收/电量聚合（collector/ 定时任务写入）
-- 电站排行、按站营收趋势、占用利用率/繁忙率/故障率/周转等图表数据源
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_station_daily (
    station_id     INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    stat_date      TEXT    NOT NULL,  -- YYYY-MM-DD
    orders         INTEGER NOT NULL DEFAULT 0,  -- 该站当日已完成订单数
    revenue        REAL    NOT NULL DEFAULT 0,  -- 该站当日营收
    kwh            REAL    NOT NULL DEFAULT 0,  -- 该站当日充电量
    pile_cnt       INTEGER NOT NULL DEFAULT 0,  -- 当日该站桩数（利用率/周转分母，demo 静态取现值）
    occ_min        REAL    NOT NULL DEFAULT 0,  -- 当日该站有效占用分钟（时间族）
    utilization    REAL    NOT NULL DEFAULT 0,  -- 占用口径利用率 = occ_min/(pile_cnt×1440)
    busy_ratio     REAL    NOT NULL DEFAULT 0,  -- 快照口径繁忙率
    fault_rate     REAL    NOT NULL DEFAULT 0,  -- 故障率（快照时间加权；无快照日为 0）
    avg_session_min REAL   NOT NULL DEFAULT 0,  -- 平均单次有效充电时长（分）
    turnover       REAL    NOT NULL DEFAULT 0,  -- 桩日周转 = orders/pile_cnt
    peak_hour      INTEGER,                     -- 物化峰值小时；当日无单为 NULL
    updated_at     TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (station_id, stat_date)
);

CREATE INDEX IF NOT EXISTS idx_ads_station_date ON ads_station_daily (stat_date);

-- ---------------------------------------------------------------------------
-- 电桩状态周期快照（collector/ 每采集周期按 站×状态 追加多条）
-- 供大屏展示电桩状态分布、按站可用桩数、日内状态趋势。
-- 时序累积，仅保留回填窗口内的快照（窗口外由聚合器定期清理，不影响已物化日表）；
-- (snap_time, station_id, status) 唯一，同刻同站同态重复写入被 INSERT OR IGNORE 幂等忽略。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_status_snapshot (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    snap_time  TEXT NOT NULL,  -- 采集时间 yyyy-MM-dd HH:mm:ss（localtime）
    station_id INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    status     TEXT NOT NULL,  -- 闲置 | 在用 | 故障 | 预约
    cnt        INTEGER NOT NULL DEFAULT 0,  -- 该站该状态的桩数
    CHECK (status IN ('闲置', '在用', '故障', '预约')),
    UNIQUE (snap_time, station_id, status)
);

CREATE INDEX IF NOT EXISTS idx_ads_snapshot_time ON ads_status_snapshot (snap_time);

-- ---------------------------------------------------------------------------
-- 清洗后订单事实台账（collector/clean 阶段写入，collector 内部中间层，非对外 KPI）
-- 每个归属日在回填窗口内的业务订单一行（含异常单；窗外订单仅参与去重登记不落表），
-- 带 清洗/补全/归属 标记；所有订单族指标统一读它，
-- 保证"去重+补全+过滤"只做一次、口径唯一。绝不回写业务表。
-- 归属时钟：统计日/小时 = start_at（充电真实发生时刻），缺失回退 end_at -> created_at。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_order_fact (
    order_id       INTEGER PRIMARY KEY,  -- 镜像 charge_order.id
    order_no       TEXT    NOT NULL,     -- 订单号（用于去重审计）
    user_id        INTEGER NOT NULL,
    station_id     INTEGER NOT NULL,
    pile_id        INTEGER NOT NULL,
    status         TEXT    NOT NULL,     -- 预约 | 充电中 | 待支付 | 已完成（原样保留）
    stat_date      TEXT    NOT NULL,     -- 统一归属自然日 YYYY-MM-DD
    stat_hour      INTEGER NOT NULL,     -- 统一归属小时 0-23
    start_ts       TEXT,                 -- 原始 start_at（审计）
    end_ts         TEXT,                 -- 原始 end_at（审计）
    duration_min   REAL    NOT NULL DEFAULT 0,  -- 有效充电时长(分)；时间倒挂/缺时间为 0
    kwh_orig       REAL    NOT NULL DEFAULT 0,  -- 业务表原始电量
    amount_orig    REAL    NOT NULL DEFAULT 0,  -- 业务表原始金额
    kwh_eff        REAL    NOT NULL DEFAULT 0,  -- 有效电量（补全/估算后）
    amount_eff     REAL    NOT NULL DEFAULT 0,  -- 有效金额
    est_source     TEXT    NOT NULL DEFAULT '', -- 补全来源：''|'price'|'amount'|'power'(可组合)
    region         TEXT    NOT NULL DEFAULT '', -- 站所属区域快照（如 福田区；提取失败为'未知'）
    excluded       INTEGER NOT NULL DEFAULT 0,  -- 1=剔除出所有对外指标
    exclude_code   TEXT,                       -- 剔除原因：dup/fk_missing/negative/time_invalid/
                                               -- future_ts/zero_unest/stale_reserve/stale_charging
    warn_code      TEXT    NOT NULL DEFAULT '',-- 逗号分隔，仅标记仍入指标：near_dup/est/price_suspect
    ts_flag        INTEGER NOT NULL DEFAULT 0, -- 1=归属时钟用了回退（start 缺失）
    ts_missing     INTEGER NOT NULL DEFAULT 0, -- 1=已完成/待支付缺 start/end，不得进时间族
    raw_created_at TEXT    NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_fact_date_status ON ads_order_fact (stat_date, status);
CREATE INDEX IF NOT EXISTS idx_fact_pile_date  ON ads_order_fact (pile_id, stat_date);
CREATE INDEX IF NOT EXISTS idx_fact_station    ON ads_order_fact (station_id, stat_date);
CREATE INDEX IF NOT EXISTS idx_fact_region     ON ads_order_fact (region, stat_date);

-- ---------------------------------------------------------------------------
-- 清洗问题台账（collector/clean 阶段写入，内部审计，不参与指标计算）
-- 每单每个问题一行，供核对去重/补全/剔除等预处理效果。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_order_issue (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no    TEXT NOT NULL,
    issue_code  TEXT NOT NULL,      -- 与 fact 的 exclude_code / warn_code 一致
    issue_level TEXT NOT NULL,      -- exclude | warn
    detail      TEXT NOT NULL DEFAULT '',
    detected_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_issue_level ON ads_order_issue (issue_level);

-- ---------------------------------------------------------------------------
-- 桩 × 日：单桩服务单量与占用利用率（collector 定时写入）
-- utilization = duration_min/1440（钳位 ≤1）；行按 桩 × 窗口日 全量铺开（静默日填 0）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_pile_daily (
    pile_id      INTEGER NOT NULL REFERENCES pile (id) ON DELETE CASCADE,
    stat_date    TEXT    NOT NULL,  -- YYYY-MM-DD
    orders       INTEGER NOT NULL DEFAULT 0,  -- 当日服务单数（已完成+待支付且有效，时间族）
    kwh          REAL    NOT NULL DEFAULT 0,  -- 当日该桩充电量（时间族有效电量）
    duration_min REAL    NOT NULL DEFAULT 0,  -- 当日占用分钟
    utilization  REAL    NOT NULL DEFAULT 0,  -- 占用利用率（0~1）
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (pile_id, stat_date)
);

CREATE INDEX IF NOT EXISTS idx_ads_pile_date ON ads_pile_daily (stat_date);

-- ---------------------------------------------------------------------------
-- 平台 × 小时：订单量/电量/时长小时分布（高峰定位、时段分析）
-- 近似口径：整单 kwh/时长归属 start 小时，跨多小时的会话只计首小时（demo 单次 <1h 影响可忽略）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_hourly_stats (
    stat_date    TEXT    NOT NULL,  -- YYYY-MM-DD
    stat_hour    INTEGER NOT NULL,  -- 0-23
    orders       INTEGER NOT NULL DEFAULT 0,  -- 已完成有效单数
    revenue      REAL    NOT NULL DEFAULT 0,
    kwh          REAL    NOT NULL DEFAULT 0,
    duration_min REAL    NOT NULL DEFAULT 0,  -- 已完成有效占用分钟
    active_users INTEGER NOT NULL DEFAULT 0,
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (stat_date, stat_hour)
);

-- ---------------------------------------------------------------------------
-- 站 × 小时：每站高峰/站内时段（同上近似口径）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_station_hourly (
    station_id   INTEGER NOT NULL REFERENCES station (id) ON DELETE CASCADE,
    stat_date    TEXT    NOT NULL,
    stat_hour    INTEGER NOT NULL,
    orders       INTEGER NOT NULL DEFAULT 0,
    revenue      REAL    NOT NULL DEFAULT 0,
    kwh          REAL    NOT NULL DEFAULT 0,
    duration_min REAL    NOT NULL DEFAULT 0,
    active_users INTEGER NOT NULL DEFAULT 0,
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (station_id, stat_date, stat_hour)
);

-- ---------------------------------------------------------------------------
-- 区域 × 日：由 station.address 文本提取"XX区"分组（福田区/南山区/宝安区…）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_region_daily (
    region       TEXT    NOT NULL,  -- 区域名；未知区归 '未知'
    stat_date    TEXT    NOT NULL,  -- YYYY-MM-DD
    orders       INTEGER NOT NULL DEFAULT 0,
    revenue      REAL    NOT NULL DEFAULT 0,
    kwh          REAL    NOT NULL DEFAULT 0,
    active_users INTEGER NOT NULL DEFAULT 0,
    station_cnt  INTEGER NOT NULL DEFAULT 0,  -- 当日有单的区域电站数
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
    PRIMARY KEY (region, stat_date)
);

CREATE INDEX IF NOT EXISTS idx_ads_region_date ON ads_region_daily (stat_date);
