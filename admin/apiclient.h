#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QString>

class ApiClient
{
public:
    explicit ApiClient(QString host = QStringLiteral("127.0.0.1"), quint16 port = 9000);

    void setHost(const QString &host);
    void setPort(quint16 port);
    void setToken(const QString &token);
    QString token() const;

    QJsonObject call(const QString &cmd, const QJsonObject &data = {});

private:
    QString m_host;
    quint16 m_port = 9000;
    QString m_token;
    int m_seq = 0;
};

#endif // APICLIENT_H
