#include "mapnavigationdialog.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

#ifdef CHARGE_USE_WEBENGINE
#include <QCoreApplication>
#include <QWebEngineView>

static bool ensureWebEngineProcessPath()
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

#ifdef CHARGE_USE_WEBENGINE
    if (ensureWebEngineProcessPath()) {
        auto *view = new QWebEngineView(this);
        view->setUrl(QUrl(navUrl));
        layout->addWidget(view, 1);
    } else
#endif
    {
        auto *tip = new QLabel(
            QStringLiteral("将使用系统浏览器打开百度地图驾车导航。"),
            this);
        tip->setWordWrap(true);
        layout->addWidget(tip);
        QDesktopServices::openUrl(QUrl(navUrl));
    }

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
