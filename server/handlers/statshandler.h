#ifndef STATSHANDLER_H
#define STATSHANDLER_H

#include <QJsonObject>
#include <QString>

/**
 * @brief 数据统计处理器（管理端 KPI 总览 + 图表数据）
 */
class StatsHandler
{
public:
    // 数据总览：KPI 数字 + 营收趋势 + 电桩状态分布 + 站点排名
    static QJsonObject overview(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // STATSHANDLER_H
