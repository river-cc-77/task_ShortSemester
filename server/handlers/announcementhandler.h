#ifndef ANNOUNCEMENTHANDLER_H
#define ANNOUNCEMENTHANDLER_H

#include <QJsonObject>
#include <QString>

/**
 * @brief 公告处理器（用户端列表 + 管理端 CRUD）
 */
class AnnouncementHandler
{
public:
    static QJsonObject list(const QString &id, const QString &token, const QJsonObject &data);
    static QJsonObject adminList(const QString &id, const QString &token, const QJsonObject &data);
    static QJsonObject create(const QString &id, const QString &token, const QJsonObject &data);
    static QJsonObject update(const QString &id, const QString &token, const QJsonObject &data);
    static QJsonObject remove(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // ANNOUNCEMENTHANDLER_H
