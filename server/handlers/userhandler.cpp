#include "userhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QRegularExpression>

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

} // namespace

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

// ============================================================
// user.profile.update — 修改昵称/头像
// ============================================================
QJsonObject UserHandler::profileUpdate(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString nickname = data.value("nickname").toString().trimmed();
    const QString avatarPath = data.value("avatar_path").toString().trimmed();

    if (nickname.isEmpty() && avatarPath.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "至少提供 nickname 或 avatar_path");
    }

    // 获取当前用户信息，保留未更新的字段
    auto userOpt = DbManager::instance().findUserById(session.userId);
    if (!userOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "用户不存在");
    }
    QJsonObject user = userOpt.value();

    const QString newNickname = nickname.isEmpty() ? user.value("nickname").toString() : nickname;
    const QString newAvatar = avatarPath.isEmpty() ? user.value("avatar_path").toString() : avatarPath;

    if (!DbManager::instance().updateUserProfile(session.userId, newNickname, newAvatar)) {
        return Protocol::makeError(id, "DB_ERROR", "更新失败");
    }

    QJsonObject responseData;
    responseData["user_id"] = session.userId;
    responseData["nickname"] = newNickname;
    responseData["avatar_path"] = newAvatar;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// user.recharge — 余额充值
// ============================================================
QJsonObject UserHandler::recharge(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const double amount = data.value("amount").toDouble();
    if (amount <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "充值金额必须大于 0");
    }

    const auto balanceOpt = DbManager::instance().rechargeUser(session.userId, amount);
    if (!balanceOpt.has_value()) {
        return Protocol::makeError(id, "DB_ERROR", "充值失败");
    }

    QJsonObject responseData;
    responseData["balance"] = balanceOpt.value();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// user.admin.list — 管理端用户列表
// ============================================================
QJsonObject UserHandler::adminList(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString phoneKeyword = data.value("phone_keyword").toString().trimmed();
    const QJsonArray items = DbManager::instance().fetchAdminUsers(phoneKeyword);

    QJsonObject responseData;
    responseData["items"] = items;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// user.freeze — 冻结/解冻用户
// ============================================================
QJsonObject UserHandler::freeze(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const int userId = data.value("user_id").toInt();
    if (userId <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 user_id");
    }
    const bool freeze = data.value("freeze").toBool(false);

    // 需求 NO.18/30：充电中被冻结须先停止并完成结算 → 冻结时拦截充电中订单
    if (freeze) {
        const auto openOrder = DbManager::instance().findOpenOrder(userId);
        if (openOrder.has_value()) {
            const QString orderStatus = openOrder.value().value("status").toString();
            if (orderStatus == QStringLiteral("充电中")) {
                return Protocol::makeError(id, "INVALID_PARAM", "用户充电中，请先停止并完成结算");
            }
        }
    }

    if (!DbManager::instance().freezeUser(userId, freeze)) {
        return Protocol::makeError(id, "DB_ERROR", "操作失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId,
        freeze ? QStringLiteral("冻结用户") : QStringLiteral("解冻用户"),
        QStringLiteral("user"),
        QString::number(userId));

    QJsonObject responseData;
    responseData["user_id"] = userId;
    responseData["status"] = freeze ? QStringLiteral("冻结") : QStringLiteral("正常");
    return Protocol::makeSuccess(id, responseData);
}
