#include "adsdatabase.h"
#include "aggregator.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 可选参数：./ads-collector [intervalSeconds=60] [windowDays=30]
    int intervalSeconds = 60;
    int windowDays = 30;
    if (argc >= 2) {
        const int v = QString::fromLocal8Bit(argv[1]).toInt();
        if (v > 0) {
            intervalSeconds = v;
        }
    }
    if (argc >= 3) {
        const int v = QString::fromLocal8Bit(argv[2]).toInt();
        if (v > 0) {
            windowDays = v;
        }
    }

    if (!AdsDatabase::instance().open()) {
        qCritical() << "Cannot open database. Ensure db/charge.db exists:";
        qCritical() << "  cd db && sqlite3 charge.db < schema.sql && sqlite3 charge.db < seed.sql";
        return 1;
    }

    qInfo() << "Database:" << AdsDatabase::instance().databasePath();
    qInfo() << "Interval:" << intervalSeconds << "s, backfill window:" << windowDays << "days";

    Aggregator aggregator(intervalSeconds, windowDays);
    aggregator.start();   // 先全量回填一次，再定时刷新

    return app.exec();
}
