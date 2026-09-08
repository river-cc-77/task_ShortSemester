#include "announcementhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

namespace {

QJsonObject authUser(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role == QStringLiteral("user")) {
        const auto user = DbManager::instance().findUserById(session.userId);
        if (user.has_value() && user.value().value("status").toString() == QStringLiteral("冻结")) {
            return Protocol::makeError(id, "USER_FROZEN", "账号已被冻结，请联系客服");
        }
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

} // namespace

QJsonObject AnnouncementHandler::list(const QString &id, const QString &token, const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) {
        return auth;
    }

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchAnnouncements();
    return Protocol::makeSuccess(id, responseData);
}

QJsonObject AnnouncementHandler::adminList(const QString &id, const QString &token,
                                           const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) {
        return auth;
    }

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchAdminAnnouncements();
    return Protocol::makeSuccess(id, responseData);
}

QJsonObject AnnouncementHandler::create(const QString &id, const QString &token,
                                        const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) {
        return auth;
    }

    const QString title = data.value("title").toString().trimmed();
    const QString content = data.value("content").toString().trimmed();
    const bool isActive = data.value("is_active").toBool(true);
    if (title.isEmpty() || content.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "标题和内容不能为空");
    }

    if (!DbManager::instance().createAnnouncement(title, content, isActive)) {
        return Protocol::makeError(id, "DB_ERROR", "创建公告失败");
    }

    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("新增公告"),
        QStringLiteral("announcement"), title);

    QJsonObject responseData;
    responseData["title"] = title;
    return Protocol::makeSuccess(id, responseData);
}

QJsonObject AnnouncementHandler::update(const QString &id, const QString &token,
                                        const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) {
        return auth;
    }

    const int annId = data.value("id").toInt();
    if (annId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 id");
    }

    const QString title = data.contains("title") ? data.value("title").toString().trimmed() : QString();
    const QString content = data.contains("content") ? data.value("content").toString().trimmed() : QString();
    int isActive = -1;
    if (data.contains("is_active")) {
        isActive = data.value("is_active").toBool() ? 1 : 0;
    }
    if (title.isEmpty() && content.isEmpty() && isActive < 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "至少提供一个要修改的字段");
    }

    if (!DbManager::instance().updateAnnouncement(annId, title, content, isActive)) {
        return Protocol::makeError(id, "NOT_FOUND", "公告不存在或更新失败");
    }

    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("修改公告"),
        QStringLiteral("announcement"), QString::number(annId));

    QJsonObject responseData;
    responseData["id"] = annId;
    return Protocol::makeSuccess(id, responseData);
}

QJsonObject AnnouncementHandler::remove(const QString &id, const QString &token,
                                          const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) {
        return auth;
    }

    const int annId = data.value("id").toInt();
    if (annId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 id");
    }

    if (!DbManager::instance().deleteAnnouncement(annId)) {
        return Protocol::makeError(id, "NOT_FOUND", "公告不存在或删除失败");
    }

    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("删除公告"),
        QStringLiteral("announcement"), QString::number(annId));

    QJsonObject responseData;
    responseData["id"] = annId;
    return Protocol::makeSuccess(id, responseData);
}
