#ifndef STATIONHANDLER_H
#define STATIONHANDLER_H

#include <QJsonObject>
#include <QString>

class StationHandler
{
public:
    // 用户端：附近充电站列表
    static QJsonObject list(const QString &id, const QString &token, const QJsonObject &data);

    // 用户端：电站详情（含电桩列表）
    static QJsonObject detail(const QString &id, const QString &token, const QJsonObject &data);

    // 管理端：电站列表
    static QJsonObject adminList(const QString &id, const QString &token, const QJsonObject &data);

    // 管理端：新增电站（自动生成电桩）
    static QJsonObject create(const QString &id, const QString &token, const QJsonObject &data);

    // 用户端：收藏电站
    static QJsonObject favoriteAdd(const QString &id, const QString &token, const QJsonObject &data);

    // 用户端：取消收藏
    static QJsonObject favoriteRemove(const QString &id, const QString &token, const QJsonObject &data);

    // 用户端：收藏列表
    static QJsonObject favoriteList(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // STATIONHANDLER_H
