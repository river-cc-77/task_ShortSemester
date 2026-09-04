#ifndef ADSDATABASE_H
#define ADSDATABASE_H

#include <QSqlDatabase>
#include <QString>

// 采集器数据库封装。
// 只读业务表（charge_order/user/pile/station），只写分析表（ads_*），
// 避免大屏直查业务大表压库。
class AdsDatabase
{
public:
    static AdsDatabase &instance();

    bool open();                 // 定位并打开 db/charge.db，开启 WAL + busy_timeout
    bool isOpen() const;
    QString databasePath() const;
    QSqlDatabase db() const;

private:
    AdsDatabase() = default;
    QString resolveDatabasePath() const;  // 支持环境变量 ADS_DB 覆盖

    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // ADSDATABASE_H
