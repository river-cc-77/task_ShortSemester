#include "orderhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

namespace {

// 鉴权辅助：验证用户 token，失败返回错误响应，成功返回空对象
QJsonObject authUser(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("user")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要用户端登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return {};
}

// 鉴权辅助：验证用户或管理员 token
QJsonObject authUserOrAdmin(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return {};
}

// 计算充电费用：kwh = power_kw × elapsed_seconds / 3600, amount = round(kwh × price, 2)
void calcCharge(const QJsonObject &order, double &kwh, double &amount,
                qint64 &elapsedSeconds)
{
    const QString startAtStr = order.value("start_at").toString();
    const QDateTime startAt = QDateTime::fromString(startAtStr, "yyyy-MM-dd HH:mm:ss");
    const QDateTime now = QDateTime::currentDateTime();
    elapsedSeconds = startAt.isValid() ? startAt.secsTo(now) : 0;
    if (elapsedSeconds < 0) elapsedSeconds = 0;

    const double powerKw = order.value("power_kw").toDouble();
    const double price = order.value("price").toDouble();
    kwh = powerKw * elapsedSeconds / 3600.0;
    amount = std::round(kwh * price * 100.0) / 100.0;
}

} // namespace

static QJsonObject settleCore(const QString &id, const SessionInfo &session, const QJsonObject &data)
{
    const QString orderNo = data.value("order_no").toString().trimmed();
    if (orderNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 order_no");
    }

    const auto orderOpt = DbManager::instance().findOrderByNo(orderNo);
    if (!orderOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "订单不存在");
    }
    const QJsonObject order = orderOpt.value();

    if (session.role == QStringLiteral("user") && order.value("user_id").toInt() != session.userId) {
        return Protocol::makeError(id, "FORBIDDEN", "无权结算此订单");
    }
    if (order.value("status").toString() == QStringLiteral("已完成")) {
        return Protocol::makeError(id, "INVALID_PARAM", "订单已结算");
    }
    if (order.value("status").toString() != QStringLiteral("待支付")) {
        return Protocol::makeError(id, "INVALID_PARAM", "订单状态不允许结算");
    }

    const int userId = order.value("user_id").toInt();
    const int adminId = session.role == QStringLiteral("admin") ? session.adminId : 0;
    const auto balanceOpt = DbManager::instance().settleOrder(orderNo, userId, adminId);
    if (!balanceOpt.has_value()) {
        const auto userOpt = DbManager::instance().findUserById(userId);
        if (userOpt.has_value()
            && userOpt.value().value("balance").toDouble() >= order.value("amount").toDouble()) {
            return Protocol::makeError(id, "DB_ERROR", "结算失败");
        }
        return Protocol::makeError(id, "BALANCE_NOT_ENOUGH", "余额不足，请先充值");
    }

    QJsonObject responseData;
    responseData["order_no"] = orderNo;
    responseData["status"] = QStringLiteral("已完成");
    responseData["balance_after"] = balanceOpt.value();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// order.check_open — 检查未完成订单
// ============================================================
QJsonObject OrderHandler::checkOpen(const QString &id, const QString &token, const QJsonObject &data)
{
    Q_UNUSED(data);
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    // 先清理超时预约（预约超 3 小时未开始充电自动取消）
    DbManager::instance().cancelExpiredReservations();

    const auto orderOpt = DbManager::instance().findOpenOrder(session.userId);

    QJsonObject responseData;
    if (orderOpt.has_value()) {
        const QJsonObject order = orderOpt.value();
        responseData["has_open"] = true;
        QJsonObject orderInfo;
        orderInfo["order_no"] = order.value("order_no").toString();
        orderInfo["status"] = order.value("status").toString();
        orderInfo["station_name"] = order.value("station_name").toString();
        orderInfo["pile_no"] = order.value("pile_no").toString();
        orderInfo["kwh"] = order.value("kwh").toDouble();
        orderInfo["amount"] = order.value("amount").toDouble();
        responseData["order"] = orderInfo;
    } else {
        responseData["has_open"] = false;
        responseData["order"] = QJsonValue::Null;
    }

    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// order.list — 订单列表
// ============================================================
QJsonObject OrderHandler::list(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUserOrAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString status = data.value("status").toString().trimmed();
    const int limit = data.value("limit").toInt(20);
    const QString phone = data.value("phone").toString().trimmed();
    const QString dateFrom = data.value("date_from").toString().trimmed();
    const QString dateTo = data.value("date_to").toString().trimmed();

    // 用户端只查自己的订单，管理端可查全部
    const int userId = (session.role == QStringLiteral("user")) ? session.userId : 0;

    const QJsonArray items = DbManager::instance().fetchOrders(
        userId, status, limit, phone, dateFrom, dateTo);

    QJsonObject responseData;
    responseData["items"] = items;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// charge.reserve — 预约电桩
// ============================================================
QJsonObject OrderHandler::reserve(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString pileNo = data.value("pile_no").toString().trimmed();
    if (pileNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 pile_no");
    }

    // 先清理超时预约（预约超 3 小时未开始充电自动取消，桩恢复闲置）
    DbManager::instance().cancelExpiredReservations();

    // 1. 检查是否有未完成订单
    if (DbManager::instance().findOpenOrder(session.userId).has_value()) {
        return Protocol::makeError(id, "ORDER_EXISTS", "您有未完成的充电订单，请先结算");
    }

    // 2. 检查电桩状态
    const auto pileOpt = DbManager::instance().findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "电桩不存在");
    }
    const QJsonObject pile = pileOpt.value();
    const QString pileStatus = pile.value("status").toString();
    if (pileStatus == QStringLiteral("故障")) {
        return Protocol::makeError(id, "PILE_FAULT", "电桩故障，无法预约");
    }
    if (pileStatus != QStringLiteral("闲置")) {
        return Protocol::makeError(id, "PILE_BUSY", "电桩不可用");
    }

    // 3. 创建订单并更新电桩状态（事务）
    const auto orderNoOpt = DbManager::instance().reservePile(
        session.userId, pile.value("station_id").toInt(), pile.value("id").toInt());
    if (!orderNoOpt.has_value()) {
        const auto pileRecheck = DbManager::instance().findPileByNo(pileNo);
        if (pileRecheck.has_value()
            && pileRecheck.value().value("status").toString() != QStringLiteral("闲置")) {
            return Protocol::makeError(id, "PILE_BUSY", "电桩不可用");
        }
        return Protocol::makeError(id, "DB_ERROR", "创建订单失败");
    }

    QJsonObject responseData;
    responseData["order_no"] = orderNoOpt.value();
    responseData["status"] = QStringLiteral("预约");
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// charge.start — 开始充电
// ============================================================
QJsonObject OrderHandler::start(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString orderNo = data.value("order_no").toString().trimmed();
    if (orderNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 order_no");
    }

    const auto orderOpt = DbManager::instance().findOrderByNo(orderNo);
    if (!orderOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "订单不存在");
    }
    const QJsonObject order = orderOpt.value();

    // 验证订单属于当前用户
    if (order.value("user_id").toInt() != session.userId) {
        return Protocol::makeError(id, "FORBIDDEN", "无权操作此订单");
    }
    if (order.value("status").toString() != QStringLiteral("预约")) {
        return Protocol::makeError(id, "INVALID_PARAM", "订单状态不允许开始充电");
    }

    // 更新订单与电桩状态（事务）
    const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    if (!DbManager::instance().startCharge(orderNo, order.value("pile_id").toInt(), now)) {
        return Protocol::makeError(id, "DB_ERROR", "开始充电失败");
    }

    QJsonObject responseData;
    responseData["order_no"] = orderNo;
    responseData["status"] = QStringLiteral("充电中");
    responseData["start_at"] = now;
    responseData["price"] = order.value("price").toDouble();
    responseData["power_kw"] = order.value("power_kw").toDouble();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// charge.progress — 查询充电进度
// ============================================================
QJsonObject OrderHandler::progress(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString orderNo = data.value("order_no").toString().trimmed();
    if (orderNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 order_no");
    }

    const auto orderOpt = DbManager::instance().findOrderByNo(orderNo);
    if (!orderOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "订单不存在");
    }
    const QJsonObject order = orderOpt.value();

    if (order.value("user_id").toInt() != session.userId) {
        return Protocol::makeError(id, "FORBIDDEN", "无权查看此订单");
    }

    double kwh = 0.0;
    double amount = 0.0;
    qint64 elapsedSeconds = 0;

    if (order.value("status").toString() == QStringLiteral("充电中")) {
        calcCharge(order, kwh, amount, elapsedSeconds);
    } else {
        kwh = order.value("kwh").toDouble();
        amount = order.value("amount").toDouble();
    }

    // 估算剩余时间（假设充到 50 度，演示用）
    const qint64 estimatedRemain = 0; // 简化：不估算

    QJsonObject responseData;
    responseData["order_no"] = orderNo;
    responseData["status"] = order.value("status").toString();
    responseData["kwh"] = kwh;
    responseData["amount"] = amount;
    responseData["elapsed_seconds"] = static_cast<qint64>(elapsedSeconds);
    responseData["estimated_remain_seconds"] = estimatedRemain;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// charge.stop — 停止充电
// ============================================================
QJsonObject OrderHandler::stop(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUser(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString orderNo = data.value("order_no").toString().trimmed();
    if (orderNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 order_no");
    }

    const auto orderOpt = DbManager::instance().findOrderByNo(orderNo);
    if (!orderOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "订单不存在");
    }
    const QJsonObject order = orderOpt.value();

    if (order.value("user_id").toInt() != session.userId) {
        return Protocol::makeError(id, "FORBIDDEN", "无权操作此订单");
    }
    if (order.value("status").toString() != QStringLiteral("充电中")) {
        return Protocol::makeError(id, "INVALID_PARAM", "订单不在充电中");
    }

    // 计算最终费用
    double kwh = 0.0;
    double amount = 0.0;
    qint64 elapsedSeconds = 0;
    calcCharge(order, kwh, amount, elapsedSeconds);

    // 更新订单与电桩状态（事务）
    const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    if (!DbManager::instance().stopCharge(orderNo, order.value("pile_id").toInt(), now, kwh, amount)) {
        return Protocol::makeError(id, "DB_ERROR", "停止充电失败");
    }

    QJsonObject responseData;
    responseData["order_no"] = orderNo;
    responseData["status"] = QStringLiteral("待支付");
    responseData["kwh"] = kwh;
    responseData["amount"] = amount;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// charge.settle — 结算订单
// ============================================================
QJsonObject OrderHandler::settle(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authUserOrAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;
    return settleCore(id, session, data);
}

// ============================================================
// order.admin.settle — 管理员代结算（协议 4.18，P1）
// 只允许管理员调用；结算逻辑复用 charge.settle（其中 admin 分支已写操作日志）
// ============================================================
QJsonObject OrderHandler::adminSettle(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("admin")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要管理员登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return settleCore(id, session, data);
}
