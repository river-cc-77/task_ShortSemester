#include "userhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QRegularExpression>

QJsonObject UserHandler::login(const QString &id, const QJsonObject &data)
{
    const QString phone = data.value("phone").toString().trimmed();
    static const QRegularExpression phonePattern(QStringLiteral("^\\d{11}$"));

    if (!phonePattern.match(phone).hasMatch()) {
        return Protocol::makeError(id, "INVALID_PARAM", "请输入11位手机号");
    }

    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

    auto userOpt = DbManager::instance().findUserByPhone(phone);
    if (!userOpt.has_value()) {
        const QJsonObject created = DbManager::instance().createUser(phone);
        if (created.isEmpty()) {
            return Protocol::makeError(id, "DB_ERROR", "自动注册失败");
        }
        userOpt = created;
    }

    QJsonObject user = userOpt.value();
    if (user.value("status").toString() == QStringLiteral("冻结")) {
        return Protocol::makeError(id, "USER_FROZEN", "账号已被冻结，请联系客服");
    }

    const int userId = user.value("user_id").toInt();
    const QString token = AuthManager::instance().createUserToken(userId);

    QJsonObject responseData = user;
    responseData["token"] = token;
    return Protocol::makeSuccess(id, responseData);
}
