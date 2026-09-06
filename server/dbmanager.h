#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <functional>
#include <optional>

class DbManager
{
public:
    static DbManager &instance();

    bool open();
    bool isOpen() const;
    QString databasePath() const;

    // ===== 用户 =====
    std::optional<QJsonObject> findUserByPhone(const QString &phone);
    std::optional<QJsonObject> findUserById(int userId);
    QJsonObject createUser(const QString &phone);
    bool updateUserProfile(int userId, const QString &nickname, const QString &avatarPath);
    std::optional<double> rechargeUser(int userId, double amount);
    QJsonArray fetchAdminUsers(const QString &phoneKeyword);
    bool freezeUser(int userId, bool freeze);

    // ===== 管理员 =====
    std::optional<QJsonObject> findAdminByUsername(const QString &username);

    // ===== 充电站 =====
    QJsonArray fetchStations(const QString &keyword);
    std::optional<QJsonObject> fetchStationDetail(int stationId);
    QJsonArray fetchAdminStations();
    bool stationNameExists(const QString &name);
    int createStation(const QString &name, const QString &address,
                      double lat, double lng, double price,
                      int fastCount, int slowCount);

    // ===== 电桩 =====
    std::optional<QJsonObject> findPileByNo(const QString &pileNo);
    bool updatePileStatus(int pileId, const QString &status);
    QJsonArray fetchPiles(int stationId, const QString &status, const QString &keyword);
    bool restartPile(const QString &pileNo);
    bool updatePile(const QString &pileNo, const QString &type,
                    double powerKw, const QString &status);
    bool deletePile(const QString &pileNo);
    bool pileHasOpenOrders(const QString &pileNo);

    // ===== 订单 =====
    std::optional<QJsonObject> findOpenOrder(int userId);
    std::optional<QJsonObject> findOrderByNo(const QString &orderNo);
    QString createOrder(int userId, int stationId, int pileId);
    bool updateOrderStatus(const QString &orderNo, const QString &status,
                           const QString &startAt = QString(),
                           const QString &endAt = QString(),
                           double kwh = -1, double amount = -1);
    QJsonArray fetchOrders(int userId, const QString &status, int limit,
                           const QString &phone = QString(),
                           const QString &dateFrom = QString(),
                           const QString &dateTo = QString());
    std::optional<double> settleOrder(const QString &orderNo, int userId, int adminId = 0);
    std::optional<QString> reservePile(int userId, int stationId, int pileId);
    bool startCharge(const QString &orderNo, int pileId, const QString &startAt);
    bool stopCharge(const QString &orderNo, int pileId, const QString &endAt,
                    double kwh, double amount);
    void cancelExpiredReservations();

    // ===== 统计 =====
    QJsonObject fetchStatsOverview(int days);

    // ===== 负荷预测 =====
    QJsonArray fetchForecasts(const QString &horizon, int stationId);

    // ===== 收藏 =====
    bool addFavorite(int userId, int stationId);
    bool removeFavorite(int userId, int stationId);
    QJsonArray listFavorites(int userId);

    // ===== 公告 =====
    QJsonArray fetchAnnouncements();

    // ===== 日志 =====
    bool writeOperationLog(int adminId, const QString &action,
                           const QString &targetType = QString(),
                           const QString &targetId = QString(),
                           const QString &detail = QString());
    bool writeWalletLog(int userId, double delta, const QString &reason, int orderId = 0);

private:
    DbManager() = default;
    bool runInTransaction(const std::function<bool()> &fn);
    QString resolveDatabasePath() const;

    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // DBMANAGER_H
