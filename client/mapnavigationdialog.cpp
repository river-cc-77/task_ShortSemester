#include "mapnavigationdialog.h"
#include "uiutil.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
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

MapNavigationDialog::MapNavigationDialog(const QString &originDesc,
                                         double originLat, double originLng,
                                         const QString &destName, double destLat, double destLng,
                                         const QString &baiduAk,
                                         const QString &destAddress,
                                         QWidget *parent)
    : QDialog(parent)
    , m_originDesc(originDesc)
    , m_originLat(originLat)
    , m_originLng(originLng)
    , m_destName(destName)
    , m_destLat(destLat)
    , m_destLng(destLng)
    , m_baiduAk(baiduAk)
    , m_region(guessRegionFromAddress(destAddress))
{
    setObjectName(QStringLiteral("navDialog"));
    setWindowTitle(QStringLiteral("地图导航"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 无边框铺满客户端窗口：避免标题栏/边框使内容溢出父窗口
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowModality(Qt::WindowModal);

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

    // ---------- 页 1：起终点 + 出行方式 ----------
    m_setupPage = new QWidget(this);
    auto *setupLay = new QVBoxLayout(m_setupPage);
    setupLay->setContentsMargins(14, 14, 14, 14);
    setupLay->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("地图导航"), m_setupPage);
    titleLabel->setObjectName(QStringLiteral("userGreet"));
    setupLay->addWidget(titleLabel);

    auto *routeCard = makeCard(m_setupPage);
    auto *routeLay = new QVBoxLayout(routeCard);
    routeLay->setContentsMargins(12, 10, 12, 10);
    routeLay->setSpacing(8);

    auto *routeTitle = new QLabel(QStringLiteral("路线信息"), routeCard);
    routeTitle->setObjectName(QStringLiteral("sectionTitle"));

    m_originLabel = new QLabel(routeCard);
    m_originLabel->setWordWrap(true);
    m_originLabel->setText(QStringLiteral("起点：%1\n（%2, %3）")
                               .arg(m_originDesc)
                               .arg(m_originLat, 0, 'f', 6)
                               .arg(m_originLng, 0, 'f', 6));

    m_destLabel = new QLabel(routeCard);
    m_destLabel->setWordWrap(true);
    m_destLabel->setText(QStringLiteral("终点：%1\n（%2, %3）")
                             .arg(m_destName)
                             .arg(m_destLat, 0, 'f', 6)
                             .arg(m_destLng, 0, 'f', 6));

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
        QStringLiteral("起终点已就绪，选择方式后点击「开始导航」。"),
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

    auto *closeBtn = new QPushButton(QStringLiteral("返回"), m_setupPage);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setMinimumHeight(36);

    setupLay->addWidget(m_startBtn);
    setupLay->addWidget(closeBtn);

    connect(m_startBtn, &QPushButton::clicked, this, &MapNavigationDialog::onStartNavigation);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // ---------- 页 2：应用内路线（地图 + 分步指引） ----------
    m_navPage = new QWidget(this);
    auto *navLay = new QVBoxLayout(m_navPage);
    navLay->setContentsMargins(14, 14, 14, 14);
    navLay->setSpacing(8);

    auto *navTitle = new QLabel(QStringLiteral("导航中"), m_navPage);
    navTitle->setObjectName(QStringLiteral("userGreet"));
    navLay->addWidget(navTitle);

    m_summaryLabel = new QLabel(m_navPage);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setObjectName(QStringLiteral("sectionTitle"));
    navLay->addWidget(m_summaryLabel);

    m_mapStack = new QStackedWidget(m_navPage);
    m_mapStack->setObjectName(QStringLiteral("navMapStack"));
    m_mapStack->setMinimumHeight(160);

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

    navLay->addWidget(m_mapStack);

    auto *stepsTitle = new QLabel(QStringLiteral("路线指引"), m_navPage);
    stepsTitle->setObjectName(QStringLiteral("sectionTitle"));
    navLay->addWidget(stepsTitle);

    m_stepsList = new NavStepsListWidget(m_navPage);
    m_stepsList->setObjectName(QStringLiteral("navStepsList"));
    m_stepsList->setMinimumHeight(88);
    navLay->addWidget(m_stepsList, 1);

    auto *backBtn = new QPushButton(QStringLiteral("返回"), m_navPage);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setMinimumHeight(36);
    navLay->addWidget(backBtn);
    connect(backBtn, &QPushButton::clicked, this, &MapNavigationDialog::onBackToSetup);

    m_stack->addWidget(m_setupPage);
    m_stack->addWidget(m_navPage);

    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->addWidget(m_stack);

    refitToParent(460);

    // 跟随父窗口（客户端主窗）变化：父窗 resize/move 时本导航窗实时重新铺满对齐，
    // 使导航窗口尺寸始终与用户端窗口保持同步（而非只取打开瞬间的尺寸）。
    if (QWidget *win = parent) {
        win->installEventFilter(this);
    }
}

void MapNavigationDialog::refitToParent(int)
{
    QWidget *parentWin = parentWidget();
    if (!parentWin) {
        return;
    }
    // 全程铺满客户端窗口：尺寸固定为父窗口客户区、位置锁定在父窗口原点，杜绝任何溢出。
    const QSize full = parentWin->size();
    setMinimumSize(full);
    setMaximumSize(full);
    resize(full);
    move(parentWin->mapToGlobal(QPoint(0, 0)));
}

MapNavigationDialog::~MapNavigationDialog()
{
    if (QWidget *win = parentWidget()) {
        win->removeEventFilter(this);
    }
}

// 父窗口（客户端主窗）尺寸/位置变化时，本导航窗重新铺满对齐，做到与用户端窗口同步。
bool MapNavigationDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget()) {
        const QEvent::Type t = event->type();
        if (t == QEvent::Resize || t == QEvent::Move) {
            // 延迟到父窗口本次 resize/move 事件处理完毕后再重排，避免嵌套改动；
            // 连续拖拽会产生大量 resize/move，用标志位合并为一次重排，防止抖动。
            if (!m_refitQueued) {
                m_refitQueued = true;
                QTimer::singleShot(0, this, [this]() {
                    m_refitQueued = false;
                    refitToParent(0);
                });
            }
        }
    }
    return QDialog::eventFilter(watched, event);
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

void MapNavigationDialog::onStartNavigation()
{
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
    refitToParent(820);
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

    m_pendingPathJson = buildPathJsonFromSteps(steps, routeOrigin, routeDest);
    m_pendingOriginLng = routeOrigin.value(QStringLiteral("lng")).toDouble();
    m_pendingOriginLat = routeOrigin.value(QStringLiteral("lat")).toDouble();
    m_pendingDestLng = routeDest.value(QStringLiteral("lng")).toDouble();
    m_pendingDestLat = routeDest.value(QStringLiteral("lat")).toDouble();
    m_pendingWalking = selectedMode() == NavMode::Walking;

    qDebug() << "[nav] path json size:" << m_pendingPathJson.size()
             << "preview:" << m_pendingPathJson.left(120);

    if (!m_mapShellReady) {
        connect(m_webView->page(), &QWebEnginePage::loadFinished, this,
                [this](bool ok) {
                    if (!ok) {
                        return;
                    }
                    m_mapShellReady = true;
                    QTimer::singleShot(300, this, &MapNavigationDialog::applyRouteOnMap);
                },
                Qt::SingleShotConnection);
        m_webView->setHtml(buildBaiduMapShellHtml(m_baiduAk),
                           QUrl(QStringLiteral("https://lbsyun.baidu.com/")));
    } else {
        QTimer::singleShot(150, this, &MapNavigationDialog::applyRouteOnMap);
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
#else
void MapNavigationDialog::loadInteractiveMap(const QJsonArray &steps,
                                             const QJsonObject &routeOrigin,
                                             const QJsonObject &routeDest)
{
    Q_UNUSED(steps);
    Q_UNUSED(routeOrigin);
    Q_UNUSED(routeDest);
}
#endif

void MapNavigationDialog::loadStaticMap(const QJsonArray &steps)
{
    if (m_baiduAk.trimmed().isEmpty()) {
        m_mapLabel->setText(QStringLiteral("（地图预览不可用）"));
        return;
    }

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
    const QString pathsParam = QStringLiteral("0x2B6BFF,4,1,") + pathPoints.join(QLatin1Char('|'));

    QUrl url(QStringLiteral("https://api.map.baidu.com/staticimage/v2"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ak"), m_baiduAk);
    query.addQueryItem(QStringLiteral("center"),
                       QStringLiteral("%1,%2").arg(centerLng, 0, 'f', 6).arg(centerLat, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("width"), QStringLiteral("360"));
    query.addQueryItem(QStringLiteral("height"), QStringLiteral("220"));
    query.addQueryItem(QStringLiteral("zoom"), QStringLiteral("13"));
    query.addQueryItem(QStringLiteral("paths"), pathsParam);
    query.addQueryItem(QStringLiteral("coordtype"), QStringLiteral("bd09ll"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Referer", "https://lbsyun.baidu.com/");

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败，请启用 WebEngine 获得更好效果）"));
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll())) {
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败，请启用 WebEngine 获得更好效果）"));
            return;
        }
        m_mapLabel->setPixmap(pix);
    });
}

void MapNavigationDialog::onBackToSetup()
{
    m_stack->setCurrentWidget(m_setupPage);
    m_statusLabel->setText(QStringLiteral("起终点已就绪，选择方式后点击「开始导航」。"));
    refitToParent(460);
}
