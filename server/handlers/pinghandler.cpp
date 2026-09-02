#include "pinghandler.h"

#include "../protocol.h"

#include <QDateTime>

QJsonObject PingHandler::handle(const QString &id)
{
    QJsonObject data;
    data["pong"] = true;
    data["server_time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    return Protocol::makeSuccess(id, data);
}
