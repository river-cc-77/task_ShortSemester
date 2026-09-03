#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <optional>

class DbManager
{
public:
    static DbManager &instance();

    bool open();
    bool isOpen() const;
    QString databasePath() const;

    std::optional<QJsonObject> findUserByPhone(const QString &phone);
    QJsonObject createUser(const QString &phone);
    std::optional<QJsonObject> findAdminByUsername(const QString &username);

    QJsonArray fetchStations(const QString &keyword);
    std::optional<QJsonObject> fetchStationDetail(int stationId);

private:
    DbManager() = default;
    QString resolveDatabasePath() const;

    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // DBMANAGER_H
