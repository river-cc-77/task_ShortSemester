#include "adminhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QCryptographicHash>
#include <QDebug>

namespace {

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

QJsonObject AdminHandler::login(const QString &id, const QJsonObject &data)
{
    const QString username = data.value("username").toString().trimmed();
    const QString password = data.value("password").toString().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "账号和密码不能为空");
    }

    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

    const auto adminOpt = DbManager::instance().findAdminByUsername(username);
    if (!adminOpt.has_value()) {
        return Protocol::makeError(id, "UNAUTHORIZED", "账号或密码错误");
    }

    const QJsonObject admin = adminOpt.value();
    const QByteArray hash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    const QString passwordHash = QString::fromLatin1(hash.toHex()).trimmed().toLower();
    const QString storedHash = admin.value("password_hash").toString().trimmed().toLower();

    if (passwordHash != storedHash) {
        qWarning() << "admin.login hash mismatch for" << username
                   << "expected len:" << storedHash.size()
                   << "got len:" << passwordHash.size();
        return Protocol::makeError(id, "UNAUTHORIZED", "账号或密码错误");
    }

    const int adminId = admin.value("admin_id").toInt();
    const QString token = AuthManager::instance().createAdminToken(adminId);

    QJsonObject responseData;
    responseData["token"] = token;
    responseData["admin_id"] = adminId;
    responseData["username"] = admin.value("username").toString();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.list — 电桩列表
// ============================================================
QJsonObject AdminHandler::pileList(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const int stationId = data.value("station_id").toInt(0);
    const QString status = data.value("status").toString().trimmed();
    const QString keyword = data.value("keyword").toString().trimmed();

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchPiles(stationId, status, keyword);
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.restart — 远程重启电桩
// ============================================================
QJsonObject AdminHandler::pileRestart(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString pileNo = data.value("pile_no").toString().trimmed();
    if (pileNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 pile_no");
    }

    const auto pileOpt = DbManager::instance().findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "电桩不存在");
    }

    if (!DbManager::instance().restartPile(pileNo)) {
        return Protocol::makeError(id, "DB_ERROR", "重启失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("远程重启电桩"),
        QStringLiteral("pile"), pileNo);

    QJsonObject responseData;
    responseData["pile_no"] = pileNo;
    responseData["status"] = QStringLiteral("闲置");
    return Protocol::makeSuccess(id, responseData);
}
