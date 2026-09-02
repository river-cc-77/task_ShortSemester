#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

#include <QByteArray>
#include <QObject>

class QTcpSocket;

class ClientHandler : public QObject
{
    Q_OBJECT

public:
    explicit ClientHandler(QTcpSocket *socket, QObject *parent = nullptr);
    ~ClientHandler() override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processBuffer();

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
};

#endif // CLIENTHANDLER_H
