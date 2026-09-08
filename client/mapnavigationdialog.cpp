#include "mapnavigationdialog.h"
#include "uiutil.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString encodeBaiduParam(const QString &raw)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(raw, "|:,;"));
}

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

QString buildBaiduDirectionUrl(double originLat, double originLng, const QString &originName,
                               double destLat, double destLng, const QString &destName,
                               MapNavigationDialog::NavMode mode, const QString &region)
{
    const QString originVal = QStringLiteral("name:%1|latlng:%2,%3")
                                  .arg(originName)
                                  .arg(originLat, 0, 'f', 6)
                                  .arg(originLng, 0, 'f', 6);
    const QString destVal = QStringLiteral("name:%1|latlng:%2,%3")
                                .arg(destName)
                                .arg(destLat, 0, 'f', 6)
                                .arg(destLng, 0, 'f', 6);
    const QString modeStr = mode == MapNavigationDialog::NavMode::Walking
                                ? QStringLiteral("walking")
                                : QStringLiteral("driving");

    QString url = QStringLiteral("https://api.map.baidu.com/direction?"
                                 "origin=%1&destination=%2&mode=%3&output=html"
                                 "&coord_type=gcj02&src=webapp.chargeClient.navigation")
                      .arg(encodeBaiduParam(originVal), encodeBaiduParam(destVal), modeStr);
    if (!region.isEmpty()) {
        url += QStringLiteral("&region=%1").arg(encodeBaiduParam(region));
    }
    return url;
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
                                         const QString &destAddress,
                                         QWidget *parent)
    : QDialog(parent)
    , m_originDesc(originDesc)
    , m_originLat(originLat)
    , m_originLng(originLng)
    , m_destName(destName)
    , m_destLat(destLat)
    , m_destLng(destLng)
    , m_region(guessRegionFromAddress(destAddress))
{
    setObjectName(QStringLiteral("navDialog"));
    setWindowTitle(QStringLiteral("地图导航"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("地图导航"), this);
    titleLabel->setObjectName(QStringLiteral("userGreet"));
    layout->addWidget(titleLabel);

    auto *routeCard = makeCard(this);
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
    layout->addWidget(routeCard);

    auto *modeCard = makeCard(this);
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
    layout->addWidget(modeCard);

    m_statusLabel = new QLabel(
        QStringLiteral("起终点已就绪，选择方式后点击「开始导航」。"),
        this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    m_startBtn = new QPushButton(QStringLiteral("开始导航"), this);
    m_startBtn->setProperty("class", "primary");
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumHeight(42);

    auto *closeBtn = new QPushButton(QStringLiteral("返回"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setMinimumHeight(36);

    layout->addWidget(m_startBtn);
    layout->addWidget(closeBtn);

    connect(m_startBtn, &QPushButton::clicked, this, &MapNavigationDialog::onStartNavigation);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    fitDialogInParent(this, parent, 460);
}

MapNavigationDialog::NavMode MapNavigationDialog::selectedMode() const
{
    return m_walkingRadio->isChecked() ? NavMode::Walking : NavMode::Driving;
}

QString MapNavigationDialog::buildBaiduDirectionUrl(NavMode mode) const
{
    return ::buildBaiduDirectionUrl(m_originLat, m_originLng, m_originDesc,
                                    m_destLat, m_destLng, m_destName,
                                    mode, m_region);
}

void MapNavigationDialog::onStartNavigation()
{
    const QString navUrl = buildBaiduDirectionUrl(selectedMode());
    if (!QDesktopServices::openUrl(QUrl(navUrl))) {
        m_statusLabel->setText(QStringLiteral("导航加载失败，请检查网络"));
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("导航加载失败，请检查网络"));
        return;
    }
    accept();
}
