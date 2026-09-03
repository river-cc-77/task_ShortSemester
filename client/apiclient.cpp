#include "apiclient.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QTcpSocket>

ApiClient::ApiClient(QString host, quint16 port)
    : m_host(std::move(host))
    , m_port(port)
{
}

void ApiClient::setHost(const QString &host)
{
    m_host = host;
}

void ApiClient::setPort(quint16 port)
{
    m_port = port;
}

void ApiClient::setToken(const QString &token)
{
    m_token = token;
}

QString ApiClient::token() const
{
    return m_token;
}

QJsonObject ApiClient::call(const QString &cmd, const QJsonObject &data)
{
    QJsonObject request;
    request["id"] = QString::number(++m_seq);
    request["cmd"] = cmd;
    request["data"] = data;
    if (!m_token.isEmpty()) {
        request["token"] = m_token;
    }

    const QByteArray body = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    const quint32 length = static_cast<quint32>(body.size());
    frame[0] = static_cast<char>((length >> 24) & 0xFF);
    frame[1] = static_cast<char>((length >> 16) & 0xFF);
    frame[2] = static_cast<char>((length >> 8) & 0xFF);
    frame[3] = static_cast<char>(length & 0xFF);
    frame.append(body);

    QTcpSocket socket;
    socket.connectToHost(m_host, m_port);
    if (!socket.waitForConnected(5000)) {
        QJsonObject error;
        error["ok"] = false;
        QJsonObject err;
        err["code"] = QStringLiteral("NETWORK_ERROR");
        err["message"] = QStringLiteral("无法连接服务器，请先启动 charge-server");
        error["error"] = err;
        return error;
    }

    socket.write(frame);
    if (!socket.waitForBytesWritten(5000)) {
        QJsonObject error;
        error["ok"] = false;
        QJsonObject err;
        err["code"] = QStringLiteral("NETWORK_ERROR");
        err["message"] = QStringLiteral("发送请求失败");
        error["error"] = err;
        return error;
    }

    if (!socket.waitForReadyRead(5000)) {
        QJsonObject error;
        error["ok"] = false;
        QJsonObject err;
        err["code"] = QStringLiteral("NETWORK_ERROR");
        err["message"] = QStringLiteral("读取响应超时");
        error["error"] = err;
        return error;
    }

    QByteArray buffer = socket.readAll();
    while (buffer.size() < 4 && socket.waitForReadyRead(2000)) {
        buffer.append(socket.readAll());
    }
    if (buffer.size() < 4) {
        QJsonObject error;
        error["ok"] = false;
        QJsonObject err;
        err["code"] = QStringLiteral("NETWORK_ERROR");
        err["message"] = QStringLiteral("响应格式错误");
        error["error"] = err;
        return error;
    }

    const quint32 respLen =
        (static_cast<quint8>(buffer[0]) << 24) |
        (static_cast<quint8>(buffer[1]) << 16) |
        (static_cast<quint8>(buffer[2]) << 8) |
        static_cast<quint8>(buffer[3]);

    while (static_cast<quint32>(buffer.size()) < 4 + respLen && socket.waitForReadyRead(2000)) {
        buffer.append(socket.readAll());
    }

    const QByteArray respBody = buffer.mid(4, static_cast<int>(respLen));
    const QJsonDocument doc = QJsonDocument::fromJson(respBody);
    if (!doc.isObject()) {
        QJsonObject error;
        error["ok"] = false;
        QJsonObject err;
        err["code"] = QStringLiteral("INVALID_JSON");
        err["message"] = QStringLiteral("响应 JSON 无效");
        error["error"] = err;
        return error;
    }

    return doc.object();
}
