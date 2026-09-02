#include "authmanager.h"

#include <QUuid>

AuthManager &AuthManager::instance()
{
    static AuthManager manager;
    return manager;
}

QString AuthManager::createUserToken(int userId)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    SessionInfo session;
    session.role = QStringLiteral("user");
    session.userId = userId;
    m_sessions.insert(token, session);
    return token;
}

QString AuthManager::createAdminToken(int adminId)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    SessionInfo session;
    session.role = QStringLiteral("admin");
    session.adminId = adminId;
    m_sessions.insert(token, session);
    return token;
}

bool AuthManager::validateToken(const QString &token, SessionInfo &session) const
{
    if (!m_sessions.contains(token)) {
        return false;
    }
    session = m_sessions.value(token);
    return true;
}
