#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonObject>
#include <QMainWindow>

class ApiClient;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QListWidgetItem;
class QComboBox;
class QNetworkAccessManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ApiClient *api, const QJsonObject &user, QWidget *parent = nullptr);

private slots:
    void onRefreshStations();
    void onStationItemClicked(QListWidgetItem *item); // 点击列表项触发, 要使充电站电桩详情弹窗弹出
    void onRegionChanged(int index);   // 下拉选区域
    void onGeocodeAddress();  // 地址地理编码
    void onProfileCenter();   // 个人中心
    void onOrderHistory();   // 订单历史
    void onFavoriteList();   // 收藏列表
protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void loadStations();
    void showStationDetail(int stationId); // 弹出站点详情对话框
    QJsonObject geocodeByBaidu(const QString &address);// 调用百度地图地理编码 API，返回 {lat, lng}，失败返回空对象
    bool checkOpenOrder(bool failClosed = false);   // 检查未完成订单，有则弹窗提示并返回 true
    void showChargingProgress(const QString &orderNo);   // 充电中页面
    void showSettleDialog(const QString &orderNo, double kwh, double amount);  // 结算页面

    ApiClient *m_api = nullptr;
    QJsonObject m_user;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_latEdit = nullptr;
    QLineEdit *m_lngEdit = nullptr;
    QListWidget *m_stationList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QComboBox *m_regionCombo = nullptr;
    QLineEdit *m_addressEdit = nullptr;
    QPushButton *m_geocodeButton = nullptr;
    QNetworkAccessManager *m_netMgr = nullptr;
    QString m_baiduAk = QStringLiteral(""); // 百度地图 AK，没有就留空（留空时只能用下拉预设区域）
    QPushButton *m_profileButton = nullptr;
    QPushButton *m_orderButton = nullptr;
    QPushButton *m_favoriteButton = nullptr;
};

#endif // MAINWINDOW_H
