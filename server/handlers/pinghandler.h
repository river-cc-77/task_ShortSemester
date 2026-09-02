#ifndef PINGHANDLER_H
#define PINGHANDLER_H

#include <QJsonObject>
#include <QString>

class PingHandler
{
public:
    static QJsonObject handle(const QString &id);
};

#endif // PINGHANDLER_H
