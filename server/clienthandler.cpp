#include "clienthandler.h"

#include "authmanager.h"
#include "protocol.h"
#include "sessionregistry.h"

#include <QDebug>
#include <QTcpSocket>

ClientHandler::ClientHandler(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientHandler::onDisconnected);
}

ClientHandler::~ClientHandler()
{
    SessionRegistry::instance().unbind(this);
    if (m_socket != nullptr) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void ClientHandler::sendEvent(const QJsonObject &eventData)
{
    if (m_socket == nullptr || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    const QByteArray frame = Protocol::encodeFrame(Protocol::makePush(eventData));
    m_socket->write(frame);
}

void ClientHandler::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void ClientHandler::onDisconnected()
{
    SessionRegistry::instance().unbind(this);
    qInfo() << "Client disconnected";
    deleteLater();
}

void ClientHandler::bindSessionFromRequest(const QJsonObject &request)
{
    const QString token = request.value(QStringLiteral("token")).toString();
    if (token.isEmpty()) {
        return;
    }

    SessionInfo session;
    if (!AuthManager::instance().validateToken(token, session)) {
        return;
    }
    if (session.role == QStringLiteral("user") && session.userId > 0) {
        SessionRegistry::instance().bindUser(session.userId, this);
    }
}

void ClientHandler::processBuffer()
{
    while (true) {
        QJsonObject request;
        if (!Protocol::tryDecodeFrame(m_buffer, request)) {
            break;
        }

        if (request.value(QStringLiteral("ok")).toBool() == false &&
            request.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString()
                == QStringLiteral("INVALID_JSON")) {
            const QByteArray frame = Protocol::encodeFrame(request);
            m_socket->write(frame);
            continue;
        }

        bindSessionFromRequest(request);

        const QString cmd = request.value(QStringLiteral("cmd")).toString();
        qInfo() << "Request:" << cmd;

        const QJsonObject response = Protocol::handleRequest(request);
        const QByteArray frame = Protocol::encodeFrame(response);
        m_socket->write(frame);
    }
}
