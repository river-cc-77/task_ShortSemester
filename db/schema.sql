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
-- 大屏聚合快照（定时任务写入，避免大屏频繁扫订单表）
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_daily_stats (
    stat_date          TEXT PRIMARY KEY,  -- YYYY-MM-DD
    total_revenue      REAL NOT NULL DEFAULT 0,
    total_kwh          REAL NOT NULL DEFAULT 0,
    order_count        INTEGER NOT NULL DEFAULT 0,
    active_user_count  INTEGER NOT NULL DEFAULT 0,
    updated_at         TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);
