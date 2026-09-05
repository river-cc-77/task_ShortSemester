#ifndef ORDERHANDLER_H
#define ORDERHANDLER_H

#include <QJsonObject>
#include <QString>

/**
 * @brief 订单与充电流程处理器
 *
 * 负责充电全流程：预约 → 开始 → 进度查询 → 停止 → 结算
 * 以及订单查询（order.check_open / order.list）。
 *
 * 状态机：
 *   电桩: 闲置 ──预约──► 预约 ──开始──► 在用 ──停止──► 闲置
 *   订单: 预约 ──开始──► 充电中 ──停止──► 待支付 ──结算──► 已完成
 */
class OrderHandler
{
public:
    // 检查是否有未完成订单（进入充电页前调用）
    static QJsonObject checkOpen(const QString &id, const QString &token, const QJsonObject &data);

    // 订单列表（用户端/管理端通用）
    static QJsonObject list(const QString &id, const QString &token, const QJsonObject &data);

    // 预约电桩
    static QJsonObject reserve(const QString &id, const QString &token, const QJsonObject &data);

    // 开始充电
    static QJsonObject start(const QString &id, const QString &token, const QJsonObject &data);

    // 查询充电进度（轮询）
    static QJsonObject progress(const QString &id, const QString &token, const QJsonObject &data);

    // 停止充电
    static QJsonObject stop(const QString &id, const QString &token, const QJsonObject &data);

    // 结算订单
    static QJsonObject settle(const QString &id, const QString &token, const QJsonObject &data);

    // 管理员代结算（order.admin.settle，协议 4.18）
    static QJsonObject adminSettle(const QString &id, const QString &token, const QJsonObject &data);
};

#endif // ORDERHANDLER_H
