#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include "clean.h"

#include <QDate>
#include <QObject>
#include <QPair>
#include <optional>

class QSqlDatabase;
class QTimer;

// 大屏数据定时聚合器（Qt6 / C++ 控制台进程，配合 main.cpp 使用）。
//   - 启动后立即执行一次「近 windowDays 天」的全窗口回填聚合；
//   - 之后每隔 intervalSeconds 秒重复全窗口重算（窗口内汇总幂等、结果一致）；
//     电桩状态快照为时序累积（每轮按 站×状态 追加多条，仅保留回填窗口，同刻幂等）。
// 遵守约定：只读业务表（charge_order / user / pile / station），只写分析表（ads_*），
//           单事务内完成，避免大屏直查业务大表压库。
//
// 流水线（单事务）：
//   1. Cleaner 清洗出订单事实台账 ads_order_fact + 问题台账 ads_order_issue（去重/补全/过滤）
//   2. 各订单族指标统一读事实表聚合（平台日/站日/桩日/小时/区域）
//   3. 追加电桩状态快照，回填 繁忙率/故障率/峰值小时 等派生指标
class Aggregator : public QObject
{
    Q_OBJECT

public:
    explicit Aggregator(int intervalSeconds = 60, int windowDays = 30, QObject *parent = nullptr);

    void start();   // 立即聚合一次，随后启动周期定时器

private slots:
    void onTick();

private:
    bool aggregate();
    bool recomputePlatformDaily(const QDate &from, const QDate &to);
    bool recomputeStationDaily(const QDate &from, const QDate &to);
    bool recomputePileDaily(const QDate &from, const QDate &to);
    bool recomputeHourly(const QDate &from, const QDate &to);   // ads_hourly_stats + ads_station_hourly
    bool recomputeRegionDaily(const QDate &from, const QDate &to);
    bool insertStatusSnapshot(const QDate &from);  // 采集现态快照，仅保留回填窗口、同刻幂等
    bool recomputeSnapshotDerived(const QDate &from, const QDate &to);  // busy/fault + peak_hour 回填

    // 快照口径 时间加权 [busy_ratio, fault_ratio]；stationId=0 表示全平台；当日无快照返回空
    static std::optional<QPair<double, double>> timeWeightedRatio(QSqlDatabase &db,
                                                                  const QDate &day,
                                                                  int stationId);

    QTimer *m_timer = nullptr;
    Cleaner m_cleaner;
    int m_intervalSeconds;
    int m_windowDays;
};

#endif // AGGREGATOR_H
