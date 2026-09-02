#include "dbmanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

DbManager &DbManager::instance()
{
    static DbManager manager;
    return manager;
}

QString DbManager::resolveDatabasePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    // 优先使用项目根目录 db/charge.db，避免误用 server/db/charge.db 等副本
    const QStringList candidates = {
        QDir(QDir::currentPath()).filePath("db/charge.db"),
        QDir(QDir::currentPath()).filePath("../db/charge.db"),
        QDir(appDir).filePath("../db/charge.db"),
        QDir(appDir).filePath("../../db/charge.db"),
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return QDir(path).canonicalPath();
        }
    }

    return QDir(QDir::currentPath()).filePath("db/charge.db");
}

bool DbManager::open()
{
    if (m_db.isOpen()) {
        return true;
    }

    m_dbPath = resolveDatabasePath();
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qCritical() << "Failed to open database:" << m_dbPath << m_db.lastError().text();
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA foreign_keys = ON");
    return true;
}

bool DbManager::isOpen() const
{
    return m_db.isOpen();
}

QString DbManager::databasePath() const
{
    return m_dbPath;
}

std::optional<QJsonObject> DbManager::findUserByPhone(const QString &phone)
{
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, phone, nickname, avatar_path, balance, status, created_at "
        "FROM user WHERE phone = :phone");
    query.bindValue(":phone", phone);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    QJsonObject user;
    user["user_id"] = query.value("id").toInt();
    user["phone"] = query.value("phone").toString();
    user["nickname"] = query.value("nickname").toString();
    user["avatar_path"] = query.value("avatar_path").toString();
    user["balance"] = query.value("balance").toDouble();
    user["status"] = query.value("status").toString();
    user["created_at"] = query.value("created_at").toString();
    return user;
}

QJsonObject DbManager::createUser(const QString &phone)
{
    const QString nickname = QStringLiteral("用户") + phone.right(4);

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO user (phone, nickname, avatar_path, balance, status) "
        "VALUES (:phone, :nickname, '', 0, '正常')");
    query.bindValue(":phone", phone);
    query.bindValue(":nickname", nickname);

    if (!query.exec()) {
        qWarning() << "Create user failed:" << query.lastError().text();
        return {};
    }

    return findUserByPhone(phone).value_or(QJsonObject{});
}

std::optional<QJsonObject> DbManager::findAdminByUsername(const QString &username)
{
    QSqlQuery query(m_db);
    // "admin" 加引号，避免个别 SQLite 环境下表名解析异常
    query.prepare(
        "SELECT id, username, password_hash FROM \"admin\" WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "findAdminByUsername SQL error:" << query.lastError().text()
                   << "db:" << m_dbPath;
        return std::nullopt;
    }
    if (!query.next()) {
        qWarning() << "findAdminByUsername: no row for username" << username
                   << "db:" << m_dbPath;
        return std::nullopt;
    }

    QJsonObject admin;
    admin["admin_id"] = query.value("id").toInt();
    admin["username"] = query.value("username").toString();
    admin["password_hash"] = query.value("password_hash").toString();
    return admin;
}
