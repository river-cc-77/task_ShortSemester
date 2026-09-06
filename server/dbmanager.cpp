#include "dbmanager.h"

#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtMath>

DbManager &DbManager::instance()
{
    static DbManager manager;
    return manager;
}

QString DbManager::resolveDatabasePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(QDir::currentPath()).filePath("db/charge.db"),
        QDir(QDir::currentPath()).filePath("../db/charge.db"),
        QDir(appDir).filePath("../db/charge.db"),
        QDir(appDir).filePath("../../db/charge.db"),
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return QDir(path).canonicalPath();
        }
    }

    return QDir(QDir::currentPath()).filePath("db/charge.db");
}

bool DbManager::open()
{
    if (m_db.isOpen()) {
        return true;
    }

    m_dbPath = resolveDatabasePath();
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qCritical() << "Failed to open database:" << m_dbPath << m_db.lastError().text();
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA foreign_keys = ON");
    return true;
}

bool DbManager::isOpen() const
{
    return m_db.isOpen();
}

QString DbManager::databasePath() const
{
    return m_dbPath;
}

bool DbManager::runInTransaction(const std::function<bool()> &fn)
{
    if (!m_db.transaction()) {
        qWarning() << "Begin transaction failed:" << m_db.lastError().text();
        return false;
    }

    if (fn()) {
        if (m_db.commit()) {
            return true;
        }
        qWarning() << "Commit failed:" << m_db.lastError().text();
    } else {
        if (!m_db.rollback()) {
            qWarning() << "Rollback failed:" << m_db.lastError().text();
        }
    }
    return false;
}

// ============================================================
// 用户
// ============================================================

std::optional<QJsonObject> DbManager::findUserByPhone(const QString &phone)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, phone, nickname, avatar_path, balance, status, created_at "
        "FROM user WHERE phone = :phone");
    query.bindValue(":phone", phone);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject user;
    user["user_id"] = query.value("id").toInt();
    user["phone"] = query.value("phone").toString();
    user["nickname"] = query.value("nickname").toString();
    user["avatar_path"] = query.value("avatar_path").toString();
    user["balance"] = query.value("balance").toDouble();
    user["status"] = query.value("status").toString();
    user["created_at"] = query.value("created_at").toString();
    return user;
}

std::optional<QJsonObject> DbManager::findUserById(int userId)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, phone, nickname, avatar_path, balance, status, created_at "
        "FROM user WHERE id = :id");
    query.bindValue(":id", userId);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject user;
    user["user_id"] = query.value("id").toInt();
    user["phone"] = query.value("phone").toString();
    user["nickname"] = query.value("nickname").toString();
    user["avatar_path"] = query.value("avatar_path").toString();
    user["balance"] = query.value("balance").toDouble();
    user["status"] = query.value("status").toString();
    user["created_at"] = query.value("created_at").toString();
    return user;
}

QJsonObject DbManager::createUser(const QString &phone)
{
    const QString nickname = QStringLiteral("用户") + phone.right(4);

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO user (phone, nickname, avatar_path, balance, status) "
        "VALUES (:phone, :nickname, '', 0, '正常')");
    query.bindValue(":phone", phone);
    query.bindValue(":nickname", nickname);

    if (!query.exec()) {
        qWarning() << "Create user failed:" << query.lastError().text();
        return {};
    }

    return findUserByPhone(phone).value_or(QJsonObject{});
}

bool DbManager::updateUserProfile(int userId, const QString &nickname, const QString &avatarPath)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE user SET nickname = :nickname, avatar_path = :avatar WHERE id = :id");
    query.bindValue(":nickname", nickname);
    query.bindValue(":avatar", avatarPath);
    query.bindValue(":id", userId);
    return query.exec();
}

std::optional<double> DbManager::rechargeUser(int userId, double amount)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE user SET balance = balance + :amount WHERE id = :id");
    query.bindValue(":amount", amount);
    query.bindValue(":id", userId);

    if (!query.exec()) {
        qWarning() << "rechargeUser failed:" << query.lastError().text();
        return std::nullopt;
    }

    writeWalletLog(userId, amount, QStringLiteral("充值"));

    auto userOpt = findUserById(userId);
    if (userOpt.has_value()) {
        return userOpt.value().value("balance").toDouble();
    }
    return std::nullopt;
}

QJsonArray DbManager::fetchAdminUsers(const QString &phoneKeyword)
{
    QSqlQuery query(m_db);
    QString sql = "SELECT id, phone, nickname, avatar_path, balance, status, created_at FROM user";
    if (!phoneKeyword.isEmpty()) {
        sql += " WHERE phone LIKE :like";
    }
    sql += " ORDER BY id";
    query.prepare(sql);
    if (!phoneKeyword.isEmpty()) {
        query.bindValue(":like", "%" + phoneKeyword + "%");
    }

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchAdminUsers failed:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        QJsonObject row;
        row["user_id"] = query.value("id").toInt();
        row["phone"] = query.value("phone").toString();
        row["nickname"] = query.value("nickname").toString();
        row["avatar_path"] = query.value("avatar_path").toString();
        row["balance"] = query.value("balance").toDouble();
        row["status"] = query.value("status").toString();
        row["created_at"] = query.value("created_at").toString();
        items.append(row);
    }
    return items;
}

bool DbManager::freezeUser(int userId, bool freeze)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE user SET status = :status WHERE id = :id");
    query.bindValue(":status", freeze ? QStringLiteral("冻结") : QStringLiteral("正常"));
    query.bindValue(":id", userId);
    return query.exec();
}

// ============================================================
// 管理员
// ============================================================

std::optional<QJsonObject> DbManager::findAdminByUsername(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, username, password_hash FROM \"admin\" WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "findAdminByUsername SQL error:" << query.lastError().text();
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    QJsonObject admin;
    admin["admin_id"] = query.value("id").toInt();
    admin["username"] = query.value("username").toString();
    admin["password_hash"] = query.value("password_hash").toString();
    return admin;
}

// ============================================================
// 充电站
// ============================================================

QJsonArray DbManager::fetchStations(const QString &keyword)
{
    QSqlQuery query(m_db);
    QString sql =
        "SELECT s.id, s.name, s.address, s.lat, s.lng, s.price, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id) AS total_piles, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id AND p.status = '闲置') AS idle_piles "
        "FROM station s ";

    if (!keyword.isEmpty()) {
        sql += "WHERE s.name LIKE :like OR s.address LIKE :like ";
    }
    sql += "ORDER BY s.id";

    query.prepare(sql);
    if (!keyword.isEmpty()) {
        query.bindValue(":like", "%" + keyword + "%");
    }

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchStations failed:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        QJsonObject row;
        row["id"] = query.value("id").toInt();
        row["name"] = query.value("name").toString();
        row["address"] = query.value("address").toString();
        row["lat"] = query.value("lat").toDouble();
        row["lng"] = query.value("lng").toDouble();
        row["price"] = query.value("price").toDouble();
        row["total_piles"] = query.value("total_piles").toInt();
        row["idle_piles"] = query.value("idle_piles").toInt();
        items.append(row);
    }
    return items;
}

std::optional<QJsonObject> DbManager::fetchStationDetail(int stationId)
{
    QSqlQuery stationQuery(m_db);
    stationQuery.prepare(
        "SELECT id, name, address, lat, lng, price FROM station WHERE id = :id");
    stationQuery.bindValue(":id", stationId);

    if (!stationQuery.exec() || !stationQuery.next()) {
        return std::nullopt;
    }

    QSqlQuery pileQuery(m_db);
    pileQuery.prepare(
        "SELECT id, pile_no, type, power_kw, status FROM pile "
        "WHERE station_id = :station_id ORDER BY pile_no");
    pileQuery.bindValue(":station_id", stationId);

    int totalPiles = 0;
    int idlePiles = 0;
    QJsonArray piles;

    if (pileQuery.exec()) {
        while (pileQuery.next()) {
            ++totalPiles;
            const QString status = pileQuery.value("status").toString();
            if (status == QStringLiteral("闲置")) {
                ++idlePiles;
            }

            QJsonObject pile;
            pile["id"] = pileQuery.value("id").toInt();
            pile["pile_no"] = pileQuery.value("pile_no").toString();
            pile["type"] = pileQuery.value("type").toString();
            pile["power_kw"] = pileQuery.value("power_kw").toDouble();
            pile["status"] = status;
            pile["can_reserve"] = (status == QStringLiteral("闲置"));
            piles.append(pile);
        }
    }

    QJsonObject station;
    station["id"] = stationQuery.value("id").toInt();
    station["name"] = stationQuery.value("name").toString();
    station["address"] = stationQuery.value("address").toString();
    station["lat"] = stationQuery.value("lat").toDouble();
    station["lng"] = stationQuery.value("lng").toDouble();
    station["price"] = stationQuery.value("price").toDouble();
    station["online_rate"] = totalPiles > 0
                                 ? qRound(idlePiles * 1000.0 / totalPiles) / 1000.0
                                 : 0.0;

    QJsonObject detail;
    detail["station"] = station;
    detail["piles"] = piles;
    return detail;
}

QJsonArray DbManager::fetchAdminStations()
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT s.id, s.name, s.address, s.lat, s.lng, s.price, s.created_at, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id) AS total_piles, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id AND p.status != '故障') AS online_piles "
        "FROM station s ORDER BY s.id");

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchAdminStations failed:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        QJsonObject row;
        row["id"] = query.value("id").toInt();
        row["name"] = query.value("name").toString();
        row["address"] = query.value("address").toString();
        row["lat"] = query.value("lat").toDouble();
        row["lng"] = query.value("lng").toDouble();
        row["price"] = query.value("price").toDouble();
        row["total_piles"] = query.value("total_piles").toInt();
        const int total = query.value("total_piles").toInt();
        const int online = query.value("online_piles").toInt();
        row["online_rate"] = total > 0 ? qRound(online * 1000.0 / total) / 1000.0 : 0.0;
        row["created_at"] = query.value("created_at").toString();
        items.append(row);
    }
    return items;
}

bool DbManager::stationNameExists(const QString &name)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM station WHERE name = :name");
    query.bindValue(":name", name);
    if (!query.exec() || !query.next()) {
        return false;
    }
    return query.value(0).toInt() > 0;
}

int DbManager::createStation(const QString &name, const QString &address,
                              double lat, double lng, double price,
                              int fastCount, int slowCount)
{
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO station (name, address, lat, lng, price) "
        "VALUES (:name, :address, :lat, :lng, :price)");
    query.bindValue(":name", name);
    query.bindValue(":address", address);
    query.bindValue(":lat", lat);
    query.bindValue(":lng", lng);
    query.bindValue(":price", price);

    if (!query.exec()) {
        qWarning() << "createStation failed:" << query.lastError().text();
        return -1;
    }

    const int stationId = query.lastInsertId().toInt();

    // 自动生成电桩编号
    const QString prefix = QString("ST%1").arg(stationId, 3, 10, QChar('0'));
    int pileIndex = 1;

    auto createPiles = [&](int count, const QString &type, double power) {
        for (int i = 0; i < count; ++i) {
            const QString pileNo = QString("%1-%2").arg(prefix).arg(pileIndex++, 2, 10, QChar('0'));
            QSqlQuery pq(m_db);
            pq.prepare("INSERT INTO pile (pile_no, station_id, type, power_kw, status) "
                       "VALUES (:no, :sid, :type, :power, '闲置')");
            pq.bindValue(":no", pileNo);
            pq.bindValue(":sid", stationId);
            pq.bindValue(":type", type);
            pq.bindValue(":power", power);
            pq.exec();
        }
    };

    createPiles(fastCount, QStringLiteral("快充"), 120.0);
    createPiles(slowCount, QStringLiteral("慢充"), 7.0);

    return stationId;
}

// ============================================================
// 电桩
// ============================================================

std::optional<QJsonObject> DbManager::findPileByNo(const QString &pileNo)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT p.id, p.pile_no, p.station_id, p.type, p.power_kw, p.status, "
        "p.charge_count, p.charge_minutes, s.name AS station_name, s.price "
        "FROM pile p JOIN station s ON p.station_id = s.id WHERE p.pile_no = :no");
    query.bindValue(":no", pileNo);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject pile;
    pile["id"] = query.value("id").toInt();
    pile["pile_no"] = query.value("pile_no").toString();
    pile["station_id"] = query.value("station_id").toInt();
    pile["station_name"] = query.value("station_name").toString();
    pile["type"] = query.value("type").toString();
    pile["power_kw"] = query.value("power_kw").toDouble();
    pile["status"] = query.value("status").toString();
    pile["charge_count"] = query.value("charge_count").toInt();
    pile["charge_minutes"] = query.value("charge_minutes").toInt();
    pile["price"] = query.value("price").toDouble();
    return pile;
}

bool DbManager::updatePileStatus(int pileId, const QString &status)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE pile SET status = :status, updated_at = datetime('now','localtime') WHERE id = :id");
    query.bindValue(":status", status);
    query.bindValue(":id", pileId);
    return query.exec();
}

QJsonArray DbManager::fetchPiles(int stationId, const QString &status, const QString &keyword)
{
    QSqlQuery query(m_db);
    QString sql =
        "SELECT p.pile_no, s.name AS station_name, p.type, p.power_kw, p.status, "
        "p.charge_count, p.charge_minutes "
        "FROM pile p JOIN station s ON p.station_id = s.id WHERE 1=1";

    if (stationId > 0) {
        sql += " AND p.station_id = :sid";
    }
    if (!status.isEmpty()) {
        sql += " AND p.status = :status";
    }
    if (!keyword.isEmpty()) {
        sql += " AND (p.pile_no LIKE :like OR s.name LIKE :like)";
    }
    sql += " ORDER BY p.pile_no";

    query.prepare(sql);
    if (stationId > 0) query.bindValue(":sid", stationId);
    if (!status.isEmpty()) query.bindValue(":status", status);
    if (!keyword.isEmpty()) query.bindValue(":like", "%" + keyword + "%");

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchPiles failed:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        QJsonObject row;
        row["pile_no"] = query.value("pile_no").toString();
        row["station_name"] = query.value("station_name").toString();
        row["type"] = query.value("type").toString();
        row["power_kw"] = query.value("power_kw").toDouble();
        row["status"] = query.value("status").toString();
        row["charge_count"] = query.value("charge_count").toInt();
        row["charge_minutes"] = query.value("charge_minutes").toInt();
        items.append(row);
    }
    return items;
}

bool DbManager::restartPile(const QString &pileNo)
{
    auto pileOpt = findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return false;
    }
    return updatePileStatus(pileOpt.value().value("id").toInt(), QStringLiteral("闲置"));
}

bool DbManager::updatePile(const QString &pileNo, const QString &type,
                            double powerKw, const QString &status)
{
    QSqlQuery query(m_db);
    QString sql = "UPDATE pile SET ";
    QStringList sets;
    if (!type.isEmpty()) sets << "type = :type";
    if (powerKw > 0) sets << "power_kw = :power";
    if (!status.isEmpty()) sets << "status = :status";
    if (sets.isEmpty()) {
        return false;
    }
    sql += sets.join(", ");
    sql += ", updated_at = datetime('now','localtime') WHERE pile_no = :no";

    query.prepare(sql);
    if (!type.isEmpty()) query.bindValue(":type", type);
    if (powerKw > 0) query.bindValue(":power", powerKw);
    if (!status.isEmpty()) query.bindValue(":status", status);
    query.bindValue(":no", pileNo);
    return query.exec();
}

bool DbManager::deletePile(const QString &pileNo)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM pile WHERE pile_no = :no");
    query.bindValue(":no", pileNo);
    return query.exec();
}

bool DbManager::pileHasOpenOrders(const QString &pileNo)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT COUNT(*) FROM charge_order o "
        "JOIN pile p ON o.pile_id = p.id "
        "WHERE p.pile_no = :no AND o.status IN ('预约', '充电中', '待支付')");
    query.bindValue(":no", pileNo);
    if (!query.exec() || !query.next()) {
        return false;
    }
    return query.value(0).toInt() > 0;
}

// ============================================================
// 订单
// ============================================================

std::optional<QJsonObject> DbManager::findOpenOrder(int userId)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT o.id, o.order_no, o.user_id, o.station_id, o.pile_id, o.status, "
        "o.reserve_at, o.start_at, o.end_at, o.kwh, o.amount, "
        "s.name AS station_name, p.pile_no "
        "FROM charge_order o "
        "JOIN station s ON o.station_id = s.id "
        "JOIN pile p ON o.pile_id = p.id "
        "WHERE o.user_id = :uid AND o.status IN ('预约', '充电中', '待支付') "
        "ORDER BY o.id DESC LIMIT 1");
    query.bindValue(":uid", userId);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject order;
    order["order_id"] = query.value("id").toInt();
    order["order_no"] = query.value("order_no").toString();
    order["user_id"] = query.value("user_id").toInt();
    order["station_id"] = query.value("station_id").toInt();
    order["pile_id"] = query.value("pile_id").toInt();
    order["status"] = query.value("status").toString();
    order["reserve_at"] = query.value("reserve_at").toString();
    order["start_at"] = query.value("start_at").toString();
    order["end_at"] = query.value("end_at").toString();
    order["kwh"] = query.value("kwh").toDouble();
    order["amount"] = query.value("amount").toDouble();
    order["station_name"] = query.value("station_name").toString();
    order["pile_no"] = query.value("pile_no").toString();
    return order;
}

std::optional<QJsonObject> DbManager::findOrderByNo(const QString &orderNo)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT o.id, o.order_no, o.user_id, o.station_id, o.pile_id, o.status, "
        "o.reserve_at, o.start_at, o.end_at, o.kwh, o.amount, "
        "s.name AS station_name, p.pile_no, p.power_kw, s.price, u.phone "
        "FROM charge_order o "
        "JOIN station s ON o.station_id = s.id "
        "JOIN pile p ON o.pile_id = p.id "
        "JOIN user u ON o.user_id = u.id "
        "WHERE o.order_no = :no");
    query.bindValue(":no", orderNo);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject order;
    order["order_id"] = query.value("id").toInt();
    order["order_no"] = query.value("order_no").toString();
    order["user_id"] = query.value("user_id").toInt();
    order["station_id"] = query.value("station_id").toInt();
    order["pile_id"] = query.value("pile_id").toInt();
    order["status"] = query.value("status").toString();
    order["reserve_at"] = query.value("reserve_at").toString();
    order["start_at"] = query.value("start_at").toString();
    order["end_at"] = query.value("end_at").toString();
    order["kwh"] = query.value("kwh").toDouble();
    order["amount"] = query.value("amount").toDouble();
    order["station_name"] = query.value("station_name").toString();
    order["pile_no"] = query.value("pile_no").toString();
    order["power_kw"] = query.value("power_kw").toDouble();
    order["price"] = query.value("price").toDouble();
    order["phone"] = query.value("phone").toString();
    return order;
}

QString DbManager::createOrder(int userId, int stationId, int pileId)
{
    // 生成订单号：CD + YYYYMMDD + 3位序号
    const QString dateStr = QDate::currentDate().toString("yyyyMMdd");
    const QString prefix = "CD" + dateStr;

    QSqlQuery countQuery(m_db);
    countQuery.prepare("SELECT COUNT(*) FROM charge_order WHERE order_no LIKE :prefix");
    countQuery.bindValue(":prefix", prefix + "%");
    countQuery.exec();
    int seq = 1;
    if (countQuery.next()) {
        seq = countQuery.value(0).toInt() + 1;
    }

    const QString orderNo = QString("%1%2").arg(prefix).arg(seq, 3, 10, QChar('0'));

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO charge_order (order_no, user_id, station_id, pile_id, status, reserve_at) "
        "VALUES (:no, :uid, :sid, :pid, '预约', datetime('now','localtime'))");
    query.bindValue(":no", orderNo);
    query.bindValue(":uid", userId);
    query.bindValue(":sid", stationId);
    query.bindValue(":pid", pileId);

    if (!query.exec()) {
        qWarning() << "createOrder failed:" << query.lastError().text();
        return QString();
    }
    return orderNo;
}

bool DbManager::updateOrderStatus(const QString &orderNo, const QString &status,
                                   const QString &startAt, const QString &endAt,
                                   double kwh, double amount)
{
    QString sql = "UPDATE charge_order SET status = :status";
    if (!startAt.isEmpty()) sql += ", start_at = :start_at";
    if (!endAt.isEmpty()) sql += ", end_at = :end_at";
    if (kwh >= 0) sql += ", kwh = :kwh";
    if (amount >= 0) sql += ", amount = :amount";
    sql += " WHERE order_no = :no";

    QSqlQuery query(m_db);
    query.prepare(sql);
    query.bindValue(":status", status);
    if (!startAt.isEmpty()) query.bindValue(":start_at", startAt);
    if (!endAt.isEmpty()) query.bindValue(":end_at", endAt);
    if (kwh >= 0) query.bindValue(":kwh", kwh);
    if (amount >= 0) query.bindValue(":amount", amount);
    query.bindValue(":no", orderNo);
    return query.exec();
}

QJsonArray DbManager::fetchOrders(int userId, const QString &status, int limit,
                                   const QString &phone, const QString &dateFrom, const QString &dateTo)
{
    QSqlQuery query(m_db);
    QString sql =
        "SELECT o.order_no, u.phone, s.name AS station_name, p.pile_no, "
        "o.status, o.reserve_at, o.start_at, o.end_at, o.kwh, o.amount "
        "FROM charge_order o "
        "JOIN user u ON o.user_id = u.id "
        "JOIN station s ON o.station_id = s.id "
        "JOIN pile p ON o.pile_id = p.id "
        "WHERE 1=1";

    if (userId > 0) sql += " AND o.user_id = :uid";
    if (!status.isEmpty()) sql += " AND o.status = :status";
    if (!phone.isEmpty()) sql += " AND u.phone LIKE :phone";
    if (!dateFrom.isEmpty()) sql += " AND date(o.created_at) >= :date_from";
    if (!dateTo.isEmpty()) sql += " AND date(o.created_at) <= :date_to";
    sql += " ORDER BY o.id DESC LIMIT :limit";

    query.prepare(sql);
    if (userId > 0) query.bindValue(":uid", userId);
    if (!status.isEmpty()) query.bindValue(":status", status);
    if (!phone.isEmpty()) query.bindValue(":phone", "%" + phone + "%");
    if (!dateFrom.isEmpty()) query.bindValue(":date_from", dateFrom);
    if (!dateTo.isEmpty()) query.bindValue(":date_to", dateTo);
    query.bindValue(":limit", limit > 0 ? limit : 50);

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchOrders failed:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        QJsonObject row;
        row["order_no"] = query.value("order_no").toString();
        row["phone"] = query.value("phone").toString();
        row["station_name"] = query.value("station_name").toString();
        row["pile_no"] = query.value("pile_no").toString();
        row["status"] = query.value("status").toString();
        row["reserve_at"] = query.value("reserve_at").toString();
        row["start_at"] = query.value("start_at").toString();
        row["end_at"] = query.value("end_at").toString();
        row["kwh"] = query.value("kwh").toDouble();
        row["amount"] = query.value("amount").toDouble();
        items.append(row);
    }
    return items;
}

std::optional<double> DbManager::settleOrder(const QString &orderNo, int userId, int adminId)
{
    const auto orderOpt = findOrderByNo(orderNo);
    if (!orderOpt.has_value()) {
        return std::nullopt;
    }
    const QJsonObject order = orderOpt.value();
    const double amount = order.value("amount").toDouble();
    const int orderId = order.value("order_id").toInt();

    const auto userOpt = findUserById(userId);
    if (!userOpt.has_value() || userOpt.value().value("balance").toDouble() < amount) {
        return std::nullopt;
    }

    std::optional<double> balanceAfter;
    const bool ok = runInTransaction([&]() {
        QSqlQuery query(m_db);
        query.prepare(
            "UPDATE user SET balance = balance - :amount "
            "WHERE id = :id AND balance >= :amount");
        query.bindValue(":amount", amount);
        query.bindValue(":id", userId);
        if (!query.exec() || query.numRowsAffected() == 0) {
            qWarning() << "settleOrder deduct failed:" << query.lastError().text();
            return false;
        }

        if (!writeWalletLog(userId, -amount, QStringLiteral("充电结算"), orderId)) {
            return false;
        }

        const int pileId = order.value("pile_id").toInt();
        const double kwh = order.value("kwh").toDouble();
        const double powerKw = order.value("power_kw").toDouble();
        const int minutes = powerKw > 0 ? qRound(kwh / powerKw * 60) : 0;

        QSqlQuery pileQuery(m_db);
        pileQuery.prepare(
            "UPDATE pile SET charge_count = charge_count + 1, "
            "charge_minutes = charge_minutes + :minutes WHERE id = :id");
        pileQuery.bindValue(":minutes", minutes);
        pileQuery.bindValue(":id", pileId);
        if (!pileQuery.exec()) {
            qWarning() << "settleOrder pile stats failed:" << pileQuery.lastError().text();
            return false;
        }

        QSqlQuery orderQuery(m_db);
        orderQuery.prepare(
            "UPDATE charge_order SET status = :status "
            "WHERE order_no = :no AND status = :expected");
        orderQuery.bindValue(":status", QStringLiteral("已完成"));
        orderQuery.bindValue(":no", orderNo);
        orderQuery.bindValue(":expected", QStringLiteral("待支付"));
        if (!orderQuery.exec() || orderQuery.numRowsAffected() == 0) {
            qWarning() << "settleOrder complete order failed:" << orderQuery.lastError().text();
            return false;
        }

        if (adminId > 0) {
            if (!writeOperationLog(
                    adminId, QStringLiteral("代结算"),
                    QStringLiteral("order"), orderNo,
                    QString("代用户结算订单 %1，金额 %2 元").arg(orderNo).arg(amount))) {
                return false;
            }
        }

        const auto updatedUser = findUserById(userId);
        if (!updatedUser.has_value()) {
            return false;
        }
        balanceAfter = updatedUser.value().value("balance").toDouble();
        return true;
    });

    if (!ok) {
        return std::nullopt;
    }
    return balanceAfter;
}

std::optional<QString> DbManager::reservePile(int userId, int stationId, int pileId)
{
    std::optional<QString> orderNo;
    const bool ok = runInTransaction([&]() {
        const QString created = createOrder(userId, stationId, pileId);
        if (created.isEmpty()) {
            return false;
        }

        QSqlQuery query(m_db);
        query.prepare(
            "UPDATE pile SET status = :status, updated_at = datetime('now','localtime') "
            "WHERE id = :id AND status = :expected");
        query.bindValue(":status", QStringLiteral("预约"));
        query.bindValue(":id", pileId);
        query.bindValue(":expected", QStringLiteral("闲置"));
        if (!query.exec() || query.numRowsAffected() == 0) {
            qWarning() << "reservePile update pile failed:" << query.lastError().text();
            return false;
        }

        orderNo = created;
        return true;
    });

    if (!ok) {
        return std::nullopt;
    }
    return orderNo;
}

bool DbManager::startCharge(const QString &orderNo, int pileId, const QString &startAt)
{
    return runInTransaction([&]() {
        QSqlQuery orderQuery(m_db);
        orderQuery.prepare(
            "UPDATE charge_order SET status = :status, start_at = :start_at "
            "WHERE order_no = :no AND status = :expected");
        orderQuery.bindValue(":status", QStringLiteral("充电中"));
        orderQuery.bindValue(":start_at", startAt);
        orderQuery.bindValue(":no", orderNo);
        orderQuery.bindValue(":expected", QStringLiteral("预约"));
        if (!orderQuery.exec() || orderQuery.numRowsAffected() == 0) {
            qWarning() << "startCharge update order failed:" << orderQuery.lastError().text();
            return false;
        }
        return updatePileStatus(pileId, QStringLiteral("在用"));
    });
}

bool DbManager::stopCharge(const QString &orderNo, int pileId, const QString &endAt,
                           double kwh, double amount)
{
    return runInTransaction([&]() {
        QSqlQuery orderQuery(m_db);
        orderQuery.prepare(
            "UPDATE charge_order SET status = :status, end_at = :end_at, kwh = :kwh, amount = :amount "
            "WHERE order_no = :no AND status = :expected");
        orderQuery.bindValue(":status", QStringLiteral("待支付"));
        orderQuery.bindValue(":end_at", endAt);
        orderQuery.bindValue(":kwh", kwh);
        orderQuery.bindValue(":amount", amount);
        orderQuery.bindValue(":no", orderNo);
        orderQuery.bindValue(":expected", QStringLiteral("充电中"));
        if (!orderQuery.exec() || orderQuery.numRowsAffected() == 0) {
            qWarning() << "stopCharge update order failed:" << orderQuery.lastError().text();
            return false;
        }
        return updatePileStatus(pileId, QStringLiteral("闲置"));
    });
}

void DbManager::cancelExpiredReservations()
{
    // 预约超过 3 小时（180 分钟）未开始充电 → 自动取消。
    // 协议状态机只有 预约/充电中/待支付/已完成 四态，无"已取消"，
    // 故超时预约直接删除订单行（订单作废），电桩恢复"闲置"。
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT o.order_no, o.pile_id FROM charge_order o "
        "WHERE o.status = '预约' "
        "AND (julianday('now','localtime') - julianday(o.reserve_at)) * 1440 >= 180");
    if (!query.exec()) {
        qWarning() << "cancelExpiredReservations query failed:" << query.lastError().text();
        return;
    }
    while (query.next()) {
        const QString orderNo = query.value(0).toString();
        const int pileId = query.value(1).toInt();
        const bool ok = runInTransaction([&]() {
            QSqlQuery del(m_db);
            del.prepare("DELETE FROM charge_order WHERE order_no = :no AND status = '预约'");
            del.bindValue(":no", orderNo);
            if (!del.exec() || del.numRowsAffected() == 0) {
                qWarning() << "cancelExpiredReservations delete failed:" << del.lastError().text();
                return false;
            }
            return updatePileStatus(pileId, QStringLiteral("闲置"));
        });
        if (ok) {
            qInfo() << "预约超时自动取消:" << orderNo;
        }
    }
}

// ============================================================
// 统计
// ============================================================

QJsonObject DbManager::fetchStatsOverview(int days)
{
    QJsonObject result;

    // 今日营收和订单数
    QSqlQuery todayQuery(m_db);
    todayQuery.prepare(
        "SELECT COALESCE(SUM(amount),0) AS revenue, COUNT(*) AS orders "
        "FROM charge_order WHERE status = '已完成' AND date(created_at) = date('now','localtime')");
    if (todayQuery.exec() && todayQuery.next()) {
        result["today_revenue"] = todayQuery.value("revenue").toDouble();
        result["today_orders"] = todayQuery.value("orders").toInt();
    } else {
        result["today_revenue"] = 0.0;
        result["today_orders"] = 0;
    }

    // 本月营收
    QSqlQuery monthQuery(m_db);
    monthQuery.prepare(
        "SELECT COALESCE(SUM(amount),0) FROM charge_order "
        "WHERE status = '已完成' AND strftime('%Y-%m', created_at) = strftime('%Y-%m', 'now','localtime')");
    if (monthQuery.exec() && monthQuery.next()) {
        result["month_revenue"] = monthQuery.value(0).toDouble();
    } else {
        result["month_revenue"] = 0.0;
    }

    // 总营收
    QSqlQuery totalQuery(m_db);
    totalQuery.exec("SELECT COALESCE(SUM(amount),0) FROM charge_order WHERE status = '已完成'");
    if (totalQuery.next()) {
        result["total_revenue"] = totalQuery.value(0).toDouble();
    } else {
        result["total_revenue"] = 0.0;
    }

    // 用户数
    QSqlQuery userQuery(m_db);
    userQuery.exec("SELECT COUNT(*) FROM user");
    if (userQuery.next()) {
        result["user_count"] = userQuery.value(0).toInt();
    } else {
        result["user_count"] = 0;
    }

    // 营收趋势（近 N 天）
    QJsonArray trend;
    QSqlQuery trendQuery(m_db);
    trendQuery.prepare(
        "SELECT date(created_at) AS d, COALESCE(SUM(amount),0) AS revenue "
        "FROM charge_order WHERE status = '已完成' "
        "AND date(created_at) >= date('now','localtime', :offset) "
        "GROUP BY date(created_at) ORDER BY d");
    trendQuery.bindValue(":offset", QString("-%1 days").arg(days - 1));
    if (trendQuery.exec()) {
        while (trendQuery.next()) {
            QJsonObject day;
            day["date"] = trendQuery.value("d").toString();
            day["revenue"] = trendQuery.value("revenue").toDouble();
            trend.append(day);
        }
    }
    result["revenue_trend"] = trend;

    // 电桩状态分布（四类状态始终返回，无桩时为 0，便于 admin 总览页计算）
    QJsonObject pileStatus;
    const QStringList allPileStatuses = {
        QStringLiteral("闲置"), QStringLiteral("预约"),
        QStringLiteral("在用"), QStringLiteral("故障"),
    };
    for (const QString &status : allPileStatuses) {
        pileStatus[status] = 0;
    }
    QSqlQuery pileStatusQuery(m_db);
    pileStatusQuery.exec("SELECT status, COUNT(*) AS cnt FROM pile GROUP BY status");
    while (pileStatusQuery.next()) {
        pileStatus[pileStatusQuery.value("status").toString()] = pileStatusQuery.value("cnt").toInt();
    }
    result["pile_status"] = pileStatus;

    // 站点营收排名
    QJsonArray stationRank;
    QSqlQuery rankQuery(m_db);
    rankQuery.prepare(
        "SELECT s.name, COALESCE(SUM(o.amount),0) AS revenue "
        "FROM station s LEFT JOIN charge_order o ON s.id = o.station_id AND o.status = '已完成' "
        "GROUP BY s.id ORDER BY revenue DESC LIMIT 5");
    if (rankQuery.exec()) {
        while (rankQuery.next()) {
            QJsonObject station;
            station["name"] = rankQuery.value("name").toString();
            station["revenue"] = rankQuery.value("revenue").toDouble();
            stationRank.append(station);
        }
    }
    result["station_rank"] = stationRank;

    return result;
}

// ============================================================
// 收藏
// ============================================================

bool DbManager::addFavorite(int userId, int stationId)
{
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT OR IGNORE INTO favorite_station (user_id, station_id) VALUES (:uid, :sid)");
    query.bindValue(":uid", userId);
    query.bindValue(":sid", stationId);
    return query.exec();
}

bool DbManager::removeFavorite(int userId, int stationId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM favorite_station WHERE user_id = :uid AND station_id = :sid");
    query.bindValue(":uid", userId);
    query.bindValue(":sid", stationId);
    return query.exec();
}

QJsonArray DbManager::listFavorites(int userId)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT s.id, s.name, s.address, s.lat, s.lng, s.price, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id) AS total_piles, "
        "(SELECT COUNT(*) FROM pile p WHERE p.station_id = s.id AND p.status = '闲置') AS idle_piles "
        "FROM favorite_station f JOIN station s ON f.station_id = s.id "
        "WHERE f.user_id = :uid ORDER BY f.created_at DESC");
    query.bindValue(":uid", userId);

    QJsonArray items;
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        QJsonObject row;
        row["id"] = query.value("id").toInt();
        row["name"] = query.value("name").toString();
        row["address"] = query.value("address").toString();
        row["lat"] = query.value("lat").toDouble();
        row["lng"] = query.value("lng").toDouble();
        row["price"] = query.value("price").toDouble();
        row["total_piles"] = query.value("total_piles").toInt();
        row["idle_piles"] = query.value("idle_piles").toInt();
        items.append(row);
    }
    return items;
}

// ============================================================
// 公告
// ============================================================

QJsonArray DbManager::fetchAnnouncements()
{
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, content, created_at FROM announcement WHERE is_active = 1 ORDER BY id DESC");

    QJsonArray items;
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        QJsonObject row;
        row["id"] = query.value("id").toInt();
        row["title"] = query.value("title").toString();
        row["content"] = query.value("content").toString();
        row["created_at"] = query.value("created_at").toString();
        items.append(row);
    }
    return items;
}

// ============================================================
// 负荷预测
// ============================================================

QJsonArray DbManager::fetchForecasts(const QString &horizon, int stationId)
{
    // horizon: 1h / 6h / 24h，取自 load_forecast 表（俞莫凡的采集/预测写入）
    QSqlQuery query(m_db);
    QString sql =
        "SELECT f.station_id, s.name AS station_name, f.forecast_hour, "
        "f.predicted_load, f.predicted_idle_piles, f.created_at "
        "FROM load_forecast f JOIN station s ON f.station_id = s.id "
        "WHERE f.horizon = :h ";
    if (stationId > 0) {
        sql += "AND f.station_id = :sid ";
    }
    sql += "ORDER BY f.station_id, f.forecast_hour";

    query.prepare(sql);
    query.bindValue(":h", horizon);
    if (stationId > 0) {
        query.bindValue(":sid", stationId);
    }

    QJsonArray items;
    if (!query.exec()) {
        qWarning() << "fetchForecasts failed:" << query.lastError().text();
        return items;
    }
    while (query.next()) {
        QJsonObject row;
        row["station_id"] = query.value("station_id").toInt();
        row["station_name"] = query.value("station_name").toString();
        row["forecast_hour"] = query.value("forecast_hour").toString();
        row["predicted_load"] = query.value("predicted_load").toDouble();
        row["predicted_idle_piles"] = query.value("predicted_idle_piles").toInt();
        items.append(row);
    }
    return items;
}

// ============================================================
// 日志
// ============================================================

bool DbManager::writeOperationLog(int adminId, const QString &action,
                                   const QString &targetType, const QString &targetId,
                                   const QString &detail)
{
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO operation_log (admin_id, action, target_type, target_id, detail) "
        "VALUES (:aid, :action, :ttype, :tid, :detail)");
    query.bindValue(":aid", adminId);
    query.bindValue(":action", action);
    query.bindValue(":ttype", targetType);
    query.bindValue(":tid", targetId);
    query.bindValue(":detail", detail);
    if (!query.exec()) {
        qWarning() << "writeOperationLog failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DbManager::writeWalletLog(int userId, double delta, const QString &reason, int orderId)
{
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO wallet_log (user_id, delta, reason, order_id) "
        "VALUES (:uid, :delta, :reason, :oid)");
    query.bindValue(":uid", userId);
    query.bindValue(":delta", delta);
    query.bindValue(":reason", reason);
    query.bindValue(":oid", orderId > 0 ? QVariant(orderId) : QVariant());
    if (!query.exec()) {
        qWarning() << "writeWalletLog failed:" << query.lastError().text();
        return false;
    }
    return true;
}
