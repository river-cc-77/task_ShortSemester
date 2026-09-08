#include "mapnavigationdialog.h"
#include "uiutil.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

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

    m_net = new QNetworkAccessManager(this);
    m_stack = new QStackedWidget(this);

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

    m_mapLabel = new QLabel(m_navPage);
    m_mapLabel->setObjectName(QStringLiteral("navMapPreview"));
    m_mapLabel->setAlignment(Qt::AlignCenter);
    m_mapLabel->setMinimumHeight(160);
    m_mapLabel->setScaledContents(true);
    navLay->addWidget(m_mapLabel);

    auto *stepsTitle = new QLabel(QStringLiteral("路线指引"), m_navPage);
    stepsTitle->setObjectName(QStringLiteral("sectionTitle"));
    navLay->addWidget(stepsTitle);

    m_stepsList = new QListWidget(m_navPage);
    m_stepsList->setObjectName(QStringLiteral("navStepsList"));
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

    fitDialogInParent(this, parent, 460);
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

        showNavigationResult(routes.first().toObject());
    });
}

void MapNavigationDialog::showNavigationResult(const QJsonObject &route)
{
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
        const QString instruction = step.value(QStringLiteral("instruction")).toString();
        const int stepDist = step.value(QStringLiteral("distance")).toInt();
        m_stepsList->addItem(QStringLiteral("%1. %2（%3 米）")
                                 .arg(idx++)
                                 .arg(instruction)
                                 .arg(stepDist));
    }

    m_mapLabel->clear();
    m_mapLabel->setText(QStringLiteral("路线地图加载中…"));
    loadStaticMap(steps);

    m_stack->setCurrentWidget(m_navPage);
    fitDialogInParent(this, parentWidget(), 760);
}

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
    query.addQueryItem(QStringLiteral("height"), QStringLiteral("200"));
    query.addQueryItem(QStringLiteral("zoom"), QStringLiteral("13"));
    query.addQueryItem(QStringLiteral("paths"), pathsParam);
    query.addQueryItem(QStringLiteral("coordtype"), QStringLiteral("gcj02"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Referer", "https://lbsyun.baidu.com/");

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败）"));
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll())) {
            m_mapLabel->setText(QStringLiteral("（地图预览加载失败）"));
            return;
        }
        m_mapLabel->setPixmap(pix);
    });
}

void MapNavigationDialog::onBackToSetup()
{
    m_stack->setCurrentWidget(m_setupPage);
    m_statusLabel->setText(QStringLiteral("起终点已就绪，选择方式后点击「开始导航」。"));
    fitDialogInParent(this, parentWidget(), 460);
}
