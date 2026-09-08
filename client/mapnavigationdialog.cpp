#include "mapnavigationdialog.h"

#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString encodeBaiduParam(const QString &raw)
{
    // 百度 direction 参数中的 | : 为语法字符，不能编码
    return QString::fromUtf8(QUrl::toPercentEncoding(raw, "|:,;"));
}

QString guessRegionFromAddress(const QString &address)
{
    static const QRegularExpression re(QStringLiteral("([\\u4e00-\\u9fa5]{2,10})市"));
    const QRegularExpressionMatch match = re.match(address);
    if (!match.hasMatch()) {
        return QString();
    }
    return match.captured(1);
}

QString buildBaiduDirectionUrl(double originLat, double originLng, const QString &originName,
                               double destLat, double destLng, const QString &destName,
                               MapNavigationDialog::NavMode mode, const QString &region)
{
    const QString originVal = QStringLiteral("latlng:%1,%2|name:%3")
                                  .arg(originLat, 0, 'f', 6)
                                  .arg(originLng, 0, 'f', 6)
                                  .arg(originName);
    const QString destVal = QStringLiteral("latlng:%1,%2|name:%3")
                                .arg(destLat, 0, 'f', 6)
                                .arg(destLng, 0, 'f', 6)
                                .arg(destName);
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
    setWindowTitle(QStringLiteral("地图导航"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);

    auto *routeBox = new QGroupBox(QStringLiteral("路线信息"), this);
    auto *routeLay = new QVBoxLayout(routeBox);

    m_originLabel = new QLabel(routeBox);
    m_originLabel->setWordWrap(true);
    m_originLabel->setText(QStringLiteral("起点：%1\n（%2, %3）")
                               .arg(m_originDesc)
                               .arg(m_originLat, 0, 'f', 6)
                               .arg(m_originLng, 0, 'f', 6));

    m_destLabel = new QLabel(routeBox);
    m_destLabel->setWordWrap(true);
    m_destLabel->setText(QStringLiteral("终点：%1\n（%2, %3）")
                             .arg(m_destName)
                             .arg(m_destLat, 0, 'f', 6)
                             .arg(m_destLng, 0, 'f', 6));

    routeLay->addWidget(m_originLabel);
    routeLay->addWidget(m_destLabel);
    layout->addWidget(routeBox);

    auto *modeBox = new QGroupBox(QStringLiteral("出行方式"), this);
    auto *modeLay = new QHBoxLayout(modeBox);
    m_drivingRadio = new QRadioButton(QStringLiteral("驾车"), modeBox);
    m_walkingRadio = new QRadioButton(QStringLiteral("步行"), modeBox);
    m_drivingRadio->setChecked(true);
    modeLay->addWidget(m_drivingRadio);
    modeLay->addWidget(m_walkingRadio);
    modeLay->addStretch();
    layout->addWidget(modeBox);

    m_statusLabel = new QLabel(QStringLiteral("正在加载路线…"), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_startBtn = new QPushButton(QStringLiteral("开始导航"), this);
    m_startBtn->setEnabled(false);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(m_startBtn, &QPushButton::clicked, this, &MapNavigationDialog::onStartNavigation);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_net = new QNetworkAccessManager(this);
    checkRouteAvailability();
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

void MapNavigationDialog::checkRouteAvailability()
{
    m_statusLabel->setText(QStringLiteral("正在加载路线…"));
    m_startBtn->setEnabled(false);

    const QUrl url(buildBaiduDirectionUrl(NavMode::Driving));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError
                        && (httpStatus == 0 || httpStatus < 400);

        if (ok) {
            m_statusLabel->setText(QStringLiteral("路线已就绪。选择出行方式后，点击「开始导航」将在外部地图中打开（起终点已预填）。"));
            m_startBtn->setEnabled(true);
            return;
        }

        m_statusLabel->setText(QStringLiteral("导航加载失败，请检查网络"));
        m_startBtn->setEnabled(false);
    });
}

void MapNavigationDialog::onStartNavigation()
{
    const QString navUrl = buildBaiduDirectionUrl(selectedMode());
    if (!QDesktopServices::openUrl(QUrl(navUrl))) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法打开外部地图，请检查系统默认浏览器。"));
        return;
    }
    accept();
}
