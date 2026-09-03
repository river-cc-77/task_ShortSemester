#ifndef USERHANDLER_H
#define USERHANDLER_H

#include <QJsonObject>
#include <QString>

class UserHandler
{
public:
    // 用户登录（手机号免密）
    static QJsonObject login(const QString &id, const QJsonObject &data);

    // 修改个人资料（昵称/头像）
    static QJsonObject profileUpdate(const QString &id, const QString &token, const QJsonObject &data);

    // 余额充值
    static QJsonObject recharge(const QString &id, const QString &token, const QJsonObject &data);

    // 管理端：用户列表
    static QJsonObject adminList(const QString &id, const QString &token, const QJsonObject &data);

    // 管理端：冻结/解冻用户
    static QJsonObject freeze(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // USERHANDLER_H
