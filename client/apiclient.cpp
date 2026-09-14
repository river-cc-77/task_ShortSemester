#include "apiclient.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpSocket>

namespace {

QJsonObject makeNetworkError(const QString &code, const QString &message)
{
    QJsonObject error;
    error["ok"] = false;
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;
    error["error"] = err;
    return error;
}

bool waitForData(QTcpSocket &socket, int timeoutMs)
{
    if (socket.bytesAvailable() > 0) {
        return true;
    }
    return socket.waitForReadyRead(timeoutMs);
}

} // namespace

ApiClient::ApiClient(QString host, quint16 port)
    : m_host(std::move(host))
    , m_port(port)
{
}

ApiClient::~ApiClient()
{
    endPersistentSession();
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

bool ApiClient::tryDecodeFrame(QByteArray &buffer, QJsonObject &payload)
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
        return false;
    }

    payload = doc.object();
    return true;
}

QByteArray ApiClient::encodeFrame(const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    const quint32 length = static_cast<quint32>(body.size());
    frame[0] = static_cast<char>((length >> 24) & 0xFF);
    frame[1] = static_cast<char>((length >> 16) & 0xFF);
    frame[2] = static_cast<char>((length >> 8) & 0xFF);
    frame[3] = static_cast<char>(length & 0xFF);
    frame.append(body);
    return frame;
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

    QTcpSocket socket;
    socket.connectToHost(m_host, m_port);
    if (!socket.waitForConnected(5000)) {
        return makeNetworkError(QStringLiteral("NETWORK_ERROR"),
                                QStringLiteral("无法连接服务器，请先启动 charge-server"));
    }

    socket.write(encodeFrame(request));
    if (!socket.waitForBytesWritten(5000)) {
        return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("发送请求失败"));
    }

    QByteArray buffer;
    const int deadlineMs = 8000;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    while (QDateTime::currentMSecsSinceEpoch() - started < deadlineMs) {
        if (!waitForData(socket, 2000)) {
            break;
        }
        buffer.append(socket.readAll());
        QJsonObject response;
        if (tryDecodeFrame(buffer, response)) {
            return response;
        }
    }

    return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("读取响应超时"));
}

bool ApiClient::beginPersistentSession()
{
    endPersistentSession();
    m_persistentSocket = new QTcpSocket();
    m_persistentSocket->connectToHost(m_host, m_port);
    if (!m_persistentSocket->waitForConnected(5000)) {
        endPersistentSession();
        return false;
    }
    m_persistentBuffer.clear();
    return true;
}

void ApiClient::endPersistentSession()
{
    if (m_persistentSocket != nullptr) {
        m_persistentSocket->disconnectFromHost();
        if (m_persistentSocket->state() != QAbstractSocket::UnconnectedState) {
            m_persistentSocket->waitForDisconnected(1000);
        }
        delete m_persistentSocket;
        m_persistentSocket = nullptr;
    }
    m_persistentBuffer.clear();
}

bool ApiClient::hasPersistentSession() const
{
    return m_persistentSocket != nullptr
           && m_persistentSocket->state() == QAbstractSocket::ConnectedState;
}

void ApiClient::setEventHandler(EventHandler handler)
{
    m_eventHandler = std::move(handler);
}

void ApiClient::pollIncomingEvents()
{
    if (!hasPersistentSession()) {
        return;
    }

    m_persistentBuffer.append(m_persistentSocket->readAll());
    QJsonObject message;
    while (tryDecodeFrame(m_persistentBuffer, message)) {
        if (message.value(QStringLiteral("cmd")).toString() == QStringLiteral("event.push")
            && m_eventHandler) {
            m_eventHandler(message.value(QStringLiteral("data")).toObject());
        }
    }
}

QJsonObject ApiClient::readNextMessage(const QString &expectId)
{
    if (!hasPersistentSession()) {
        return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("长连接未建立"));
    }

    const int deadlineMs = 10000;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    while (QDateTime::currentMSecsSinceEpoch() - started < deadlineMs) {
        QJsonObject message;
        while (tryDecodeFrame(m_persistentBuffer, message)) {
            if (message.value(QStringLiteral("cmd")).toString() == QStringLiteral("event.push")) {
                if (m_eventHandler) {
                    m_eventHandler(message.value(QStringLiteral("data")).toObject());
                }
                continue;
            }
            if (expectId.isEmpty() || message.value(QStringLiteral("id")).toString() == expectId) {
                return message;
            }
        }

        if (!waitForData(*m_persistentSocket, 500)) {
            continue;
        }
        m_persistentBuffer.append(m_persistentSocket->readAll());
    }

    return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("读取响应超时"));
}

QJsonObject ApiClient::callPersistent(const QString &cmd, const QJsonObject &data)
{
    if (!hasPersistentSession()) {
        return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("长连接未建立"));
    }

    QJsonObject request;
    const QString reqId = QString::number(++m_seq);
    request["id"] = reqId;
    request["cmd"] = cmd;
    request["data"] = data;
    if (!m_token.isEmpty()) {
        request["token"] = m_token;
    }

    m_persistentSocket->write(encodeFrame(request));
    if (!m_persistentSocket->waitForBytesWritten(5000)) {
        return makeNetworkError(QStringLiteral("NETWORK_ERROR"), QStringLiteral("发送请求失败"));
    }

    return readNextMessage(reqId);
}
