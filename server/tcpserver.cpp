#include "tcpserver.h"

#include "clienthandler.h"

#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>

TcpServer::TcpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &TcpServer::onNewConnection);
}

bool TcpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical() << "Listen failed:" << m_server->errorString();
        return false;
    }
    qInfo() << "Server listening on port" << port;
    return true;
}

void TcpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        qInfo() << "Client connected from" << socket->peerAddress().toString();
        new ClientHandler(socket, this);
    }
}
