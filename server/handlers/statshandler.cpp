#include "statshandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

QJsonObject StatsHandler::overview(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("admin")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要管理员登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

    const int days = data.value("days").toInt(7);
    const QJsonObject result = DbManager::instance().fetchStatsOverview(days);

    return Protocol::makeSuccess(id, result);
}
