#ifndef ADMINHANDLER_H
#define ADMINHANDLER_H

#include <QJsonObject>
#include <QString>

class AdminHandler
{
public:
    // 管理员登录
    static QJsonObject login(const QString &id, const QJsonObject &data);

    // 电桩列表
    static QJsonObject pileList(const QString &id, const QString &token, const QJsonObject &data);

    // 远程重启电桩（故障 → 闲置）
    static QJsonObject pileRestart(const QString &id, const QString &token, const QJsonObject &data);

    // 修改电桩（类型/功率/状态，协议 5.2 P2）
    static QJsonObject pileUpdate(const QString &id, const QString &token, const QJsonObject &data);

    // 删除电桩（协议 5.2 P2，使用中禁止删除）
    static QJsonObject pileDelete(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // ADMINHANDLER_H
