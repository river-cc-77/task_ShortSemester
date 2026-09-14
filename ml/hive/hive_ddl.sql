-- Hive 外部表 DDL（指向 HDFS 或本地 ml/data/hdfs 镜像）
-- 用法（Ubuntu 验收环境）:
--   hdfs dfs -put ml/data/hdfs/charging/* /charging/
--   hive -f ml/hive/hive_ddl.sql

CREATE DATABASE IF NOT EXISTS charging;
USE charging;

DROP TABLE IF EXISTS dws_station_hourly;
CREATE EXTERNAL TABLE dws_station_hourly (
    station_id   INT,
    stat_date    STRING,
    stat_hour    INT,
    orders       INT,
    revenue      DOUBLE,
    kwh          DOUBLE,
    duration_min DOUBLE,
    active_users INT,
    updated_at   STRING
)
ROW FORMAT SERDE 'org.apache.hadoop.hive.serde2.OpenCSVSerde'
WITH SERDEPROPERTIES ('separatorChar' = ',', 'quoteChar' = '"')
STORED AS TEXTFILE
LOCATION '/charging/dws/station_hourly'
TBLPROPERTIES ('skip.header.line.count' = '1');

DROP TABLE IF EXISTS dws_station_daily;
CREATE EXTERNAL TABLE dws_station_daily (
    station_id      INT,
    stat_date       STRING,
    orders          INT,
    revenue         DOUBLE,
    kwh             DOUBLE,
    pile_cnt        INT,
    occ_min         DOUBLE,
    utilization     DOUBLE,
    busy_ratio      DOUBLE,
    fault_rate      DOUBLE,
    avg_session_min DOUBLE,
    turnover        DOUBLE,
    peak_hour       INT,
    updated_at      STRING
)
ROW FORMAT SERDE 'org.apache.hadoop.hive.serde2.OpenCSVSerde'
WITH SERDEPROPERTIES ('separatorChar' = ',', 'quoteChar' = '"')
STORED AS TEXTFILE
LOCATION '/charging/dws/station_daily'
TBLPROPERTIES ('skip.header.line.count' = '1');

DROP TABLE IF EXISTS dim_pile;
CREATE EXTERNAL TABLE dim_pile (
    id         INT,
    pile_no    STRING,
    station_id INT,
    type       STRING,
    power_kw   DOUBLE,
    status     STRING
)
ROW FORMAT SERDE 'org.apache.hadoop.hive.serde2.OpenCSVSerde'
WITH SERDEPROPERTIES ('separatorChar' = ',', 'quoteChar' = '"')
STORED AS TEXTFILE
LOCATION '/charging/dim/pile'
TBLPROPERTIES ('skip.header.line.count' = '1');

DROP TABLE IF EXISTS dim_station;
CREATE EXTERNAL TABLE dim_station (
    id      INT,
    name    STRING,
    address STRING,
    lat     DOUBLE,
    lng     DOUBLE,
    price   DOUBLE
)
ROW FORMAT SERDE 'org.apache.hadoop.hive.serde2.OpenCSVSerde'
WITH SERDEPROPERTIES ('separatorChar' = ',', 'quoteChar' = '"')
STORED AS TEXTFILE
LOCATION '/charging/dim/station'
TBLPROPERTIES ('skip.header.line.count' = '1');
