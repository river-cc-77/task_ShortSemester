#include "adminhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QCryptographicHash>

QJsonObject AdminHandler::login(const QString &id, const QJsonObject &data)
{
    const QString username = data.value("username").toString().trimmed();
    const QString password = data.value("password").toString();

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
    const QString passwordHash = QString::fromLatin1(hash.toHex());

    if (passwordHash != admin.value("password_hash").toString()) {
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
