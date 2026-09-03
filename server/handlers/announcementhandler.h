#ifndef ANNOUNCEMENTHANDLER_H
#define ANNOUNCEMENTHANDLER_H

#include <QJsonObject>
#include <QString>

/**
 * @brief 公告处理器（用户端首页公告）
 */
class AnnouncementHandler
{
public:
    // 公告列表
    static QJsonObject list(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // ANNOUNCEMENTHANDLER_H
