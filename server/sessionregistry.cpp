#include "sessionregistry.h"

#include "clienthandler.h"

#include <QDebug>

SessionRegistry &SessionRegistry::instance()
{
    static SessionRegistry registry;
    return registry;
}

void SessionRegistry::bindUser(int userId, ClientHandler *handler)
{
    if (userId <= 0 || handler == nullptr) {
        return;
    }

    unbind(handler);

    auto &list = m_userHandlers[userId];
    for (const QPointer<ClientHandler> &existing : list) {
        if (existing.data() == handler) {
            m_handlerUsers.insert(handler, userId);
            return;
        }
    }
    list.append(handler);
    m_handlerUsers.insert(handler, userId);
}

void SessionRegistry::unbind(ClientHandler *handler)
{
    if (handler == nullptr) {
        return;
    }

    const auto it = m_handlerUsers.find(handler);
    if (it == m_handlerUsers.end()) {
        return;
    }

    const int userId = it.value();
    m_handlerUsers.erase(it);

    const auto listIt = m_userHandlers.find(userId);
    if (listIt == m_userHandlers.end()) {
        return;
    }

    QList<QPointer<ClientHandler>> &list = listIt.value();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).data() == handler || list.at(i).isNull()) {
            list.removeAt(i);
        }
    }
    if (list.isEmpty()) {
        m_userHandlers.erase(listIt);
    }
}

void SessionRegistry::pushToUser(int userId, const QJsonObject &eventData)
{
    const auto it = m_userHandlers.find(userId);
    if (it == m_userHandlers.end()) {
        return;
    }

    for (int i = it.value().size() - 1; i >= 0; --i) {
        ClientHandler *handler = it.value().at(i).data();
        if (handler == nullptr) {
            it.value().removeAt(i);
            continue;
        }
        handler->sendEvent(eventData);
    }
    if (it.value().isEmpty()) {
        m_userHandlers.erase(it);
    }
}
