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
WHERE datediff(current_date(), to_date(stat_date)) BETWEEN 0 AND 7
GROUP BY station_id, stat_hour;

-- 桩维度
CREATE OR REPLACE TEMP VIEW dim_pile_agg AS
SELECT
    station_id,
    COUNT(*) AS total_piles,
    AVG(power_kw) AS avg_power_kw
FROM dim_pile
GROUP BY station_id;

-- 高峰小时（优先日表 peak_hour，否则 orders 最大的 hour）
CREATE OR REPLACE TEMP VIEW feat_peak AS
SELECT
    s.station_id,
    COALESCE(
        (
            SELECT d.peak_hour
            FROM dws_station_daily d
            WHERE d.station_id = s.station_id AND d.peak_hour IS NOT NULL
            ORDER BY d.stat_date DESC
            LIMIT 1
        ),
        (
            SELECT h.stat_hour
            FROM dws_station_hourly h
            WHERE h.station_id = s.station_id
            GROUP BY h.stat_hour
            ORDER BY AVG(h.orders) DESC, h.stat_hour ASC
            LIMIT 1
        )
    ) AS predicted_peak_hour
FROM (SELECT DISTINCT station_id FROM dws_station_hourly) s;

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

CREATE OR REPLACE TABLE ads_load_forecast_result
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

CREATE OR REPLACE TABLE ads_time_forecast_result
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
