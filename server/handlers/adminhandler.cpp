#include "adminhandler.h"

#include "../authmanager.h"
#include "../dbmanager.h"
#include "../protocol.h"

#include <QCryptographicHash>
#include <QDebug>

namespace {

QJsonObject authAdmin(const QString &id, const QString &token, SessionInfo &session)
{
    if (!AuthManager::instance().validateToken(token, session)) {
        return Protocol::makeError(id, "UNAUTHORIZED", "未登录或 token 无效");
    }
    if (session.role != QStringLiteral("admin")) {
        return Protocol::makeError(id, "FORBIDDEN", "需要管理员登录");
    }
    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }
    return {};
}

} // namespace

QJsonObject AdminHandler::login(const QString &id, const QJsonObject &data)
{
    const QString username = data.value("username").toString().trimmed();
    const QString password = data.value("password").toString().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "账号和密码不能为空");
    }

    if (!DbManager::instance().isOpen()) {
        return Protocol::makeError(id, "DB_ERROR", "数据库未打开");
    }

    const auto adminOpt = DbManager::instance().findAdminByUsername(username);
    if (!adminOpt.has_value()) {
        return Protocol::makeError(id, "UNAUTHORIZED", "账号或密码错误");
    }

    const QJsonObject admin = adminOpt.value();
    const QByteArray hash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    const QString passwordHash = QString::fromLatin1(hash.toHex()).trimmed().toLower();
    const QString storedHash = admin.value("password_hash").toString().trimmed().toLower();

    if (passwordHash != storedHash) {
        qWarning() << "admin.login hash mismatch for" << username
                   << "expected len:" << storedHash.size()
                   << "got len:" << passwordHash.size();
        return Protocol::makeError(id, "UNAUTHORIZED", "账号或密码错误");
    }

    const int adminId = admin.value("admin_id").toInt();
    const QString token = AuthManager::instance().createAdminToken(adminId);

    QJsonObject responseData;
    responseData["token"] = token;
    responseData["admin_id"] = adminId;
    responseData["username"] = admin.value("username").toString();
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.list — 电桩列表
// ============================================================
QJsonObject AdminHandler::pileList(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const int stationId = data.value("station_id").toInt(0);
    const QString status = data.value("status").toString().trimmed();
    const QString keyword = data.value("keyword").toString().trimmed();

    QJsonObject responseData;
    responseData["items"] = DbManager::instance().fetchPiles(stationId, status, keyword);
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.restart — 远程重启电桩
// ============================================================
QJsonObject AdminHandler::pileRestart(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString pileNo = data.value("pile_no").toString().trimmed();
    if (pileNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 pile_no");
    }

    const auto pileOpt = DbManager::instance().findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "电桩不存在");
    }

    // 使用中的电桩（预约/在用）不可重启；仅有他人待支付历史订单时不阻止
    const QString pileStatus = pileOpt.value().value("status").toString();
    if (pileStatus == QStringLiteral("预约") || pileStatus == QStringLiteral("在用")) {
        return Protocol::makeError(id, "INVALID_PARAM", "该电桩使用中，无法重启");
    }
    if (DbManager::instance().pileHasActiveOrders(pileNo)) {
        return Protocol::makeError(id, "INVALID_PARAM", "该电桩使用中，无法重启");
    }

    if (!DbManager::instance().restartPile(pileNo)) {
        return Protocol::makeError(id, "DB_ERROR", "重启失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("远程重启电桩"),
        QStringLiteral("pile"), pileNo);

    QJsonObject responseData;
    responseData["pile_no"] = pileNo;
    responseData["status"] = QStringLiteral("闲置");
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.update — 修改电桩（类型/功率/状态），协议 5.2 P2
// 不可改编号、所属电站
// ============================================================
QJsonObject AdminHandler::pileUpdate(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString pileNo = data.value("pile_no").toString().trimmed();
    if (pileNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 pile_no");
    }

    const auto pileOpt = DbManager::instance().findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "电桩不存在");
    }

    const QString type = data.value("type").toString().trimmed();          // 快充/慢充
    const double powerKw = data.value("power_kw").toDouble(-1);            // kW > 0
    const QString status = data.value("status").toString().trimmed();      // 闲置/预约/在用/故障

    if (!type.isEmpty() && type != QStringLiteral("快充") && type != QStringLiteral("慢充")) {
        return Protocol::makeError(id, "INVALID_PARAM", "类型只能为快充或慢充");
    }
    if (powerKw != -1 && powerKw <= 0) {
        return Protocol::makeError(id, "INVALID_PARAM", "功率必须大于 0");
    }
    if (!status.isEmpty() && status != QStringLiteral("闲置")
            && status != QStringLiteral("预约") && status != QStringLiteral("在用")
            && status != QStringLiteral("故障")) {
        return Protocol::makeError(id, "INVALID_PARAM", "电桩状态不合法");
    }
    if (type.isEmpty() && powerKw == -1 && status.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "至少提供一个要修改的字段");
    }
    if (!status.isEmpty() && DbManager::instance().pileHasActiveOrders(pileNo)) {
        return Protocol::makeError(id, "INVALID_PARAM", "该电桩使用中，无法修改状态");
    }

    if (!DbManager::instance().updatePile(pileNo, type, powerKw, status)) {
        return Protocol::makeError(id, "DB_ERROR", "更新电桩失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("修改电桩"),
        QStringLiteral("pile"), pileNo,
        QString("type=%1 power=%2 status=%3").arg(type).arg(powerKw).arg(status));

    QJsonObject responseData;
    responseData["pile_no"] = pileNo;
    return Protocol::makeSuccess(id, responseData);
}

// ============================================================
// pile.delete — 删除电桩（协议 5.2 P2）
// 预约/在用 或有未完成订单的桩不可删除
// ============================================================
QJsonObject AdminHandler::pileDelete(const QString &id, const QString &token, const QJsonObject &data)
{
    SessionInfo session;
    const QJsonObject auth = authAdmin(id, token, session);
    if (!auth.isEmpty()) return auth;

    const QString pileNo = data.value("pile_no").toString().trimmed();
    if (pileNo.isEmpty()) {
        return Protocol::makeError(id, "INVALID_PARAM", "缺少 pile_no");
    }

    const auto pileOpt = DbManager::instance().findPileByNo(pileNo);
    if (!pileOpt.has_value()) {
        return Protocol::makeError(id, "NOT_FOUND", "电桩不存在");
    }
    const QJsonObject pile = pileOpt.value();
    const QString pileStatus = pile.value("status").toString();
    if (pileStatus == QStringLiteral("预约") || pileStatus == QStringLiteral("在用")) {
        return Protocol::makeError(id, "INVALID_PARAM", "该电桩使用中，无法删除");
    }

    // 检查是否存在未完成订单（预约/充电中/待支付）
    if (DbManager::instance().pileHasOpenOrders(pileNo)) {
        return Protocol::makeError(id, "INVALID_PARAM", "该电桩使用中，无法删除");
    }

    if (!DbManager::instance().deletePile(pileNo)) {
        return Protocol::makeError(id, "DB_ERROR", "删除电桩失败");
    }

    // 写操作日志
    DbManager::instance().writeOperationLog(
        session.adminId, QStringLiteral("删除电桩"),
        QStringLiteral("pile"), pileNo);

    QJsonObject responseData;
    responseData["pile_no"] = pileNo;
    return Protocol::makeSuccess(id, responseData);
}
