#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonObject>
#include <QMainWindow>

class ApiClient;
class MapNavigationDialog;
class QButtonGroup;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QListWidgetItem;
class QComboBox;
class QNetworkAccessManager;
class QStackedWidget;

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
    void onAnnouncementList(); // 公告列表
    void switchPage(int index);   // 底部导航切页（导航跳转也复用本函数）
private:
    // 页面下标：与底部导航按钮的 id 一一对应
    enum PageIndex { kPageStations = 0, kPageMap = 1, kPageProfile = 2 };

    QWidget *buildTopBar();        // 顶部欢迎/余额栏（三页共用一份）
    QWidget *buildStationPage();   // 页0 电站选择
    QWidget *buildMapPage();       // 页1 地图定位
    QWidget *buildProfilePage();   // 页2 个人中心
    QWidget *buildBottomNav();     // 底部三 Tab
    void refreshLocationChip();    // 刷新页0 的只读位置摘要
    void startNavigationOnMapPage(double destLat, double destLng, const QString &destName,
                                  const QString &destAddress = QString());
    void loadStations();
    void showStationDetail(int stationId); // 弹出站点详情对话框
    QJsonObject geocodeByBaidu(const QString &address);// 调用百度地图地理编码 API，返回 {lat, lng}，失败返回空对象
    bool refreshUserProfile();   // 从服务端同步用户资料（含余额）
    void updateUserHeaderLabel(); // 刷新顶部欢迎/余额栏
    QJsonObject fetchOrderByNo(const QString &orderNo); // 从服务端拉取最新订单快照
    bool checkOpenOrder(bool failClosed = false);   // 检查未完成订单，有则弹窗提示并返回 true
    bool cancelReservation(const QString &orderNo); // 用户取消预约
    void showChargingProgress(const QString &orderNo);   // 充电中页面
    void showSettleDialog(const QString &orderNo, double kwh, double amount);  // 结算页面

    ApiClient *m_api = nullptr;
    QJsonObject m_user;
    QStackedWidget *m_pageStack = nullptr;
    QButtonGroup *m_tabGroup = nullptr;
    QPushButton *m_tabStations = nullptr;
    QPushButton *m_tabMap = nullptr;
    QPushButton *m_tabProfile = nullptr;
    MapNavigationDialog *m_mapPage = nullptr;   // 页1 的地图/导航（常驻页面）
    QPushButton *m_locationChip = nullptr;      // 页0 的位置摘要（点击进地图页）
    QLabel *m_profileSummaryLabel = nullptr;    // 页2 账户概览
    QLabel *m_avatarLabel = nullptr;            // 页2 头像（进页即显示，不必先点「编辑资料」）
    // 站点详情弹窗里点「导航」时先把目的地记下，等 exec() 返回再切页
    // （模态事件循环还在时切页会被模态窗盖住）
    bool m_pendingNav = false;
    QString m_pendingDestName;
    double m_pendingDestLat = 0;
    double m_pendingDestLng = 0;
    QString m_pendingDestAddress;
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
    QString m_baiduAk = QStringLiteral("pMd3Q5PqSmoVkn8UvYjbJy28GzeqM7hl"); // 百度地图 AK（留空时只能用下拉预设区域）
    QPushButton *m_profileButton = nullptr;
    QPushButton *m_orderButton = nullptr;
    QPushButton *m_favoriteButton = nullptr;
    QPushButton *m_announcementButton = nullptr;
};

#endif // MAINWINDOW_H
