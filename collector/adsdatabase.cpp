#include "adsdatabase.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

AdsDatabase &AdsDatabase::instance()
{
    static AdsDatabase database;
    return database;
}

namespace {

bool looksLikeSqliteFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(16).startsWith("SQLite format 3");
}

} // namespace

QString AdsDatabase::resolveDatabasePath() const
{
    // 允许用环境变量显式指定，便于部署与测试
    const QByteArray overridePath = qgetenv("ADS_DB");
    if (!overridePath.isEmpty()) {
        return QString::fromLocal8Bit(overridePath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    // 与 server/dbmanager.cpp 相同思路：优先项目根 db/charge.db
    const QStringList candidates = {
        QDir(QDir::currentPath()).filePath("db/charge.db"),
        QDir(QDir::currentPath()).filePath("../db/charge.db"),
        QDir(appDir).filePath("../db/charge.db"),
        QDir(appDir).filePath("../../db/charge.db"),
    };

    for (const QString &path : candidates) {
        if (!QFile::exists(path)) {
            continue;
        }
        const QString canonical = QDir(path).canonicalPath();
        if (looksLikeSqliteFile(canonical)) {
            return canonical;
        }
        qWarning() << "Skip non-SQLite file:" << canonical;
    }

    return QDir(QDir::currentPath()).filePath("db/charge.db");
}

bool AdsDatabase::open()
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

    QSqlQuery sanity(m_db);
    if (!sanity.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='station'"))
        || !sanity.next()) {
        qCritical() << "Database is not initialized or corrupted:" << m_dbPath;
        if (sanity.lastError().isValid()) {
            qCritical() << sanity.lastError().text();
        }
        qCritical() << "Rebuild with:";
        qCritical() << "  cd db && rm -f charge.db charge.db-wal charge.db-shm";
        qCritical() << "  sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql";
        m_db.close();
        return false;
    }

    // 与业务 server 并发读写同一 charge.db：
    // - busy_timeout 让写冲突时等待而非立即报 database is locked
    // - WAL 降低读写互斥（属性持久化到库文件，server 侧同样受益）
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    if (pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL")) && pragma.next()) {
        qInfo() << "SQLite journal_mode:" << pragma.value(0).toString();
    }
    return true;
}

bool AdsDatabase::isOpen() const
{
    return m_db.isOpen();
}

QString AdsDatabase::databasePath() const
{
    return m_dbPath;
}

QSqlDatabase &AdsDatabase::db()
{
    return m_db;
}
