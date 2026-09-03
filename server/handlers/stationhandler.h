#ifndef STATIONHANDLER_H
#define STATIONHANDLER_H

#include <QJsonObject>
#include <QString>

class StationHandler
{
public:
    static QJsonObject list(const QString &id, const QString &token, const QJsonObject &data);
    static QJsonObject detail(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // STATIONHANDLER_H
