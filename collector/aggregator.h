#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include <QDate>
#include <QObject>

class QTimer;

// 大屏数据定时聚合器（Qt6 / C++ 控制台进程，配合 main.cpp 使用）。
//   - 启动后立即执行一次「近 windowDays 天」的全窗口回填聚合；
//   - 之后每隔 intervalSeconds 秒重复全窗口重算 + 电桩状态快照（幂等）。
// 遵守约定：只读业务表（charge_order / user / pile / station），只写分析表（ads_*），
//           单事务内完成，避免大屏直查业务大表压库。
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
    bool insertStatusSnapshot();

    QTimer *m_timer = nullptr;
    int m_intervalSeconds;
    int m_windowDays;
};

#endif // AGGREGATOR_H
