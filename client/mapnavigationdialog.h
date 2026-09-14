#ifndef MAPNAVIGATIONDIALOG_H
#define MAPNAVIGATIONDIALOG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QStackedWidget;
class QNetworkAccessManager;

#ifdef CHARGE_USE_WEBENGINE
class QWebEngineView;
#endif

// 地图定位页面。
//
// 说明：类名/文件名沿用历史上的 MapNavigationDialog（原为一个铺满父窗的模态 QDialog）。
// 现在它是用户端「地图定位」标签页内的常驻 QWidget 页面：地图始终可见，起终点由
// startNavigationTo() 在每次导航时写入。重命名留作独立的后续提交，以免把
// 「改名」和「改行为」混在同一次变更里。
class MapNavigationDialog : public QWidget
{
    Q_OBJECT
public:
    enum class NavMode { Driving, Walking };

    // 常驻页面：构造时只需 AK；起终点不再由构造函数固定，避免第二次导航仍用旧起点
    explicit MapNavigationDialog(const QString &baiduAk, QWidget *parent = nullptr);

    // 需求 3：从电站点「导航」直接在本页开导（不再弹模态窗）
    void startNavigationTo(const QString &originDesc, double originLat, double originLng,
                           const QString &destName, double destLat, double destLng,
                           const QString &destAddress = QString());

    // 需求 3（默认态）：进页就显示地图，并标出当前位置
    void focusOnLocation(double lat, double lng, const QString &label);

signals:
    void requestBack();   // 页内「返回」→ 由 MainWindow 切回电站选择页

private slots:
    void onStartNavigation();
    void onBackToSetup();
#ifdef CHARGE_USE_WEBENGINE
    void applyRouteOnMap();
    void applyLocationOnMap();     // 当前位置视图：只标点居中，不画路线
    void applyPendingMapView();    // 地图壳就绪后，按本次意图派发给上面两个之一
#endif

private:
    QString directionLiteUrl(NavMode mode) const;
    NavMode selectedMode() const;
    void fetchRoute();
    void showNavigationResult(const QJsonObject &result);
    void updateRouteLabels();               // 起终点标签刷新（多处状态变化后调用）
    void loadStaticMap(const QJsonArray &steps);   // steps 为空 = 当前位置视图（不打 paths，只放标记点）
    void loadInteractiveMap(const QJsonArray &steps,
                            const QJsonObject &routeOrigin,
                            const QJsonObject &routeDest);
    // 初始「地图定位」也要和导航时一样可拖拽/缩放，所以当前位置视图同样走 WebEngine 地图
    void loadInteractiveLocation(double lat, double lng, const QString &label);
#ifdef CHARGE_USE_WEBENGINE
    void ensureMapShellLoaded();   // 地图壳只加载一次，就绪后由 applyPendingMapView() 派发
#endif

    QString m_originDesc;
    double m_originLat = 0;
    double m_originLng = 0;
    QString m_destName;
    double m_destLat = 0;
    double m_destLng = 0;
    QString m_baiduAk;
    QString m_region;

    QStackedWidget *m_stack = nullptr;      // 下半区：线路设置 / 导航中 两个面板
    QScrollArea *m_setupScroll = nullptr;   // 线路设置面板内容较高，窄屏下可滚动，避免按钮被裁掉
    QWidget *m_setupPage = nullptr;
    QWidget *m_navPage = nullptr;
    QStackedWidget *m_mapStack = nullptr;   // 常驻地图（不随 m_stack 切换而消失）

    QLabel *m_originLabel = nullptr;
    QLabel *m_destLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_mapLabel = nullptr;
    QRadioButton *m_drivingRadio = nullptr;
    QRadioButton *m_walkingRadio = nullptr;
    QPushButton *m_startBtn = nullptr;
    QListWidget *m_stepsList = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    int m_staticMapSeq = 0;   // 静态图请求序号：丢弃过期回包（快速连点时旧图可能后到）
    QString m_lastStaticMapKey;   // 上次已请求的静态图标识，避免每次切页重复请求同一张图

#ifdef CHARGE_USE_WEBENGINE
    QWebEngineView *m_webView = nullptr;
    bool m_webEngineReady = false;
    bool m_mapShellReady = false;
    QString m_pendingPathJson;
    double m_pendingOriginLng = 0;
    double m_pendingOriginLat = 0;
    double m_pendingDestLng = 0;
    double m_pendingDestLat = 0;
    bool m_pendingWalking = false;
    // 同一张地图要服务两种视图，故记下「本次要显示什么」，壳加载完再按它派发
    bool m_pendingLocationView = false;
    double m_pendingLocLng = 0;
    double m_pendingLocLat = 0;
    QString m_pendingLocLabel;
#endif
};

#endif // MAPNAVIGATIONDIALOG_H
