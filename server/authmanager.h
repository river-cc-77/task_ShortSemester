#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QHash>
#include <QString>

struct SessionInfo
{
    QString role;
    int userId = 0;
    int adminId = 0;
};

class AuthManager
{
public:
    static AuthManager &instance();

    QString createUserToken(int userId);
    QString createAdminToken(int adminId);
    bool validateToken(const QString &token, SessionInfo &session) const;

private:
    AuthManager() = default;

    QHash<QString, SessionInfo> m_sessions;
};

#endif // AUTHMANAGER_H
