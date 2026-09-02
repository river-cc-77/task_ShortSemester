#ifndef ADMINHANDLER_H
#define ADMINHANDLER_H

#include <QJsonObject>
#include <QString>

class AdminHandler
{
public:
    static QJsonObject login(const QString &id, const QJsonObject &data);
};

#endif // ADMINHANDLER_H
