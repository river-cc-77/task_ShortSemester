#include "stationhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QtMath>

#include <QJsonArray>

#include <algorithm>

namespace {

QJsonObject authError(const QString &id, const QString &token)
{
    SessionInfo session;
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("user")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要用户端登录");
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
    if (totalPiles <= 0) {
        return 0.0;
    }
    return qRound(idlePiles * 1000.0 / totalPiles) / 1000.0;
}

} // namespace

QJsonObject StationHandler::list(const QString &id, const QString &token, const QJsonObject &data)
{
    const QJsonObject auth = authError(id, token);
    if (!auth.isEmpty()) {
        return auth;
    }

    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

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

QJsonObject StationHandler::detail(const QString &id, const QString &token, const QJsonObject &data)
{
    const QJsonObject auth = authError(id, token);
    if (!auth.isEmpty()) {
        return auth;
    }

    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

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
