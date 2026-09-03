#include "protocol.h"

#include "handlers/adminhandler.h"
#include "handlers/pinghandler.h"
#include "handlers/stationhandler.h"
#include "handlers/userhandler.h"

#include <QJsonDocument>
#include <QJsonParseError>

QByteArray Protocol::encodeFrame(const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.resize(4);
    const quint32 length = static_cast<quint32>(body.size());
    frame[0] = static_cast<char>((length >> 24) & 0xFF);
    frame[1] = static_cast<char>((length >> 16) & 0xFF);
    frame[2] = static_cast<char>((length >> 8) & 0xFF);
    frame[3] = static_cast<char>(length & 0xFF);
    frame.append(body);
    return frame;
}

bool Protocol::tryDecodeFrame(QByteArray &buffer, QJsonObject &payload)
{
    if (buffer.size() < 4) {
        return false;
    }

    const quint32 length =
        (static_cast<quint8>(buffer[0]) << 24) |
        (static_cast<quint8>(buffer[1]) << 16) |
        (static_cast<quint8>(buffer[2]) << 8) |
        static_cast<quint8>(buffer[3]);

    if (length == 0 || length > 65536) {
        buffer.clear();
        return false;
    }

    if (static_cast<quint32>(buffer.size()) < 4 + length) {
        return false;
    }

    const QByteArray body = buffer.mid(4, static_cast<int>(length));
    buffer.remove(0, 4 + static_cast<int>(length));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        payload = makeError("", "INVALID_JSON", "JSON 解析失败");
        return true;
    }

    payload = doc.object();
    return true;
}

QJsonObject Protocol::makeSuccess(const QString &id, const QJsonObject &data)
{
    QJsonObject response;
    response["id"] = id;
    response["ok"] = true;
    response["data"] = data;
    return response;
}

QJsonObject Protocol::makeError(const QString &id, const QString &code, const QString &message)
{
    QJsonObject response;
    response["id"] = id;
    response["ok"] = false;

    QJsonObject error;
    error["code"] = code;
    error["message"] = message;
    response["error"] = error;
    return response;
}

QJsonObject Protocol::handleRequest(const QJsonObject &request)
{
    const QString id = request.value("id").toString();
    const QString cmd = request.value("cmd").toString();
    const QString token = request.value("token").toString();
    const QJsonObject data = request.value("data").toObject();

    if (cmd == "ping") {
        return PingHandler::handle(id);
    }
    if (cmd == "user.login") {
        return UserHandler::login(id, data);
    }
    if (cmd == "admin.login") {
        return AdminHandler::login(id, data);
    }
    if (cmd == "station.list") {
        return StationHandler::list(id, token, data);
    }
    if (cmd == "station.detail") {
        return StationHandler::detail(id, token, data);
    }

    return makeError(id, "UNKNOWN_CMD", "未知命令: " + cmd);
}
