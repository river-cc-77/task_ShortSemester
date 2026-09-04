#include "aggregator.h"

#include "adsdatabase.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

Aggregator::Aggregator(int intervalSeconds, int windowDays, QObject *parent)
    : QObject(parent)
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

    bool ok = recomputePlatformDaily(from, today);
    ok = recomputeStationDaily(from, today) && ok;
    ok = insertStatusSnapshot() && ok;

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

bool Aggregator::recomputePlatformDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();

    for (QDate day = from; day <= to; day = day.addDays(1)) {
        const QString dayStr = day.toString(Qt::ISODate);  // yyyy-MM-dd

        // 当日已完成订单聚合（口径与 seed 一致）
        double revenue = 0.0;
        double kwh = 0.0;
        qlonglong orderCount = 0;
        qlonglong activeUsers = 0;

        QSqlQuery orderQuery(db);
        orderQuery.prepare(
            "SELECT COALESCE(SUM(amount), 0), COALESCE(SUM(kwh), 0), COUNT(*), "
            "       COUNT(DISTINCT user_id) "
            "FROM charge_order "
            "WHERE status = '已完成' AND substr(created_at, 1, 10) = :d");
        orderQuery.bindValue(":d", dayStr);
        if (!orderQuery.exec() || !orderQuery.next()) {
            qCritical() << "daily order query failed:" << orderQuery.lastError().text();
            return false;
        }
        revenue = orderQuery.value(0).toDouble();
        kwh = orderQuery.value(1).toDouble();
        orderCount = orderQuery.value(2).toLongLong();
        activeUsers = orderQuery.value(3).toLongLong();

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

        QSqlQuery insert(db);
        insert.prepare(
            "INSERT OR REPLACE INTO ads_daily_stats "
            "(stat_date, total_revenue, total_kwh, order_count, active_user_count, "
            " new_user_count, total_users, updated_at) "
            "VALUES (:d, :revenue, :kwh, :orders, :active_users, :new_users, :total_users, "
            "        datetime('now', 'localtime'))");
        insert.bindValue(":d", dayStr);
        insert.bindValue(":revenue", revenue);
        insert.bindValue(":kwh", kwh);
        insert.bindValue(":orders", orderCount);
        insert.bindValue(":active_users", activeUsers);
        insert.bindValue(":new_users", newUsers);
        insert.bindValue(":total_users", totalUsers);
        if (!insert.exec()) {
            qCritical() << "ads_daily_stats insert failed:" << insert.lastError().text();
            return false;
        }
    }
    return true;
}

bool Aggregator::recomputeStationDaily(const QDate &from, const QDate &to)
{
    QSqlDatabase db = AdsDatabase::instance().db();
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    // 先删窗口内旧行，再按业务表重算，保证幂等不叠加
    QSqlQuery remove(db);
    remove.prepare("DELETE FROM ads_station_daily WHERE stat_date BETWEEN :from AND :to");
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_station_daily delete failed:" << remove.lastError().text();
        return false;
    }

    QSqlQuery insert(db);
    insert.prepare(
        "INSERT INTO ads_station_daily "
        "(station_id, stat_date, orders, revenue, kwh, updated_at) "
        "SELECT station_id, substr(created_at, 1, 10) AS d, COUNT(*), "
        "       COALESCE(SUM(amount), 0), COALESCE(SUM(kwh), 0), datetime('now', 'localtime') "
        "FROM charge_order "
        "WHERE status = '已完成' AND substr(created_at, 1, 10) BETWEEN :from AND :to "
        "GROUP BY station_id, d");
    insert.bindValue(":from", fromStr);
    insert.bindValue(":to", toStr);
    if (!insert.exec()) {
        qCritical() << "ads_station_daily insert failed:" << insert.lastError().text();
        return false;
    }
    return true;
}

bool Aggregator::insertStatusSnapshot()
{
    QSqlDatabase db = AdsDatabase::instance().db();

    // 按 站 × 状态 统计当前电桩分布，追加一条周期快照
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO ads_status_snapshot (snap_time, station_id, status, cnt) "
        "SELECT datetime('now', 'localtime'), station_id, status, COUNT(*) "
        "FROM pile "
        "GROUP BY station_id, status");
    if (!query.exec()) {
        qCritical() << "ads_status_snapshot insert failed:" << query.lastError().text();
        return false;
    }
    return true;
}
