#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>

class QTcpServer;

class TcpServer : public QObject
{
    Q_OBJECT

public:
    explicit TcpServer(QObject *parent = nullptr);
    bool start(quint16 port);

private slots:
    void onNewConnection();

private:
    QTcpServer *m_server = nullptr;
};

#endif // TCPSERVER_H
