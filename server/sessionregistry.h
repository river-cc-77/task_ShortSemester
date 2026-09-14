#ifndef SESSIONREGISTRY_H
#define SESSIONREGISTRY_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>

class ClientHandler;

/** 将已登录用户的 TCP 连接登记为 event.push 推送目标。 */
class SessionRegistry
{
public:
    static SessionRegistry &instance();

    void bindUser(int userId, ClientHandler *handler);
    void unbind(ClientHandler *handler);

    void pushToUser(int userId, const QJsonObject &eventData);

private:
    SessionRegistry() = default;

    QHash<int, QList<QPointer<ClientHandler>>> m_userHandlers;
    QHash<ClientHandler *, int> m_handlerUsers;
};

#endif // SESSIONREGISTRY_H
