#include "stationhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>

namespace {

QJsonObject authUser(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("user")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要用户端登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return {};
}

QJsonObject authAdmin(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("admin")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要管理员登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return {};
}

double haversineKm(double lat1, double lng1, double lat2, double lng2)
{
    constexpr double kEarthRadiusKm = 6371.0;
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLng = qDegreesToRadians(lng2 - lng1);
    const double a = qSin(dLat / 2) * qSin(dLat / 2) +
                     qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2)) *
                         qSin(dLng / 2) * qSin(dLng / 2);
    const double c = 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
    return kEarthRadiusKm * c;
}

double round1(double value)
{
    return qRound(value * 10.0) / 10.0;
}

double onlineRate(int totalPiles, int idlePiles)
{
    if (totalPiles <= 0) return 0.0;
    return qRound(idlePiles * 1000.0 / totalPiles) / 1000.0;
}

} // namespace

// ============================================================
// station.list — 附近充电站列表
// ============================================================
QJsonObject StationHandler::list(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    if (!data.contains("lat") || !data.contains("lng")) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 lat 或 lng");
    }

    const double lat = data.value("lat").toDouble();
    const double lng = data.value("lng").toDouble();
    const QString keyword = data.value("keyword").toString().trimmed();

    QJsonArray rows = DbManager::instance().fetchStations(keyword);
    QList<QJsonObject> items;

    for (const QJsonValue &value : rows) {
        QJsonObject row = value.toObject();
        const double stationLat = row.value("lat").toDouble();
        const double stationLng = row.value("lng").toDouble();
        const int totalPiles = row.value("total_piles").toInt();
        const int idlePiles = row.value("idle_piles").toInt();

        row["distance_km"] = round1(haversineKm(lat, lng, stationLat, stationLng));
        row["online_rate"] = onlineRate(totalPiles, idlePiles);
        row["recommended"] = false;
        items.append(row);
    }

    std::sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("distance_km").toDouble() < b.value("distance_km").toDouble();
    });

    QJsonArray sorted;
    for (const QJsonObject &item : items) {
        sorted.append(item);
    }

    QJsonObject responseData;
    responseData["items"] = sorted;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// station.detail — 电站详情
// ============================================================
QJsonObject StationHandler::detail(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    if (!data.contains("station_id")) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 station_id");
    }

    const int stationId = data.value("station_id").toInt();
    if (stationId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "station_id 无效");
    }

    const auto detailOpt = DbManager::instance().fetchStationDetail(stationId);
    if (!detailOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "充电站不存在");
    }

    return Protocol::makeSuccess(id, detailOpt.value());
}

// ============================================================
// station.admin.list — 管理端电站列表
// ============================================================
QJsonObject StationHandler::adminList(const QString &id, const QString &token, const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchAdminStations();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// station.create — 新增电站
// ============================================================
QJsonObject StationHandler::create(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    const double lat = data.value("lat").toDouble();
    const double lng = data.value("lng").toDouble();
    const double price = data.value("price").toDouble();
    const int fastCount = data.value("fast_count").toInt(0);
    const int slowCount = data.value("slow_count").toInt(0);

    if (name.isEmpty() || address.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "站名和地址不能为空");
    }
    if (price <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "电价必须大于 0");
    }
    if (fastCount <= 0 && slowCount <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "至少需要一个电桩");
    }

    const int stationId = DbManager::instance().createStation(
        name, address, lat, lng, price, fastCount, slowCount);
    if (stationId <= 0) {
        return Protocol::makeError(id, "DB_ERROR", "创建电站失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("新增电站"),
        QStringLiteral("station"), QString::number(stationId),
        QString("站名: %1, 快充: %2, 慢充: %3").arg(name).arg(fastCount).arg(slowCount));

    QJsonObject responseData;
    responseData["station_id"] = stationId;
    responseData["name"] = name;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// station.favorite.add — 收藏电站
// ============================================================
QJsonObject StationHandler::favoriteAdd(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const int stationId = data.value("station_id").toInt();
    if (stationId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 station_id");
    }

    if (!DbManager::instance().addFavorite(session.userId, stationId)) {
        return Protocol::makeError(id, "DB_ERROR", "收藏失败");
    }

    QJsonObject responseData;
    responseData["station_id"] = stationId;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// station.favorite.remove — 取消收藏
// ============================================================
QJsonObject StationHandler::favoriteRemove(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const int stationId = data.value("station_id").toInt();
    if (stationId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 station_id");
    }

    if (!DbManager::instance().removeFavorite(session.userId, stationId)) {
        return Protocol::makeError(id, "DB_ERROR", "取消收藏失败");
    }

    QJsonObject responseData;
    responseData["station_id"] = stationId;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// station.favorite.list — 收藏列表
// ============================================================
QJsonObject StationHandler::favoriteList(const QString &id, const QString &token, const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().listFavorites(session.userId);
    return Protocol::makeSuccess(id, responseData);
}
