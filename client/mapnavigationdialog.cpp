#include "mapnavigationdialog.h"

#include <QDesktopServices>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

#if __has_include(<QWebEngineView>)
#include <QWebEngineView>
#define HAS_WEBENGINE 1
#else
#define HAS_WEBENGINE 0
#endif

MapNavigationDialog::MapNavigationDialog(double originLat, double originLng,
                                         double destLat, double destLng,
                                         const QString &destName,
                                         const QString &baiduAk,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("地图导航 - %1").arg(destName));
    resize(900, 640);

    const QString encodedName = QString::fromUtf8(QUrl::toPercentEncoding(destName));
    const QString navUrl = QStringLiteral(
        "https://map.baidu.com/direction?"
        "origin=latlng:%1,%2|name:%3"
        "&destination=latlng:%4,%5|name:%6"
        "&mode=driving&region=全国&output=html&src=webapp.chargeClient")
                               .arg(originLat, 0, 'f', 6)
                               .arg(originLng, 0, 'f', 6)
                               .arg(QString::fromUtf8(QUrl::toPercentEncoding(QStringLiteral("当前位置"))))
                               .arg(destLat, 0, 'f', 6)
                               .arg(destLng, 0, 'f', 6)
                               .arg(encodedName);

    auto *layout = new QVBoxLayout(this);

#if HAS_WEBENGINE
    auto *view = new QWebEngineView(this);
    view->setUrl(QUrl(navUrl));
    layout->addWidget(view, 1);
#else
    auto *tip = new QLabel(
        QStringLiteral("未安装 Qt WebEngine 组件，将使用系统浏览器打开百度地图导航。"),
        this);
    tip->setWordWrap(true);
    layout->addWidget(tip);
    QDesktopServices::openUrl(QUrl(navUrl));
#endif

    auto *openBtn = new QPushButton(QStringLiteral("在浏览器中打开"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(openBtn, &QPushButton::clicked, this, [navUrl]() {
        QDesktopServices::openUrl(QUrl(navUrl));
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(openBtn);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    Q_UNUSED(baiduAk);
}
