#ifndef CLEAN_H
#define CLEAN_H

#include <QDate>
#include <QString>

class QSqlDatabase;

// 订单数据清洗器（collector 预处理阶段）。
// 职责：把业务表 charge_order 清洗成「每订单一行」的 ads_order_fact 事实台账
//       （去重 / 字段补全 / 异常过滤 / 区域与归属口径），再从事实同步出
//       ads_order_issue 问题台账。只写 ads_*，绝不回写业务表。
// 说明：
//   - 归属时钟 stat_date/stat_hour 取充电真实发生时刻 start_at，
//     缺失回退 end_at -> created_at（回退置 ts_flag=1）。
//   - excluded=1（剔除）的行不进任何对外指标；warn_code（仅标记）仍入指标。
// 调用方（Aggregator）负责把两阶段放进同一事务。
class Cleaner
{
public:
    explicit Cleaner(QSqlDatabase &db);

    // 重建窗口 [from,to] 的事实台账：DELETE 窗口 -> 逐行清洗 INSERT（幂等）
    bool rebuildFactLedger(const QDate &from, const QDate &to);

    // 从 ads_order_fact 全量重建问题台账（DELETE + INSERT，供审计/演示预处理效果）
    bool rebuildIssueLedger();

    // 从电站地址解析区域："深圳市福田区…" -> "福田区"；解析失败返回 "未知"
    static QString regionFromAddress(const QString &address);

private:
    QSqlDatabase &m_db;
};

#endif // CLEAN_H
