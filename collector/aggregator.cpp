#include "aggregator.h"

#include "adsdatabase.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTime>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <algorithm>

Aggregator::Aggregator(int intervalSeconds, int windowDays, QObject *parent)
    : QObject(parent)
    , m_cleaner(AdsDatabase::instance().db())
    , m_intervalSeconds(intervalSeconds > 0 ? intervalSeconds : 60)
    , m_windowDays(windowDays > 0 ? windowDays : 30)
{
}

void Aggregator::start()
{
    aggregate();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Aggregator::onTick);
    m_timer->start(m_intervalSeconds * 1000);
    qInfo() << "Collector timer started, interval" << m_intervalSeconds << "s,"
            << "window" << m_windowDays << "days";
}

void Aggregator::onTick()
{
    aggregate();
}

bool Aggregator::aggregate()
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QDate today = QDate::currentDate();
    const QDate from = today.addDays(-(m_windowDays - 1));

    if (!db.transaction()) {
        qCritical() << "Begin transaction failed:" << db.lastError().text();
        return false;
    }

    // 先清洗出事实台账与问题台账，再让各指标统一读事实表（口径唯一）
    bool ok = m_cleaner.rebuildFactLedger(from, today);
    ok = m_cleaner.rebuildIssueLedger() && ok;
    ok = recomputePlatformDaily(from, today) && ok;
    ok = recomputeStationDaily(from, today) && ok;
    ok = recomputePileDaily(from, today) && ok;
    ok = recomputeHourly(from, today) && ok;
    ok = recomputeRegionDaily(from, today) && ok;
    ok = insertStatusSnapshot(from) && ok;
    ok = recomputeSnapshotDerived(from, today) && ok;

    if (!ok) {
        db.rollback();
        qCritical() << "Aggregation failed, rolled back";
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Commit failed:" << db.lastError().text();
        db.rollback();
        return false;
    }

    qInfo() << "Aggregated window" << from.toString(Qt::ISODate) << "~"
            << today.toString(Qt::ISODate) << "at"
            << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    return true;
}

// ---------------------------------------------------------------------------
// 平台级 日 KPI：营收族 + 用户增长 + 完成率/人均/时长/双利用率（占用口径）
// ---------------------------------------------------------------------------
bool Aggregator::recomputePlatformDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();

    // 桩总数（利用率分母；demo 桩静态，取现值即可）
    qlonglong pileTotal = 0;
    {
        QSqlQuery pileCount(db);
        if (pileCount.exec(QStringLiteral("SELECT COUNT(*) FROM pile")) && pileCount.next()) {
            pileTotal = pileCount.value(0).toLongLong();
        }
    }

    for (QDate day = from; day <= to; day = day.addDays(1)) {
        const QString dayStr = day.toString(Qt::ISODate);  // yyyy-MM-dd

        // 当日有效已完成单（营收族 R，读清洗事实表）
        double revenue = 0.0;
        double kwh = 0.0;
        qlonglong orderCount = 0;
        qlonglong activeUsers = 0;
        QSqlQuery orderQuery(db);
        orderQuery.prepare(
            "SELECT COALESCE(SUM(amount_eff), 0), COALESCE(SUM(kwh_eff), 0), COUNT(*), "
            "       COUNT(DISTINCT user_id) "
            "FROM ads_order_fact "
            "WHERE excluded = 0 AND status = '已完成' AND stat_date = :d");
        orderQuery.bindValue(":d", dayStr);
        if (!orderQuery.exec() || !orderQuery.next()) {
            qCritical() << "daily order query failed:" << orderQuery.lastError().text();
            return false;
        }
        revenue = orderQuery.value(0).toDouble();
        kwh = orderQuery.value(1).toDouble();
        orderCount = orderQuery.value(2).toLongLong();
        activeUsers = orderQuery.value(3).toLongLong();

        // 当日有效"待支付"（终态未结算）
        qlonglong pending = 0;
        QSqlQuery pendingQuery(db);
        pendingQuery.prepare(
            "SELECT COUNT(*) FROM ads_order_fact "
            "WHERE excluded = 0 AND status = '待支付' AND stat_date = :d");
        pendingQuery.bindValue(":d", dayStr);
        if (!pendingQuery.exec() || !pendingQuery.next()) {
            qCritical() << "pending query failed:" << pendingQuery.lastError().text();
            return false;
        }
        pending = pendingQuery.value(0).toLongLong();

        // 时间族：占用分钟 / 已完成有效时长与次数
        double occMin = 0.0;
        double durDone = 0.0;
        qlonglong sessDone = 0;
        QSqlQuery timeQuery(db);
        timeQuery.prepare(
            "SELECT COALESCE(SUM(CASE WHEN status IN ('已完成','待支付') AND ts_missing = 0 "
            "                       THEN duration_min END), 0), "
            "       COALESCE(SUM(CASE WHEN status = '已完成' AND ts_missing = 0 "
            "                       THEN duration_min END), 0), "
            "       COALESCE(COUNT(CASE WHEN status = '已完成' AND ts_missing = 0 THEN 1 END), 0) "
            "FROM ads_order_fact WHERE excluded = 0 AND stat_date = :d");
        timeQuery.bindValue(":d", dayStr);
        if (!timeQuery.exec() || !timeQuery.next()) {
            qCritical() << "time-family query failed:" << timeQuery.lastError().text();
            return false;
        }
        occMin = timeQuery.value(0).toDouble();
        durDone = timeQuery.value(1).toDouble();
        sessDone = timeQuery.value(2).toLongLong();

        // 当日新增注册用户
        qlonglong newUsers = 0;
        QSqlQuery newUserQuery(db);
        newUserQuery.prepare("SELECT COUNT(*) FROM user WHERE substr(created_at, 1, 10) = :d");
        newUserQuery.bindValue(":d", dayStr);
        if (!newUserQuery.exec() || !newUserQuery.next()) {
            qCritical() << "new-user query failed:" << newUserQuery.lastError().text();
            return false;
        }
        newUsers = newUserQuery.value(0).toLongLong();

        // 截至当日累计用户（created_at 为 yyyy-MM-dd HH:mm:ss，按日字符串比较即可）
        qlonglong totalUsers = 0;
        QSqlQuery totalUserQuery(db);
        totalUserQuery.prepare("SELECT COUNT(*) FROM user WHERE substr(created_at, 1, 10) <= :d");
        totalUserQuery.bindValue(":d", dayStr);
        if (!totalUserQuery.exec() || !totalUserQuery.next()) {
            qCritical() << "total-user query failed:" << totalUserQuery.lastError().text();
            return false;
        }
        totalUsers = totalUserQuery.value(0).toLongLong();

        const double completionRate = (orderCount + pending) > 0
                                          ? static_cast<double>(orderCount) / (orderCount + pending)
                                          : 0.0;
        const double activeRatio = totalUsers > 0 ? static_cast<double>(activeUsers) / totalUsers : 0.0;
        const double perUserOrders = activeUsers > 0 ? static_cast<double>(orderCount) / activeUsers : 0.0;
        const double perUserKwh = activeUsers > 0 ? kwh / activeUsers : 0.0;
        const double avgSessionMin = sessDone > 0 ? durDone / sessDone : 0.0;
        const double avgKwhOrder = orderCount > 0 ? kwh / orderCount : 0.0;
        const double utilization = pileTotal > 0 ? qMin(occMin / (pileTotal * 1440.0), 1.0) : 0.0;

        QSqlQuery insert(db);
        insert.prepare(
            "INSERT OR REPLACE INTO ads_daily_stats "
            "(stat_date, total_revenue, total_kwh, order_count, active_user_count, "
            " new_user_count, total_users, pending_cnt, completion_rate, active_ratio, "
            " per_user_orders, per_user_kwh, avg_session_min, avg_kwh_order, "
            " occ_min, utilization, busy_ratio, fault_rate, peak_hour, updated_at) "
            "VALUES (:d, :revenue, :kwh, :orders, :active_users, "
            " :new_users, :total_users, :pending, :completion, :active_ratio, "
            " :per_user_orders, :per_user_kwh, :avg_session, :avg_kwh, "
            " :occ_min, :utilization, 0, 0, NULL, datetime('now', 'localtime'))");
        insert.bindValue(":d", dayStr);
        insert.bindValue(":revenue", revenue);
        insert.bindValue(":kwh", kwh);
        insert.bindValue(":orders", orderCount);
        insert.bindValue(":active_users", activeUsers);
        insert.bindValue(":new_users", newUsers);
        insert.bindValue(":total_users", totalUsers);
        insert.bindValue(":pending", pending);
        insert.bindValue(":completion", completionRate);
        insert.bindValue(":active_ratio", activeRatio);
        insert.bindValue(":per_user_orders", perUserOrders);
        insert.bindValue(":per_user_kwh", perUserKwh);
        insert.bindValue(":avg_session", avgSessionMin);
        insert.bindValue(":avg_kwh", avgKwhOrder);
        insert.bindValue(":occ_min", occMin);
        insert.bindValue(":utilization", utilization);
        if (!insert.exec()) {
            qCritical() << "ads_daily_stats insert failed:" << insert.lastError().text();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 电站 × 日：营收族 + 占用利用率/繁忙/故障/周转（每 站×窗口日 全量铺开，静默日填 0）
// ---------------------------------------------------------------------------
bool Aggregator::recomputeStationDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    QSqlQuery remove(db);
    remove.prepare("DELETE FROM ads_station_daily WHERE stat_date BETWEEN :from AND :to");
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_station_daily delete failed:" << remove.lastError().text();
        return false;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "WITH RECURSIVE dates(d) AS ("
        "  SELECT :from UNION ALL SELECT date(d, '+1 day') FROM dates WHERE d < :to), "
        "agg AS ("
        "  SELECT station_id, stat_date AS d, "
        "    COUNT(CASE WHEN status = '已完成' THEN 1 END) AS orders, "
        "    COALESCE(SUM(CASE WHEN status = '已完成' THEN amount_eff END), 0) AS revenue, "
        "    COALESCE(SUM(CASE WHEN status = '已完成' THEN kwh_eff END), 0) AS kwh, "
        "    COALESCE(SUM(CASE WHEN status IN ('已完成','待支付') AND ts_missing = 0 "
        "                     THEN duration_min END), 0) AS occ_min, "
        "    COALESCE(SUM(CASE WHEN status = '已完成' AND ts_missing = 0 "
        "                     THEN duration_min END), 0) AS dur_done, "
        "    COALESCE(COUNT(CASE WHEN status = '已完成' AND ts_missing = 0 THEN 1 END), 0) AS sess_done "
        "  FROM ads_order_fact "
        "  WHERE excluded = 0 AND stat_date BETWEEN :from AND :to "
        "  GROUP BY station_id, stat_date), "
        "pc AS (SELECT station_id, COUNT(*) AS c FROM pile GROUP BY station_id) "
        "INSERT INTO ads_station_daily "
        "(station_id, stat_date, orders, revenue, kwh, pile_cnt, occ_min, utilization, "
        " busy_ratio, fault_rate, avg_session_min, turnover, updated_at) "
        "SELECT s.id, dates.d, "
        "  COALESCE(a.orders, 0), ROUND(COALESCE(a.revenue, 0), 2), ROUND(COALESCE(a.kwh, 0), 2), "
        "  COALESCE(pc.c, 0), ROUND(COALESCE(a.occ_min, 0), 2), "
        "  CASE WHEN COALESCE(pc.c, 0) > 0 "
        "       THEN ROUND(MIN(COALESCE(a.occ_min, 0) / (pc.c * 1440.0), 1.0), 4) ELSE 0 END, "
        "  0, 0, "
        "  CASE WHEN COALESCE(a.sess_done, 0) > 0 "
        "       THEN ROUND(a.dur_done / a.sess_done, 2) ELSE 0 END, "
        "  CASE WHEN COALESCE(pc.c, 0) > 0 "
        "       THEN ROUND(COALESCE(a.orders, 0) * 1.0 / pc.c, 4) ELSE 0 END, "
        "  datetime('now', 'localtime') "
        "FROM dates CROSS JOIN station s "
        "LEFT JOIN agg a ON a.d = dates.d AND a.station_id = s.id "
        "LEFT JOIN pc ON pc.station_id = s.id"));
    insert.bindValue(":from", fromStr);
    insert.bindValue(":to", toStr);
    if (!insert.exec()) {
        qCritical() << "ads_station_daily insert failed:" << insert.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 桩 × 日：单桩服务单量与占用利用率（时间族；每 桩×窗口日 全量铺开）
// ---------------------------------------------------------------------------
bool Aggregator::recomputePileDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    QSqlQuery remove(db);
    remove.prepare("DELETE FROM ads_pile_daily WHERE stat_date BETWEEN :from AND :to");
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_pile_daily delete failed:" << remove.lastError().text();
        return false;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "WITH RECURSIVE dates(d) AS ("
        "  SELECT :from UNION ALL SELECT date(d, '+1 day') FROM dates WHERE d < :to), "
        "agg AS ("
        "  SELECT pile_id, stat_date AS d, "
        "    COUNT(*) AS orders, COALESCE(SUM(kwh_eff), 0) AS kwh, "
        "    COALESCE(SUM(duration_min), 0) AS duration_min "
        "  FROM ads_order_fact "
        "  WHERE excluded = 0 AND status IN ('已完成','待支付') AND ts_missing = 0 "
        "    AND duration_min > 0 AND stat_date BETWEEN :from AND :to "
        "  GROUP BY pile_id, stat_date) "
        "INSERT INTO ads_pile_daily "
        "(pile_id, stat_date, orders, kwh, duration_min, utilization, updated_at) "
        "SELECT p.id, dates.d, "
        "  COALESCE(a.orders, 0), ROUND(COALESCE(a.kwh, 0), 2), "
        "  ROUND(COALESCE(a.duration_min, 0), 2), "
        "  CASE WHEN COALESCE(a.duration_min, 0) > 0 "
        "       THEN ROUND(MIN(a.duration_min / 1440.0, 1.0), 4) ELSE 0 END, "
        "  datetime('now', 'localtime') "
        "FROM dates CROSS JOIN pile p "
        "LEFT JOIN agg a ON a.d = dates.d AND a.pile_id = p.id"));
    insert.bindValue(":from", fromStr);
    insert.bindValue(":to", toStr);
    if (!insert.exec()) {
        qCritical() << "ads_pile_daily insert failed:" << insert.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 小时级分布：平台 × 小时 + 站 × 小时（24h 全量铺开；整单归属 start 小时的近似口径）
// ---------------------------------------------------------------------------
bool Aggregator::recomputeHourly(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    QSqlQuery remove(db);
    remove.prepare("DELETE FROM ads_hourly_stats WHERE stat_date BETWEEN :from AND :to");
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_hourly_stats delete failed:" << remove.lastError().text();
        return false;
    }
    QSqlQuery removeStation(db);
    removeStation.prepare("DELETE FROM ads_station_hourly WHERE stat_date BETWEEN :from AND :to");
    removeStation.bindValue(":from", fromStr);
    removeStation.bindValue(":to", toStr);
    if (!removeStation.exec()) {
        qCritical() << "ads_station_hourly delete failed:" << removeStation.lastError().text();
        return false;
    }

    // 平台小时
    QSqlQuery insertPlatform(db);
    insertPlatform.prepare(QStringLiteral(
        "WITH RECURSIVE dates(d) AS ("
        "  SELECT :from UNION ALL SELECT date(d, '+1 day') FROM dates WHERE d < :to), "
        "hours(h) AS (SELECT 0 UNION ALL SELECT h + 1 FROM hours WHERE h < 23), "
        "agg AS ("
        "  SELECT stat_date AS d, stat_hour AS h, COUNT(*) AS orders, "
        "    COALESCE(SUM(amount_eff), 0) AS revenue, COALESCE(SUM(kwh_eff), 0) AS kwh, "
        "    COALESCE(SUM(duration_min), 0) AS duration_min, "
        "    COUNT(DISTINCT user_id) AS active "
        "  FROM ads_order_fact "
        "  WHERE excluded = 0 AND status = '已完成' AND stat_date BETWEEN :from AND :to "
        "  GROUP BY stat_date, stat_hour) "
        "INSERT INTO ads_hourly_stats "
        "(stat_date, stat_hour, orders, revenue, kwh, duration_min, active_users, updated_at) "
        "SELECT dates.d, hours.h, "
        "  COALESCE(a.orders, 0), ROUND(COALESCE(a.revenue, 0), 2), ROUND(COALESCE(a.kwh, 0), 2), "
        "  ROUND(COALESCE(a.duration_min, 0), 2), COALESCE(a.active, 0), "
        "  datetime('now', 'localtime') "
        "FROM dates CROSS JOIN hours "
        "LEFT JOIN agg a ON a.d = dates.d AND a.h = hours.h"));
    insertPlatform.bindValue(":from", fromStr);
    insertPlatform.bindValue(":to", toStr);
    if (!insertPlatform.exec()) {
        qCritical() << "ads_hourly_stats insert failed:" << insertPlatform.lastError().text();
        return false;
    }

    // 站小时
    QSqlQuery insertStation(db);
    insertStation.prepare(QStringLiteral(
        "WITH RECURSIVE dates(d) AS ("
        "  SELECT :from UNION ALL SELECT date(d, '+1 day') FROM dates WHERE d < :to), "
        "hours(h) AS (SELECT 0 UNION ALL SELECT h + 1 FROM hours WHERE h < 23), "
        "agg AS ("
        "  SELECT station_id, stat_date AS d, stat_hour AS h, COUNT(*) AS orders, "
        "    COALESCE(SUM(amount_eff), 0) AS revenue, COALESCE(SUM(kwh_eff), 0) AS kwh, "
        "    COALESCE(SUM(duration_min), 0) AS duration_min, "
        "    COUNT(DISTINCT user_id) AS active "
        "  FROM ads_order_fact "
        "  WHERE excluded = 0 AND status = '已完成' AND stat_date BETWEEN :from AND :to "
        "  GROUP BY station_id, stat_date, stat_hour) "
        "INSERT INTO ads_station_hourly "
        "(station_id, stat_date, stat_hour, orders, revenue, kwh, duration_min, active_users, updated_at) "
        "SELECT s.id, dates.d, hours.h, "
        "  COALESCE(a.orders, 0), ROUND(COALESCE(a.revenue, 0), 2), ROUND(COALESCE(a.kwh, 0), 2), "
        "  ROUND(COALESCE(a.duration_min, 0), 2), COALESCE(a.active, 0), "
        "  datetime('now', 'localtime') "
        "FROM dates CROSS JOIN hours CROSS JOIN station s "
        "LEFT JOIN agg a ON a.d = dates.d AND a.h = hours.h AND a.station_id = s.id"));
    insertStation.bindValue(":from", fromStr);
    insertStation.bindValue(":to", toStr);
    if (!insertStation.exec()) {
        qCritical() << "ads_station_hourly insert failed:" << insertStation.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 区域 × 日：station.address 提取"XX区"分组（每 区域×窗口日 全量铺开）
// ---------------------------------------------------------------------------
bool Aggregator::recomputeRegionDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    QSqlQuery remove(db);
    remove.prepare("DELETE FROM ads_region_daily WHERE stat_date BETWEEN :from AND :to");
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_region_daily delete failed:" << remove.lastError().text();
        return false;
    }

    // 区域表达式与 clean.cpp 的 regionFromAddress 语义一致（'市'后第一个'区'含）
    const QString regionExpr =
        QStringLiteral(
            "CASE WHEN instr(s.address,'区') > 0 AND instr(s.address,'市') > 0 "
            "          AND instr(s.address,'区') > instr(s.address,'市') "
            "     THEN substr(s.address, instr(s.address,'市') + 1, "
            "                 instr(s.address,'区') - instr(s.address,'市')) "
            "     WHEN instr(s.address,'区') > 0 THEN substr(s.address, 1, instr(s.address,'区')) "
            "     ELSE '未知' END");

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "WITH RECURSIVE dates(d) AS ("
        "  SELECT :from UNION ALL SELECT date(d, '+1 day') FROM dates WHERE d < :to), "
        "regs AS (SELECT DISTINCT (%1) AS region FROM station s), "
        "agg AS ("
        "  SELECT region, stat_date AS d, COUNT(*) AS orders, "
        "    COALESCE(SUM(amount_eff), 0) AS revenue, COALESCE(SUM(kwh_eff), 0) AS kwh, "
        "    COUNT(DISTINCT user_id) AS active, COUNT(DISTINCT station_id) AS scnt "
        "  FROM ads_order_fact "
        "  WHERE excluded = 0 AND status = '已完成' AND stat_date BETWEEN :from AND :to "
        "  GROUP BY region, stat_date) "
        "INSERT INTO ads_region_daily "
        "(region, stat_date, orders, revenue, kwh, active_users, station_cnt, updated_at) "
        "SELECT r.region, dates.d, "
        "  COALESCE(a.orders, 0), ROUND(COALESCE(a.revenue, 0), 2), ROUND(COALESCE(a.kwh, 0), 2), "
        "  COALESCE(a.active, 0), COALESCE(a.scnt, 0), datetime('now', 'localtime') "
        "FROM dates CROSS JOIN regs r "
        "LEFT JOIN agg a ON a.d = dates.d AND a.region = r.region").arg(regionExpr));
    insert.bindValue(":from", fromStr);
    insert.bindValue(":to", toStr);
    if (!insert.exec()) {
        qCritical() << "ads_region_daily insert failed:" << insert.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 电桩状态周期快照（每采集周期追加一行，供状态分布与趋势）
// ---------------------------------------------------------------------------
bool Aggregator::insertStatusSnapshot(const QDate &from)
{
    QSqlDatabase db = AdsDatabase::instance().db();

    // 只保留回填窗口内的快照（窗口外历史日派生值已物化在日表，删快照不影响），
    // 防止表随运行时长无限膨胀。
    QSqlQuery prune(db);
    prune.prepare("DELETE FROM ads_status_snapshot WHERE date(snap_time) < :from");
    prune.bindValue(":from", from.toString(Qt::ISODate));
    if (!prune.exec()) {
        qCritical() << "ads_status_snapshot prune failed:" << prune.lastError().text();
        return false;
    }

    // 采集当前桩态分布（站×状态计数）；(snap_time,station_id,status) 唯一，同刻重复被忽略
    QSqlQuery query(db);
    query.prepare(
        "INSERT OR IGNORE INTO ads_status_snapshot (snap_time, station_id, status, cnt) "
        "SELECT datetime('now', 'localtime'), station_id, status, COUNT(*) "
        "FROM pile "
        "GROUP BY station_id, status");
    if (!query.exec()) {
        qCritical() << "ads_status_snapshot insert failed:" << query.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 派生指标回填：繁忙率/故障率（快照时间加权）+ 峰值小时（物化自小时表）
// 需在 insertStatusSnapshot() 之后调用。
// 口径：运行日/有快照日按真实快照时间加权；无快照的历史日（collector 首轮回填期）
// 以当前桩态稳态近似回填，使"首次回填近 30 天"的派生指标逐日有值。
// ---------------------------------------------------------------------------
bool Aggregator::recomputeSnapshotDerived(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    // 峰值小时（只认有订单的小时，空窗日保持 NULL）
    QSqlQuery peakPlatform(db);
    peakPlatform.prepare(
        "UPDATE ads_daily_stats SET peak_hour = ("
        "  SELECT h.stat_hour FROM ads_hourly_stats h "
        "  WHERE h.stat_date = ads_daily_stats.stat_date AND h.orders > 0 "
        "  ORDER BY h.orders DESC, h.kwh DESC, h.stat_hour ASC LIMIT 1) "
        "WHERE stat_date BETWEEN :from AND :to");
    peakPlatform.bindValue(":from", fromStr);
    peakPlatform.bindValue(":to", toStr);
    if (!peakPlatform.exec()) {
        qCritical() << "peak_hour (platform) update failed:" << peakPlatform.lastError().text();
        return false;
    }

    QSqlQuery peakStation(db);
    peakStation.prepare(
        "UPDATE ads_station_daily SET peak_hour = ("
        "  SELECT h.stat_hour FROM ads_station_hourly h "
        "  WHERE h.stat_date = ads_station_daily.stat_date "
        "    AND h.station_id = ads_station_daily.station_id AND h.orders > 0 "
        "  ORDER BY h.orders DESC, h.kwh DESC, h.stat_hour ASC LIMIT 1) "
        "WHERE stat_date BETWEEN :from AND :to");
    peakStation.bindValue(":from", fromStr);
    peakStation.bindValue(":to", toStr);
    if (!peakStation.exec()) {
        qCritical() << "peak_hour (station) update failed:" << peakStation.lastError().text();
        return false;
    }

    // 繁忙率/故障率：平台级 + 各站级
    QSqlQuery setFault(db);
    setFault.prepare("UPDATE ads_daily_stats SET busy_ratio = :busy, fault_rate = :fault "
                     "WHERE stat_date = :d");
    QSqlQuery setStationFault(db);
    setStationFault.prepare(
        "UPDATE ads_station_daily SET busy_ratio = :busy, fault_rate = :fault "
        "WHERE station_id = :s AND stat_date = :d");

    // 站 id 列表
    QList<qlonglong> stationIds;
    {
        QSqlQuery list(db);
        if (list.exec(QStringLiteral("SELECT id FROM station ORDER BY id"))) {
            while (list.next()) {
                stationIds.append(list.value(0).toLongLong());
            }
        }
    }

    // 现态稳态（用于近似回填"无快照的历史日"）：collector 首次运行前，历史日没有桩态时序。
    // 需求要求"首次回填近 30 天"且"按日聚合故障率/繁忙率"，故对这类历史日以当前 pile.status
    // 分布作为代表稳态，让回填窗口内派生指标逐日有值（近似口径，勿与真实快照混淆）；
    // 该日一旦积累真实快照，上面的 timeWeightedRatio 时间加权分支即接管。
    struct Steady { double busy = 0.0; double fault = 0.0; };
    bool steadyLoaded = false;
    double steadyPlatBusy = 0.0, steadyPlatFault = 0.0;
    QHash<int, Steady> steadySt;
    auto ensureSteady = [&]() -> bool {
        if (steadyLoaded) {
            return true;
        }
        steadyLoaded = true;
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral(
                "SELECT station_id, status, COUNT(*) FROM pile GROUP BY station_id, status"))) {
            qCritical() << "steady-state query failed:" << q.lastError().text();
            return false;
        }
        QHash<int, double> total;
        while (q.next()) {
            const int sid = q.value(0).toInt();
            const QString status = q.value(1).toString();
            const double cnt = q.value(2).toDouble();
            total[sid] += cnt;
            if (status == QStringLiteral("在用") || status == QStringLiteral("预约")) {
                steadySt[sid].busy += cnt;
                steadyPlatBusy += cnt;
            } else if (status == QStringLiteral("故障")) {
                steadySt[sid].fault += cnt;
                steadyPlatFault += cnt;
            }
        }
        double platTotal = 0.0;
        for (auto it = total.constBegin(); it != total.constEnd(); ++it) {
            platTotal += it.value();
            auto st = steadySt.find(it.key());
            if (st != steadySt.end() && it.value() > 0.0) {
                st->busy = qMin(st->busy / it.value(), 1.0);
                st->fault = qMin(st->fault / it.value(), 1.0);
            }
        }
        if (platTotal > 0.0) {
            steadyPlatBusy = qMin(steadyPlatBusy / platTotal, 1.0);
            steadyPlatFault = qMin(steadyPlatFault / platTotal, 1.0);
        }
        return true;
    };

    for (QDate day = from; day <= to; day = day.addDays(1)) {
        const QString dayStr = day.toString(Qt::ISODate);

        // 平台（stationId=0）；无快照的历史日（day<运行日）以现态稳态近似
        const auto platform = timeWeightedRatio(db, day, 0);
        if (platform) {
            setFault.bindValue(":busy", platform->first);
            setFault.bindValue(":fault", platform->second);
            setFault.bindValue(":d", dayStr);
            if (!setFault.exec()) {
                qCritical() << "busy/fault (platform) update failed:" << setFault.lastError().text();
                return false;
            }
        } else if (day < to) {
            if (!ensureSteady()) {
                return false;
            }
            setFault.bindValue(":busy", steadyPlatBusy);
            setFault.bindValue(":fault", steadyPlatFault);
            setFault.bindValue(":d", dayStr);
            if (!setFault.exec()) {
                qCritical() << "busy/fault (platform steady approx) update failed:"
                            << setFault.lastError().text();
                return false;
            }
        }

        // 各站
        for (qlonglong sid : stationIds) {
            const auto ratio = timeWeightedRatio(db, day, static_cast<int>(sid));
            double busy, fault;
            if (ratio) {
                busy = ratio->first;
                fault = ratio->second;
            } else if (day < to) {
                if (!ensureSteady()) {
                    return false;
                }
                busy = steadySt.value(sid).busy;   // 无快照历史日：现态稳态近似
                fault = steadySt.value(sid).fault;
            } else {
                continue;
            }
            setStationFault.bindValue(":busy", busy);
            setStationFault.bindValue(":fault", fault);
            setStationFault.bindValue(":s", sid);
            setStationFault.bindValue(":d", dayStr);
            if (!setStationFault.exec()) {
                qCritical() << "busy/fault (station) update failed:" << setStationFault.lastError().text();
                return false;
            }
        }
    }
    return true;
}

// 快照口径 时间加权 [繁忙率, 故障率]。
// 每个快照点代表持续到下一快照的状态，末条按当日 23:59:59 封顶。
// 某站某日无快照返回空（调用方保持 0）。
std::optional<QPair<double, double>> Aggregator::timeWeightedRatio(QSqlDatabase &db,
                                                                  const QDate &day,
                                                                  int stationId)
{
    QSqlQuery query(db);
    QString sql = QStringLiteral(
        "SELECT snap_time, status, cnt FROM ads_status_snapshot "
        "WHERE date(snap_time) = :d");
    if (stationId > 0) {
        sql += QStringLiteral(" AND station_id = :s");
    }
    sql += QStringLiteral(" ORDER BY snap_time");
    query.prepare(sql);
    query.bindValue(":d", day.toString(Qt::ISODate));
    if (stationId > 0) {
        query.bindValue(":s", stationId);
    }
    if (!query.exec()) {
        qCritical() << "snapshot query failed:" << query.lastError().text();
        return std::nullopt;
    }

    // 逐快照时刻累计 在用+预约 / 故障 / 总数（SQL 已按时间排序）
    struct Acc { double busy = 0.0; double fault = 0.0; double total = 0.0; };
    QVector<Acc> points;
    QStringList stamps;
    QHash<QString, int> index;
    while (query.next()) {
        const QString ts = query.value(0).toString();
        const QString status = query.value(1).toString();
        const double cnt = query.value(2).toDouble();
        if (!index.contains(ts)) {
            index.insert(ts, points.size());
            stamps.append(ts);
            points.append(Acc());
        }
        Acc &acc = points[index.value(ts)];
        acc.total += cnt;
        if (status == QStringLiteral("在用") || status == QStringLiteral("预约")) {
            acc.busy += cnt;
        } else if (status == QStringLiteral("故障")) {
            acc.fault += cnt;
        }
    }
    if (points.isEmpty()) {
        return std::nullopt;
    }

    const QDateTime dayStart(QDate(day), QTime(0, 0, 0));
    const QDateTime dayEnd = dayStart.addDays(1);

    double numBusy = 0.0;
    double numFault = 0.0;
    double numTotal = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QDateTime cur = QDateTime::fromString(stamps.at(i),
                                                    QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        QDateTime next = dayEnd;
        if (i + 1 < points.size()) {
            next = QDateTime::fromString(stamps.at(i + 1),
                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        if (!cur.isValid() || !next.isValid()) {
            continue;
        }
        const double weight = std::max(0.0, cur.msecsTo(next) / 1000.0);
        numBusy += weight * points.at(i).busy;
        numFault += weight * points.at(i).fault;
        numTotal += weight * points.at(i).total;
    }
    if (numTotal <= 0.0) {
        return std::nullopt;
    }
    return qMakePair(qMin(numBusy / numTotal, 1.0), qMin(numFault / numTotal, 1.0));
}
