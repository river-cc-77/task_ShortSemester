#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QString>
#include <functional>

class QTcpSocket;

class ApiClient
{
public:
    using EventHandler = std::function<void(const QJsonObject &)>;

    explicit ApiClient(QString host = QStringLiteral("127.0.0.1"), quint16 port = 9000);
    ~ApiClient();

    void setHost(const QString &host);
    void setPort(quint16 port);
    void setToken(const QString &token);
    QString token() const;

    QJsonObject call(const QString &cmd, const QJsonObject &data = {});

    bool beginPersistentSession();
    void endPersistentSession();
    bool hasPersistentSession() const;
    void setEventHandler(EventHandler handler);
    void pollIncomingEvents();
    QJsonObject callPersistent(const QString &cmd, const QJsonObject &data = {});

private:
    QJsonObject readNextMessage(const QString &expectId = QString());
    static bool tryDecodeFrame(QByteArray &buffer, QJsonObject &payload);
    static QByteArray encodeFrame(const QJsonObject &payload);

    QString m_host;
    quint16 m_port = 9000;
    QString m_token;
    int m_seq = 0;
    QTcpSocket *m_persistentSocket = nullptr;
    QByteArray m_persistentBuffer;
    EventHandler m_eventHandler;
};

#endif // APICLIENT_H
