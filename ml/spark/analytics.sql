-- 第二阶段：Spark SQL 多维分析（≥8 个分析维度，含 ≥2 组交叉对比）
-- 依赖 Hive 表：dws_station_hourly / dws_station_daily / dim_station / dim_pile
-- 平台 KPI / 用户活跃由 dws_station_daily 聚合（与 hive_ddl.sql 一致，不依赖 ads_daily_stats）
--
-- 用法:
--   spark-sql -f ml/spark/analytics.sql
--
-- 输出 Parquet 目录（答辩环境写入 HDFS /charging/ads/analytics/...）:
--   analytics_platform_daily
--   analytics_station_rank
--   analytics_hourly_profile
--   analytics_region_stats
--   analytics_pile_status
--   analytics_station_util
--   analytics_user_activity
--   analytics_fault_rate
--   analytics_weekday_weekend      -- 交叉对比 1：工作日 vs 周末
--   analytics_station_hour_matrix  -- 交叉对比 2：电站 × 小时热力

CREATE DATABASE IF NOT EXISTS charging;
USE charging;

-- 1 平台日 KPI（由 dws_station_daily 聚合）
CREATE OR REPLACE TABLE analytics_platform_daily
USING parquet LOCATION '/charging/ads/analytics/platform_daily' AS
SELECT
    stat_date,
    SUM(revenue) AS total_revenue,
    SUM(kwh) AS total_kwh,
    SUM(orders) AS order_count,
    SUM(active_users) AS active_user_count,
    AVG(utilization) AS utilization,
    MAX(peak_hour) AS peak_hour
FROM (
    SELECT d.*, h.active_users
    FROM dws_station_daily d
    LEFT JOIN (
        SELECT station_id, stat_date, MAX(active_users) AS active_users
        FROM dws_station_hourly
        GROUP BY station_id, stat_date
    ) h ON h.station_id = d.station_id AND h.stat_date = d.stat_date
) t
GROUP BY stat_date
ORDER BY stat_date;

-- 2 电站排行（最近一日）
CREATE OR REPLACE TABLE analytics_station_rank
USING parquet LOCATION '/charging/ads/analytics/station_rank' AS
SELECT s.name, d.orders, d.revenue, d.kwh, d.utilization, d.peak_hour, d.fault_rate
FROM dws_station_daily d
JOIN dim_station s ON s.id = d.station_id
WHERE d.stat_date = (SELECT MAX(stat_date) FROM dws_station_daily)
ORDER BY d.revenue DESC;

-- 3 24 小时负荷曲线（近 30 天汇总）
CREATE OR REPLACE TABLE analytics_hourly_profile
USING parquet LOCATION '/charging/ads/analytics/hourly_profile' AS
SELECT stat_hour, SUM(kwh) AS kwh, SUM(orders) AS orders
FROM dws_station_hourly
WHERE datediff(current_date(), to_date(stat_date)) BETWEEN 0 AND 30
GROUP BY stat_hour
ORDER BY stat_hour;

-- 4 区域分布（从地址提取区名）
CREATE OR REPLACE TABLE analytics_region_stats
USING parquet LOCATION '/charging/ads/analytics/region_stats' AS
SELECT
    regexp_extract(address, '([^区]+区)', 1) AS region,
    SUM(orders) AS orders,
    SUM(revenue) AS revenue,
    SUM(kwh) AS kwh
FROM dws_station_daily d
JOIN dim_station s ON s.id = d.station_id
WHERE datediff(current_date(), to_date(d.stat_date)) BETWEEN 0 AND 30
GROUP BY regexp_extract(address, '([^区]+区)', 1);

-- 5 电桩状态分布
CREATE OR REPLACE TABLE analytics_pile_status
USING parquet LOCATION '/charging/ads/analytics/pile_status' AS
SELECT status, COUNT(*) AS cnt FROM dim_pile GROUP BY status;

-- 6 电站利用率
CREATE OR REPLACE TABLE analytics_station_util
USING parquet LOCATION '/charging/ads/analytics/station_util' AS
SELECT s.name, AVG(d.utilization) AS avg_util, AVG(d.turnover) AS avg_turnover
FROM dws_station_daily d
JOIN dim_station s ON s.id = d.station_id
WHERE datediff(current_date(), to_date(d.stat_date)) BETWEEN 0 AND 30
GROUP BY s.name;

-- 7 用户活跃（按日，由 hourly 表 distinct 近似）
CREATE OR REPLACE TABLE analytics_user_activity
USING parquet LOCATION '/charging/ads/analytics/user_activity' AS
SELECT
    stat_date,
    SUM(active_users) AS active_user_count,
    SUM(orders) AS order_count
FROM dws_station_hourly
GROUP BY stat_date
ORDER BY stat_date;

-- 8 故障率趋势
CREATE OR REPLACE TABLE analytics_fault_rate
USING parquet LOCATION '/charging/ads/analytics/fault_rate' AS
SELECT s.name, AVG(d.fault_rate) AS fault_rate, AVG(d.busy_ratio) AS busy_ratio
FROM dws_station_daily d
JOIN dim_station s ON s.id = d.station_id
WHERE datediff(current_date(), to_date(d.stat_date)) BETWEEN 0 AND 30
GROUP BY s.name;

-- 交叉对比 1：工作日 vs 周末
CREATE OR REPLACE TABLE analytics_weekday_weekend
USING parquet LOCATION '/charging/ads/analytics/weekday_weekend' AS
SELECT
    CASE WHEN dayofweek(to_date(stat_date)) IN (1, 7) THEN 'weekend' ELSE 'weekday' END AS day_type,
    SUM(kwh) AS kwh,
    SUM(orders) AS orders,
    SUM(revenue) AS revenue
FROM dws_station_daily
WHERE datediff(current_date(), to_date(stat_date)) BETWEEN 0 AND 30
GROUP BY CASE WHEN dayofweek(to_date(stat_date)) IN (1, 7) THEN 'weekend' ELSE 'weekday' END;

-- 交叉对比 2：电站 × 小时（热力矩阵）
CREATE OR REPLACE TABLE analytics_station_hour_matrix
USING parquet LOCATION '/charging/ads/analytics/station_hour_matrix' AS
SELECT s.name AS station_name, h.stat_hour, SUM(h.kwh) AS kwh, SUM(h.orders) AS orders
FROM dws_station_hourly h
JOIN dim_station s ON s.id = h.station_id
WHERE datediff(current_date(), to_date(h.stat_date)) BETWEEN 0 AND 30
GROUP BY s.name, h.stat_hour;
