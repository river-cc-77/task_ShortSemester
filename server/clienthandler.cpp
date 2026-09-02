#include "clienthandler.h"

#include "protocol.h"

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
    if (m_socket != nullptr) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void ClientHandler::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void ClientHandler::onDisconnected()
{
    qInfo() << "Client disconnected";
    deleteLater();
}

void ClientHandler::processBuffer()
{
    while (true) {
        QJsonObject request;
        if (!Protocol::tryDecodeFrame(m_buffer, request)) {
            break;
        }

        if (request.value("ok").toBool() == false &&
            request.value("error").toObject().value("code").toString() == "INVALID_JSON") {
            const QByteArray frame = Protocol::encodeFrame(request);
            m_socket->write(frame);
            continue;
        }

        const QString cmd = request.value("cmd").toString();
        qInfo() << "Request:" << cmd;

        const QJsonObject response = Protocol::handleRequest(request);
        const QByteArray frame = Protocol::encodeFrame(response);
        m_socket->write(frame);
    }
}
