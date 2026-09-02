#include "dbmanager.h"
#include "tcpserver.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (!DbManager::instance().open()) {
        qCritical() << "Cannot open database. Run from project root and ensure db/charge.db exists:";
        qCritical() << "  cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql";
        return 1;
    }

    qInfo() << "Database:" << DbManager::instance().databasePath();

    TcpServer server;
    if (!server.start(9000)) {
        return 1;
    }

    return app.exec();
}
