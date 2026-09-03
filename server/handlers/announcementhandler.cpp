#include "announcementhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

QJsonObject AnnouncementHandler::list(const QString &id, const QString &token, const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchAnnouncements();
    return Protocol::makeSuccess(id, responseData);
}
