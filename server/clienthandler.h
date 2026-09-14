#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

class QTcpSocket;

class ClientHandler : public QObject
{
    Q_OBJECT

public:
    explicit ClientHandler(QTcpSocket *socket, QObject *parent = nullptr);
    ~ClientHandler() override;

    void sendEvent(const QJsonObject &eventData);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processBuffer();
    void bindSessionFromRequest(const QJsonObject &request);

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
};

#endif // CLIENTHANDLER_H
