-- Spark SQL 负荷预测 + 充电时间预测（仅 Spark SQL，不用 MLlib）
-- 算法: 加权移动平均 WMA（与 ml/predict_local.py 一致）
--   权重 w = 1 / (days_ago + 1)，越近的历史权重越大
--
-- 用法:
--   spark-sql -f ml/spark/forecast.sql \
--     --hiveconf history_days=7 \
--     --hiveconf run_ts="2026-09-14 09:00:00"
--
-- 输出表（Parquet，供 sync_to_sqlite 导出 CSV）:
--   charging.ads_load_forecast_result
--   charging.ads_time_forecast_result

CREATE DATABASE IF NOT EXISTS charging;
USE charging;

SET spark.sql.sources.partitionOverwriteMode=dynamic;

-- 近 N 天同 hour 加权移动平均
-- 窗口为 [今天-history_days, 今天)，**不含今天**：当天可能只过了一半，甚至只有当天
-- 那个全 0 占位行，而它的权重最大（days_ago=0 -> w=1.0），会把预测系统性拉低。
-- 口径与 ml/predict_local.py 的 wma_same_hour 保持一致。
CREATE OR REPLACE TEMP VIEW feat_wma AS
SELECT
    station_id,
    stat_hour,
    SUM(kwh * (1.0 / (datediff(current_date(), to_date(stat_date)) + 1)))
        / SUM(1.0 / (datediff(current_date(), to_date(stat_date)) + 1)) AS wma_kwh,
    SUM(
        CASE WHEN duration_min > 0
             THEN duration_min * (1.0 / (datediff(current_date(), to_date(stat_date)) + 1))
        END
    ) / NULLIF(
        SUM(
            CASE WHEN duration_min > 0
                 THEN 1.0 / (datediff(current_date(), to_date(stat_date)) + 1)
            END
        ),
        0
    ) AS wma_duration_min
FROM dws_station_hourly
WHERE datediff(current_date(), to_date(stat_date)) BETWEEN 1 AND 7
GROUP BY station_id, stat_hour;

-- 桩维度
CREATE OR REPLACE TEMP VIEW dim_pile_agg AS
SELECT
    station_id,
    COUNT(*) AS total_piles,
    AVG(power_kw) AS avg_power_kw
FROM dim_pile
GROUP BY station_id;

-- 高峰小时（Spark 3.4 不支持关联子查询，改用窗口函数，口径同 predict_local.py）
CREATE OR REPLACE TEMP VIEW feat_peak_daily AS
SELECT station_id, peak_hour
FROM (
    SELECT station_id, peak_hour,
           ROW_NUMBER() OVER (PARTITION BY station_id ORDER BY stat_date DESC) AS rn
    FROM dws_station_daily
    WHERE peak_hour IS NOT NULL
) t
WHERE rn = 1;

CREATE OR REPLACE TEMP VIEW hourly_peak_agg AS
SELECT station_id, stat_hour, AVG(orders) AS avg_orders
FROM dws_station_hourly
GROUP BY station_id, stat_hour
HAVING SUM(orders) > 0;

CREATE OR REPLACE TEMP VIEW feat_peak_hourly AS
SELECT station_id, stat_hour AS predicted_peak_hour
FROM (
    SELECT station_id, stat_hour,
           ROW_NUMBER() OVER (PARTITION BY station_id ORDER BY avg_orders DESC, stat_hour ASC) AS rn
    FROM hourly_peak_agg
) t
WHERE rn = 1;

CREATE OR REPLACE TEMP VIEW feat_peak AS
SELECT s.station_id, COALESCE(d.peak_hour, h.predicted_peak_hour) AS predicted_peak_hour
FROM (SELECT DISTINCT station_id FROM dws_station_hourly) s
LEFT JOIN feat_peak_daily d ON d.station_id = s.station_id
LEFT JOIN feat_peak_hourly h ON h.station_id = s.station_id;

-- 三个 horizon 的目标时刻（以 run_ts 为基准）
CREATE OR REPLACE TEMP VIEW horizon_targets AS
SELECT '1h' AS horizon, date_format(from_unixtime(unix_timestamp('${run_ts}') + 3600), 'yyyy-MM-dd HH:00') AS forecast_hour,
       hour(from_unixtime(unix_timestamp('${run_ts}') + 3600)) AS target_hour
UNION ALL
SELECT '6h', date_format(from_unixtime(unix_timestamp('${run_ts}') + 6 * 3600), 'yyyy-MM-dd HH:00'),
       hour(from_unixtime(unix_timestamp('${run_ts}') + 6 * 3600))
UNION ALL
SELECT '24h', date_format(from_unixtime(unix_timestamp('${run_ts}') + 24 * 3600), 'yyyy-MM-dd HH:00'),
       hour(from_unixtime(unix_timestamp('${run_ts}') + 24 * 3600));

DROP TABLE IF EXISTS ads_load_forecast_result;
CREATE TABLE ads_load_forecast_result
USING parquet
LOCATION '/charging/ads/load_forecast_result'
AS
SELECT
    s.id AS station_id,
    t.forecast_hour,
    ROUND(COALESCE(w.wma_kwh, 0), 2) AS predicted_load,
    GREATEST(
        0,
        CAST(p.total_piles AS INT) - CAST(CEIL(COALESCE(w.wma_kwh, 0) / NULLIF(p.avg_power_kw, 0)) AS INT)
    ) AS predicted_idle_piles,
    t.horizon,
    date_format(current_timestamp(), 'yyyy-MM-dd HH:mm:ss') AS created_at
FROM dim_station s
CROSS JOIN horizon_targets t
LEFT JOIN feat_wma w ON w.station_id = s.id AND w.stat_hour = t.target_hour
LEFT JOIN dim_pile_agg p ON p.station_id = s.id;

DROP TABLE IF EXISTS ads_time_forecast_result;
CREATE TABLE ads_time_forecast_result
USING parquet
LOCATION '/charging/ads/time_forecast_result'
AS
SELECT
    s.id AS station_id,
    t.forecast_hour,
    ROUND(COALESCE(w.wma_duration_min, 0), 2) AS predicted_avg_duration_min,
    pk.predicted_peak_hour,
    t.horizon,
    date_format(current_timestamp(), 'yyyy-MM-dd HH:mm:ss') AS created_at
FROM dim_station s
CROSS JOIN horizon_targets t
LEFT JOIN feat_wma w ON w.station_id = s.id AND w.stat_hour = t.target_hour
LEFT JOIN feat_peak pk ON pk.station_id = s.id;
