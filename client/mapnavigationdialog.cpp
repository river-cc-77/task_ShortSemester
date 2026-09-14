#include "mapnavigationdialog.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTextOption>
#include <QAbstractTextDocumentLayout>
#include <QAbstractItemView>
#include <QPainter>
#include <QPen>
#include <QPalette>
#include <QStyleOptionViewItem>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#ifdef CHARGE_USE_WEBENGINE
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

namespace {

QString guessRegionFromAddress(const QString &address)
{
    const int idx = address.indexOf(QChar(0x5E02));
    if (idx <= 0) {
        return QString();
    }
    QString city = address.left(idx);
    if (city.size() > 4) {
        city = city.right(3);
    }
    return city;
}

QWidget *makeCard(QWidget *parent)
{
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("searchCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    return card;
}

QString jsStringLiteral(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral(" "));
    escaped.replace(QLatin1Char('\r'), QStringLiteral(" "));
    return escaped;
}

QString stripHtmlTags(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText().trimmed();
}

QString buildPathJsonFromSteps(const QJsonArray &steps,
                               const QJsonObject &routeOrigin,
                               const QJsonObject &routeDest)
{
    QJsonArray pathArray;
    for (const QJsonValue &value : steps) {
        const QString path = value.toObject().value(QStringLiteral("path")).toString();
        for (const QString &pt : path.split(QLatin1Char(';'))) {
            const QString trimmed = pt.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const QStringList ll = trimmed.split(QLatin1Char(','));
            if (ll.size() < 2) {
                continue;
            }
            QJsonArray point;
            point.append(ll.at(0).trimmed().toDouble());
            point.append(ll.at(1).trimmed().toDouble());
            pathArray.append(point);
        }
    }
    if (pathArray.isEmpty()) {
        QJsonArray o;
        o.append(routeOrigin.value(QStringLiteral("lng")).toDouble());
        o.append(routeOrigin.value(QStringLiteral("lat")).toDouble());
        QJsonArray d;
        d.append(routeDest.value(QStringLiteral("lng")).toDouble());
        d.append(routeDest.value(QStringLiteral("lat")).toDouble());
        pathArray.append(o);
        pathArray.append(d);
    }
    return QString::fromUtf8(QJsonDocument(pathArray).toJson(QJsonDocument::Compact));
}

QString buildBaiduMapShellHtml(const QString &ak)
{
    return QStringLiteral(
               R"(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>html,body,#map{margin:0;padding:0;width:100%;height:100%;background:#edf2f9;}</style>
</head>
<body>
<div id="map"></div>
<script>
window.map = null;
function initBaiduMap() {
  var el = document.getElementById('map');
  if (!el) return;
  window.map = new BMap.Map(el);
  window.map.enableScrollWheelZoom(true);
  window.map.enableDragging(true);
  window.map.enableDoubleClickZoom(true);
  window.map.enableInertialDragging(true);
  window.map.addControl(new BMap.NavigationControl({
    anchor: BMAP_ANCHOR_TOP_LEFT, type: BMAP_NAVIGATION_CONTROL_SMALL
  }));
  window.map.centerAndZoom(new BMap.Point(114.06, 22.54), 12);
  if (window.map.checkResize) window.map.checkResize();
}
</script>
<script src="https://api.map.baidu.com/api?v=3.0&ak=%1&callback=initBaiduMap"></script>
</body></html>)")
        .arg(ak);
}

constexpr int kStepTextLeft = 10;
constexpr int kStepTextRight = 10;
constexpr int kStepTextTop = 8;
constexpr int kStepTextBottom = 10;
constexpr int kStepScrollbarAllow = 8;

class NavStepItemDelegate : public QStyledItemDelegate
{
public:
    explicit NavStepItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        const auto *view = qobject_cast<const QAbstractItemView *>(parent());
        int avail = (view ? view->viewport()->width() : 320)
                    - kStepTextLeft - kStepTextRight - kStepScrollbarAllow;
        avail = qMax(avail, 100);

        QTextDocument doc;
        doc.setDocumentMargin(0);
        QTextOption wrapOpt;
        wrapOpt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc.setDefaultTextOption(wrapOpt);
        doc.setDefaultFont(option.font);
        doc.setPlainText(index.data(Qt::DisplayRole).toString());
        doc.setTextWidth(avail);
        const int textH = static_cast<int>(doc.size().height() + 0.999);
        return QSize(avail + kStepTextLeft + kStepTextRight,
                     textH + kStepTextTop + kStepTextBottom);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        const QRect r = option.rect;

        const QRect textRect = r.adjusted(kStepTextLeft, kStepTextTop,
                                          -kStepTextRight, -kStepTextBottom);
        if (textRect.width() <= 0 || textRect.height() <= 0) {
            painter->restore();
            return;
        }

        QTextDocument doc;
        doc.setDocumentMargin(0);
        QTextOption wrapOpt;
        wrapOpt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc.setDefaultTextOption(wrapOpt);
        doc.setDefaultFont(option.font);
        doc.setPlainText(index.data(Qt::DisplayRole).toString());
        doc.setTextWidth(textRect.width());
        painter->translate(textRect.topLeft());
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette.setColor(QPalette::Text, QColor(QStringLiteral("#25324F")));
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();

        painter->save();
        painter->setPen(QPen(QColor(QStringLiteral("#E6ECF5")), 1));
        painter->drawLine(textRect.left(), r.bottom(), textRect.right(), r.bottom());
        painter->restore();
    }
};

class NavStepsListWidget : public QListWidget
{
public:
    explicit NavStepsListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setWordWrap(true);
        setTextElideMode(Qt::ElideNone);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setItemDelegate(new NavStepItemDelegate(this));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        doItemsLayout();
    }
};

#ifdef CHARGE_USE_WEBENGINE
bool ensureWebEngineProcessPath()
{
    const QByteArray existing = qgetenv("QTWEBENGINEPROCESS_PATH");
    if (!existing.isEmpty()) {
        return QFile::exists(QString::fromUtf8(existing));
    }

    const QStringList candidates = {
        QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/libexec/QtWebEngineProcess"),
        QStringLiteral("/usr/lib/qt6/libexec/QtWebEngineProcess"),
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../libexec/QtWebEngineProcess"),
    };
    for (const QString &path : candidates) {
        const QString abs = QFileInfo(path).absoluteFilePath();
        if (QFile::exists(abs)) {
            qputenv("QTWEBENGINEPROCESS_PATH", abs.toUtf8());
            return true;
        }
    }
    return false;
}
#endif

} // namespace

MapNavigationDialog::MapNavigationDialog(const QString &baiduAk, QWidget *parent)
    : QWidget(parent)
    , m_baiduAk(baiduAk)
{
    setObjectName(QStringLiteral("navDialog"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_net = new QNetworkAccessManager(this);
    m_stack = new QStackedWidget(this);

#ifdef CHARGE_USE_WEBENGINE
    if (ensureWebEngineProcessPath()) {
        m_webEngineReady = true;
        QWebEngineProfile::defaultProfile()->setHttpUserAgent(
            QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                           "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    }
#endif

    // ---------- 常驻地图：下半区无论切到哪个面板，地图都保持显示（需求 3） ----------
    m_mapStack = new QStackedWidget(this);
    m_mapStack->setObjectName(QStringLiteral("navMapStack"));
    m_mapStack->setMinimumHeight(180);   // 原 160；最小窗口 320×568 时仍是可用的地图区

    m_mapLabel = new QLabel(m_mapStack);
    m_mapLabel->setObjectName(QStringLiteral("navMapPreview"));
    m_mapLabel->setAlignment(Qt::AlignCenter);
    m_mapLabel->setScaledContents(true);
    m_mapStack->addWidget(m_mapLabel);

#ifdef CHARGE_USE_WEBENGINE
    if (m_webEngineReady) {
        m_webView = new QWebEngineView(m_mapStack);
        m_webView->setObjectName(QStringLiteral("navWebMap"));
        m_webView->setFocusPolicy(Qt::StrongFocus);
        auto *settings = m_webView->settings();
        settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
        m_mapStack->addWidget(m_webView);
    }
#endif

    // ---------- 面板 1：线路设置（放进滚动区：内容约 340px，窄屏下不能被裁掉） ----------
    m_setupScroll = new QScrollArea(this);
    m_setupScroll->setObjectName(QStringLiteral("navSetupScroll"));
    m_setupScroll->setWidgetResizable(true);
    m_setupScroll->setFrameShape(QFrame::NoFrame);
    m_setupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_setupPage = new QWidget;
    auto *setupLay = new QVBoxLayout(m_setupPage);
    setupLay->setContentsMargins(4, 4, 4, 4);
    setupLay->setSpacing(8);

    auto *routeCard = makeCard(m_setupPage);
    auto *routeLay = new QVBoxLayout(routeCard);
    routeLay->setContentsMargins(12, 10, 12, 10);
    routeLay->setSpacing(8);

    auto *routeTitle = new QLabel(QStringLiteral("路线信息"), routeCard);
    routeTitle->setObjectName(QStringLiteral("sectionTitle"));

    m_originLabel = new QLabel(routeCard);
    m_originLabel->setWordWrap(true);

    m_destLabel = new QLabel(routeCard);
    m_destLabel->setWordWrap(true);
    updateRouteLabels();   // 起终点文案统一由 updateRouteLabels() 生成（未设置时显示提示语）

    routeLay->addWidget(routeTitle);
    routeLay->addWidget(m_originLabel);
    routeLay->addWidget(m_destLabel);
    setupLay->addWidget(routeCard);

    auto *modeCard = makeCard(m_setupPage);
    auto *modeLay = new QVBoxLayout(modeCard);
    modeLay->setContentsMargins(12, 10, 12, 10);
    modeLay->setSpacing(8);

    auto *modeTitle = new QLabel(QStringLiteral("出行方式"), modeCard);
    modeTitle->setObjectName(QStringLiteral("sectionTitle"));

    auto *modeRow = new QHBoxLayout;
    m_drivingRadio = new QRadioButton(QStringLiteral("驾车"), modeCard);
    m_walkingRadio = new QRadioButton(QStringLiteral("步行"), modeCard);
    m_drivingRadio->setChecked(true);
    modeRow->addWidget(m_drivingRadio);
    modeRow->addWidget(m_walkingRadio);
    modeRow->addStretch();

    modeLay->addWidget(modeTitle);
    modeLay->addLayout(modeRow);
    setupLay->addWidget(modeCard);

    m_statusLabel = new QLabel(
        QStringLiteral("请先在「电站选择」页选择充电站并点「导航」。"),
        m_setupPage);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);
    setupLay->addWidget(m_statusLabel);

#ifdef CHARGE_USE_WEBENGINE
    if (m_webEngineReady) {
        setupLay->addWidget(new QLabel(QStringLiteral("将使用应用内地图显示路线。"), m_setupPage));
    } else {
        setupLay->addWidget(new QLabel(
            QStringLiteral("未检测到 Qt WebEngine 运行时，将使用文字指引（可安装 libqt6webenginecore6-bin 启用地图）。"),
            m_setupPage));
    }
#else
    setupLay->addWidget(new QLabel(
        QStringLiteral("编译时未启用 WebEngine，将使用文字指引（需 qt6-webengine-dev 重新编译）。"),
        m_setupPage));
#endif

    setupLay->addStretch();

    m_startBtn = new QPushButton(QStringLiteral("开始导航"), m_setupPage);
    m_startBtn->setProperty("class", "primary");
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumHeight(42);

    // 与「导航中」面板的「返回」（回到本面板）语义不同，这里是退出到电站列表
    auto *backToStationsBtn = new QPushButton(QStringLiteral("返回电站列表"), m_setupPage);
    backToStationsBtn->setCursor(Qt::PointingHandCursor);
    backToStationsBtn->setMinimumHeight(36);

    setupLay->addWidget(m_startBtn);
    setupLay->addWidget(backToStationsBtn);

    connect(m_startBtn, &QPushButton::clicked, this, &MapNavigationDialog::onStartNavigation);
    connect(backToStationsBtn, &QPushButton::clicked, this, &MapNavigationDialog::requestBack);

    m_setupScroll->setWidget(m_setupPage);

    // ---------- 面板 2：导航中（摘要 + 分步指引；地图在上方常驻） ----------
    m_navPage = new QWidget(this);
    auto *navLay = new QVBoxLayout(m_navPage);
    navLay->setContentsMargins(4, 4, 4, 4);
    navLay->setSpacing(8);

    m_summaryLabel = new QLabel(m_navPage);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setObjectName(QStringLiteral("sectionTitle"));
    navLay->addWidget(m_summaryLabel);

    auto *stepsTitle = new QLabel(QStringLiteral("路线指引"), m_navPage);
    stepsTitle->setObjectName(QStringLiteral("sectionTitle"));
    navLay->addWidget(stepsTitle);

    m_stepsList = new NavStepsListWidget(m_navPage);
    m_stepsList->setObjectName(QStringLiteral("navStepsList"));
    m_stepsList->setMinimumHeight(88);
    // 原为 stretch=1（并抢走全部剩余高度，是地图被压到 160px 的直接原因）。
    // 改为限高，把纵向空间让给地图（需求 3）。
    m_stepsList->setMaximumHeight(220);
    navLay->addWidget(m_stepsList, 1);

    auto *backBtn = new QPushButton(QStringLiteral("返回"), m_navPage);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setMinimumHeight(36);
    navLay->addWidget(backBtn);
    connect(backBtn, &QPushButton::clicked, this, &MapNavigationDialog::onBackToSetup);

    m_stack->addWidget(m_setupScroll);
    m_stack->addWidget(m_navPage);

    // ---------- 组装：地图常驻在上，面板在下 ----------
    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(8);
    rootLay->addWidget(m_mapStack, 3);   // 地图吃掉主要高度
    rootLay->addWidget(m_stack, 2);

    m_stack->setCurrentWidget(m_setupScroll);
    loadStaticMap(QJsonArray());   // 进页先显示当前位置地图（需求 3 默认态）
}

MapNavigationDialog::NavMode MapNavigationDialog::selectedMode() const
{
    return m_walkingRadio->isChecked() ? NavMode::Walking : NavMode::Driving;
}

QString MapNavigationDialog::directionLiteUrl(NavMode mode) const
{
    const QString apiPath = mode == NavMode::Walking
                                ? QStringLiteral("walking")
                                : QStringLiteral("driving");
    QUrl url(QStringLiteral("https://api.map.baidu.com/directionlite/v1/") + apiPath);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("origin"),
                       QStringLiteral("%1,%2")
                           .arg(m_originLat, 0, 'f', 6)
                           .arg(m_originLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("destination"),
                       QStringLiteral("%1,%2")
                           .arg(m_destLat, 0, 'f', 6)
                           .arg(m_destLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("coord_type"), QStringLiteral("gcj02"));
    query.addQueryItem(QStringLiteral("ret_coordtype"), QStringLiteral("bd09ll"));
    query.addQueryItem(QStringLiteral("ak"), m_baiduAk);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

// 起终点文案集中在此生成：未设置时给提示语，避免常驻页面里出现「起点：（0.000000, 0.000000）」
void MapNavigationDialog::updateRouteLabels()
{
    if (!m_originLabel || !m_destLabel) {
        return;
    }

    if (m_originDesc.isEmpty() && qFuzzyIsNull(m_originLat) && qFuzzyIsNull(m_originLng)) {
        m_originLabel->setText(QStringLiteral("起点：未设置（请在上方填写当前位置）"));
    } else {
        m_originLabel->setText(QStringLiteral("起点：%1\n（%2, %3）")
                                   .arg(m_originDesc)
                                   .arg(m_originLat, 0, 'f', 6)
                                   .arg(m_originLng, 0, 'f', 6));
    }

    if (m_destName.trimmed().isEmpty()) {
        m_destLabel->setText(QStringLiteral("终点：未选择（请到「电站选择」页点选电站后按「导航」）"));
    } else {
        m_destLabel->setText(QStringLiteral("终点：%1\n（%2, %3）")
                                 .arg(m_destName)
                                 .arg(m_destLat, 0, 'f', 6)
                                 .arg(m_destLng, 0, 'f', 6));
    }
}

// 需求 3：从「电站选择」页点「导航」后，直接在本页开导（不再弹模态窗）
void MapNavigationDialog::startNavigationTo(const QString &originDesc, double originLat, double originLng,
                                            const QString &destName, double destLat, double destLng,
                                            const QString &destAddress)
{
    // 本页是常驻页面：起终点必须每次重写，否则第二次导航还会带着上一次的起点/终点
    m_originDesc = originDesc.isEmpty() ? QStringLiteral("当前位置") : originDesc;
    m_originLat = originLat;
    m_originLng = originLng;
    m_destName = destName;
    m_destLat = destLat;
    m_destLng = destLng;
    m_region = guessRegionFromAddress(destAddress);
    updateRouteLabels();

    m_stack->setCurrentWidget(m_setupScroll);   // 回到设置视图，让「正在规划路线…」可见
    onStartNavigation();                        // 免掉手动再点一次「开始导航」
}

// 需求 3 默认态：进页就显示地图，并标出当前位置
void MapNavigationDialog::focusOnLocation(double lat, double lng, const QString &label)
{
    // 正在显示路线时不要覆盖掉路线图（用户可能只是切回来看一眼）
    if (m_stack->currentWidget() == m_navPage) {
        return;
    }

    m_originLat = lat;
    m_originLng = lng;
    if (!label.trimmed().isEmpty()) {
        m_originDesc = label.trimmed();
    }
    updateRouteLabels();

    m_stack->setCurrentWidget(m_setupScroll);

#ifdef CHARGE_USE_WEBENGINE
    // 和导航时用同一张可拖拽/缩放的地图。原来这里切到静态图（一张 PNG），
    // 初始态既不能缩放也不能拖动，和导航时的地图手感不一致。
    if (m_webView) {
        loadInteractiveLocation(lat, lng, m_originDesc);
        return;
    }
#endif
    m_mapStack->setCurrentWidget(m_mapLabel);   // 无 WebEngine 时的兜底：静态图
    loadStaticMap(QJsonArray());                // 空 steps = 当前位置视图
}

void MapNavigationDialog::onStartNavigation()
{
    // 本页现在常驻，「开始导航」在没有目的地/没有位置时也可能被点到，先给明确提示
    if (m_destName.trimmed().isEmpty() || (qFuzzyIsNull(m_destLat) && qFuzzyIsNull(m_destLng))) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择充电站"));
        return;
    }
    if (qFuzzyIsNull(m_originLat) && qFuzzyIsNull(m_originLng)) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先设置当前位置"));
        return;
    }
    if (m_baiduAk.trimmed().isEmpty()) {
        m_statusLabel->setText(QStringLiteral("未配置百度地图 AK，无法规划路线"));
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在规划路线…"));
    m_startBtn->setEnabled(false);
    fetchRoute();
}

void MapNavigationDialog::fetchRoute()
{
    const QUrl url(directionLiteUrl(selectedMode()));
    QNetworkRequest request(url);
    request.setRawHeader("Referer", "https://lbsyun.baidu.com/");

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_startBtn->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(QStringLiteral("导航加载失败，请检查网络"));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            m_statusLabel->setText(QStringLiteral("导航加载失败，请检查网络"));
            return;
        }

        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("status")).toInt() != 0) {
            m_statusLabel->setText(QStringLiteral("导航加载失败，请检查网络"));
            return;
        }

        const QJsonArray routes = root.value(QStringLiteral("result")).toObject()
                                      .value(QStringLiteral("routes")).toArray();
        if (routes.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("未找到可用路线"));
            return;
        }

        showNavigationResult(root.value(QStringLiteral("result")).toObject());
    });
}

void MapNavigationDialog::showNavigationResult(const QJsonObject &result)
{
    const QJsonArray routes = result.value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("未找到可用路线"));
        return;
    }

    const QJsonObject route = routes.first().toObject();
    const QJsonObject routeOrigin = result.value(QStringLiteral("origin")).toObject();
    const QJsonObject routeDest = result.value(QStringLiteral("destination")).toObject();
    const int distM = route.value(QStringLiteral("distance")).toInt();
    const int durS = route.value(QStringLiteral("duration")).toInt();
    const QJsonArray steps = route.value(QStringLiteral("steps")).toArray();

    const QString modeText = selectedMode() == NavMode::Walking
                                 ? QStringLiteral("步行")
                                 : QStringLiteral("驾车");
    m_summaryLabel->setText(
        QStringLiteral("%1 · 全程约 %2 公里 · 预计 %3 分钟")
            .arg(modeText)
            .arg(distM / 1000.0, 0, 'f', 1)
            .arg(qMax(1, static_cast<int>(qCeil(durS / 60.0)))));

    m_stepsList->clear();
    int idx = 1;
    for (const QJsonValue &value : steps) {
        const QJsonObject step = value.toObject();
        const QString instruction = stripHtmlTags(step.value(QStringLiteral("instruction")).toString());
        const int stepDist = step.value(QStringLiteral("distance")).toInt();
        m_stepsList->addItem(QStringLiteral("%1. %2（%3 米）")
                                 .arg(idx++)
                                 .arg(instruction)
                                 .arg(stepDist));
    }

#ifdef CHARGE_USE_WEBENGINE
    if (m_webView) {
        m_mapStack->setCurrentWidget(m_webView);
        loadInteractiveMap(steps, routeOrigin, routeDest);
    } else
#endif
    {
        m_mapStack->setCurrentWidget(m_mapLabel);
        m_mapLabel->clear();
        m_mapLabel->setText(QStringLiteral("路线地图加载中…"));
        loadStaticMap(steps);
    }

    m_stack->setCurrentWidget(m_navPage);
#ifdef CHARGE_USE_WEBENGINE
    if (m_webView) {
        m_webView->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(450, this, &MapNavigationDialog::applyRouteOnMap);
    }
#endif
}

#ifdef CHARGE_USE_WEBENGINE
void MapNavigationDialog::loadInteractiveMap(const QJsonArray &steps,
                                             const QJsonObject &routeOrigin,
                                             const QJsonObject &routeDest)
{
    if (!m_webView) {
        return;
    }

    m_pendingLocationView = false;   // 本次要显示的是路线
    m_pendingPathJson = buildPathJsonFromSteps(steps, routeOrigin, routeDest);
    m_pendingOriginLng = routeOrigin.value(QStringLiteral("lng")).toDouble();
    m_pendingOriginLat = routeOrigin.value(QStringLiteral("lat")).toDouble();
    m_pendingDestLng = routeDest.value(QStringLiteral("lng")).toDouble();
    m_pendingDestLat = routeDest.value(QStringLiteral("lat")).toDouble();
    m_pendingWalking = selectedMode() == NavMode::Walking;

    qDebug() << "[nav] path json size:" << m_pendingPathJson.size()
             << "preview:" << m_pendingPathJson.left(120);

    ensureMapShellLoaded();
}

// 地图壳（buildBaiduMapShellHtml）只加载一次；调用前必须先把本次要显示的内容
// （路线 or 当前位置）写进 pending 字段，就绪后由 applyPendingMapView() 派发。
void MapNavigationDialog::ensureMapShellLoaded()
{
    if (!m_webView) {
        return;
    }

    if (!m_mapShellReady) {
        connect(m_webView->page(), &QWebEnginePage::loadFinished, this,
                [this](bool ok) {
                    if (!ok) {
                        return;
                    }
                    m_mapShellReady = true;
                    QTimer::singleShot(300, this, &MapNavigationDialog::applyPendingMapView);
                },
                Qt::SingleShotConnection);
        m_webView->setHtml(buildBaiduMapShellHtml(m_baiduAk),
                           QUrl(QStringLiteral("https://lbsyun.baidu.com/")));
    } else {
        QTimer::singleShot(150, this, &MapNavigationDialog::applyPendingMapView);
    }
}

void MapNavigationDialog::applyRouteOnMap()
{
    if (!m_webView || !m_mapShellReady) {
        return;
    }

    const QString js = QStringLiteral(
                           "(function(){"
                           "if(typeof window.map==='undefined'||!window.map)return;"
                           "if(window.map.checkResize)window.map.checkResize();"
                           "var path=%1;"
                           "var pts=(path&&path.length>=2)"
                           "?path.map(function(p){return new BMap.Point(p[0],p[1]);})"
                           ":[new BMap.Point(%2,%3),new BMap.Point(%4,%5)];"
                           "window.map.clearOverlays();"
                           "window.map.addOverlay(new BMap.Polyline(pts,{"
                           "strokeColor:'#2B6BFF',strokeWeight:6,strokeOpacity:0.95}));"
                           "var m1=new BMap.Marker(pts[0]);"
                           "var m2=new BMap.Marker(pts[pts.length-1]);"
                           "window.map.addOverlay(m1);"
                           "window.map.addOverlay(m2);"
                           "m1.setLabel(new BMap.Label('%6',{offset:new BMap.Size(16,-10)}));"
                           "m2.setLabel(new BMap.Label('%7',{offset:new BMap.Size(16,-10)}));"
                           "window.map.setViewport(pts);"
                           "})();")
                       .arg(m_pendingPathJson)
                       .arg(m_pendingOriginLng, 0, 'f', 8)
                       .arg(m_pendingOriginLat, 0, 'f', 8)
                       .arg(m_pendingDestLng, 0, 'f', 8)
                       .arg(m_pendingDestLat, 0, 'f', 8)
                       .arg(jsStringLiteral(m_originDesc), jsStringLiteral(m_destName));

    m_webView->page()->runJavaScript(js);
}

// 当前位置视图：把地图居中到该点并打一个标记，不画路线。
// 地图壳本身已 enableDragging/enableScrollWheelZoom，所以这里不需要额外开放交互。
void MapNavigationDialog::applyLocationOnMap()
{
    if (!m_webView || !m_mapShellReady) {
        return;
    }

    QString labelJs;
    if (!m_pendingLocLabel.trimmed().isEmpty()) {
        labelJs = QStringLiteral(
                      "mk.setLabel(new BMap.Label('%1',{offset:new BMap.Size(16,-10)}));")
                      .arg(jsStringLiteral(m_pendingLocLabel));
    }

    const QString js = QStringLiteral(
                           "(function(){"
                           "if(typeof window.map==='undefined'||!window.map)return;"
                           "if(window.map.checkResize)window.map.checkResize();"
                           "var pt=new BMap.Point(%1,%2);"
                           "window.map.clearOverlays();"
                           "var mk=new BMap.Marker(pt);"
                           "window.map.addOverlay(mk);"
                           "%4"
                           "window.map.centerAndZoom(pt,%3);"
                           "})();")
                       .arg(m_pendingLocLng, 0, 'f', 8)
                       .arg(m_pendingLocLat, 0, 'f', 8)
                       .arg(15)          // 与静态兜底图的 zoom 保持一致
                       .arg(labelJs);

    m_webView->page()->runJavaScript(js);
}

// 地图壳就绪后统一从这里派发：同一张地图要同时服务「当前位置」与「路线」两种视图
void MapNavigationDialog::applyPendingMapView()
{
    if (m_pendingLocationView) {
        applyLocationOnMap();
    } else {
        applyRouteOnMap();
    }
}

void MapNavigationDialog::loadInteractiveLocation(double lat, double lng, const QString &label)
{
    if (!m_webView) {
        return;
    }

    m_pendingLocationView = true;   // 本次要显示的是当前位置
    m_pendingLocLat = lat;
    m_pendingLocLng = lng;
    m_pendingLocLabel = label;

    m_mapStack->setCurrentWidget(m_webView);   // 初始态就用可拖拽/缩放的地图

    ensureMapShellLoaded();
}
#else
void MapNavigationDialog::loadInteractiveMap(const QJsonArray &steps,
                                             const QJsonObject &routeOrigin,
                                             const QJsonObject &routeDest)
{
    Q_UNUSED(steps);
    Q_UNUSED(routeOrigin);
    Q_UNUSED(routeDest);
}

// 无 WebEngine 的构建里没有可交互地图，focusOnLocation 会走静态图兜底
void MapNavigationDialog::loadInteractiveLocation(double lat, double lng, const QString &label)
{
    Q_UNUSED(lat);
    Q_UNUSED(lng);
    Q_UNUSED(label);
}
#endif

// 静态地图兜底渲染（无 WebEngine 时的唯一显示手段）。
// steps 为空 = 当前位置视图：只放一个标记点，不打 paths。
void MapNavigationDialog::loadStaticMap(const QJsonArray &steps)
{
    if (m_baiduAk.trimmed().isEmpty()) {
        m_mapLabel->setText(QStringLiteral("（地图预览不可用）"));
        return;
    }

    const bool locationView = steps.isEmpty();
    if (locationView && qFuzzyIsNull(m_originLat) && qFuzzyIsNull(m_originLng)) {
        m_mapLabel->setText(QStringLiteral("（未设置当前位置）"));
        return;
    }

    // 同一张图不重复请求（每次切回「地图定位」Tab 都会走到这里）
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(m_originLat, 0, 'f', 6)
                            .arg(m_originLng, 0, 'f', 6)
                            .arg(m_destLat, 0, 'f', 6)
                            .arg(m_destLng, 0, 'f', 6)
                            .arg(locationView ? QStringLiteral("loc") : QStringLiteral("route"));
    if (key == m_lastStaticMapKey) {
        return;
    }
    m_lastStaticMapKey = key;

    // 按地图控件实际像素请求（×2 供高分屏），百度静态图上限 1024。
    // 原来写死 360×220 再靠 setScaledContents 拉伸，地图区一大就糊。
    const QSize want = m_mapStack->size().expandedTo(QSize(360, 220)) * 2;

    QUrl url(QStringLiteral("https://api.map.baidu.com/staticimage/v2"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ak"), m_baiduAk);
    query.addQueryItem(QStringLiteral("width"), QString::number(qBound(360, want.width(), 1024)));
    query.addQueryItem(QStringLiteral("height"), QString::number(qBound(220, want.height(), 1024)));
    query.addQueryItem(QStringLiteral("coordtype"), QStringLiteral("bd09ll"));

    if (locationView) {
        const QString pt = QStringLiteral("%1,%2")
                               .arg(m_originLng, 0, 'f', 6)
                               .arg(m_originLat, 0, 'f', 6);
        query.addQueryItem(QStringLiteral("center"), pt);
        query.addQueryItem(QStringLiteral("zoom"), QStringLiteral("15"));
        query.addQueryItem(QStringLiteral("markers"), pt);   // 标出当前位置
    } else {
        QStringList pathPoints;
        for (const QJsonValue &value : steps) {
            const QString path = value.toObject().value(QStringLiteral("path")).toString();
            for (const QString &pt : path.split(QLatin1Char(';'))) {
                const QString trimmed = pt.trimmed();
                if (!trimmed.isEmpty()) {
                    pathPoints << trimmed;
                }
            }
        }
        if (pathPoints.isEmpty()) {
            pathPoints << QStringLiteral("%1,%2").arg(m_originLng, 0, 'f', 6).arg(m_originLat, 0, 'f', 6)
                       << QStringLiteral("%1,%2").arg(m_destLng, 0, 'f', 6).arg(m_destLat, 0, 'f', 6);
        }

        const double centerLng = (m_originLng + m_destLng) / 2.0;
        const double centerLat = (m_originLat + m_destLat) / 2.0;
        query.addQueryItem(QStringLiteral("center"),
                           QStringLiteral("%1,%2").arg(centerLng, 0, 'f', 6).arg(centerLat, 0, 'f', 6));
        query.addQueryItem(QStringLiteral("zoom"), QStringLiteral("13"));
        query.addQueryItem(QStringLiteral("paths"),
                           QStringLiteral("0x2B6BFF,4,1,") + pathPoints.join(QLatin1Char('|')));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Referer", "https://lbsyun.baidu.com/");

    const int seq = ++m_staticMapSeq;
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seq]() {
        reply->deleteLater();
        // 已有更新的请求发出：丢弃这个过期回包，否则旧图会盖掉新图
        if (seq != m_staticMapSeq) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_lastStaticMapKey.clear();   // 失败不缓存标识，下次切页允许重试
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败，请启用 WebEngine 获得更好效果）"));
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll())) {
            m_lastStaticMapKey.clear();
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败，请启用 WebEngine 获得更好效果）"));
            return;
        }
        m_mapLabel->setPixmap(pix);
    });
}

void MapNavigationDialog::onBackToSetup()
{
    m_stack->setCurrentWidget(m_setupScroll);
    m_statusLabel->setText(QStringLiteral("起终点已就绪，选择方式后点击「开始导航」。"));
    // 回到设置视图时把地图恢复为当前位置预览（m_stack 已切走，不会覆盖路线图）
    focusOnLocation(m_originLat, m_originLng, m_originDesc);
}
