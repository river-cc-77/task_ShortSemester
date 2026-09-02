#ifndef USERHANDLER_H
#define USERHANDLER_H

#include <QJsonObject>
#include <QString>

class UserHandler
{
public:
    static QJsonObject login(const QString &id, const QJsonObject &data);
};

#endif // USERHANDLER_H
