#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>

class Protocol
{
public:
    static QByteArray encodeFrame(const QJsonObject &payload);
    static bool tryDecodeFrame(QByteArray &buffer, QJsonObject &payload);

    static QJsonObject makeSuccess(const QString &id, const QJsonObject &data);
    static QJsonObject makeError(const QString &id, const QString &code, const QString &message);

    static QJsonObject handleRequest(const QJsonObject &request);
};

#endif // PROTOCOL_H
