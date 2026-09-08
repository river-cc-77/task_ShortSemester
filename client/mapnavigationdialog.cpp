#include "mapnavigationdialog.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

QString buildBaiduDirectionUrl(double originLat, double originLng,
                               double destLat, double destLng,
                               const QString &destName)
{
    // 官方地图调起 API（map.baidu.com/direction 会 404）
    QUrl url(QStringLiteral("https://api.map.baidu.com/direction"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("origin"),
                       QStringLiteral("latlng:%1,%2|name:%3")
                           .arg(originLat, 0, 'f', 6)
                           .arg(originLng, 0, 'f', 6)
                           .arg(QStringLiteral("当前位置")));
    query.addQueryItem(QStringLiteral("destination"),
                       QStringLiteral("latlng:%1,%2|name:%3")
                           .arg(destLat, 0, 'f', 6)
                           .arg(destLng, 0, 'f', 6)
                           .arg(destName));
    query.addQueryItem(QStringLiteral("mode"), QStringLiteral("driving"));
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("html"));
    query.addQueryItem(QStringLiteral("coord_type"), QStringLiteral("gcj02"));
    query.addQueryItem(QStringLiteral("src"), QStringLiteral("webapp.chargeClient.navigation"));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

} // namespace

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

    const QString navUrl = buildBaiduDirectionUrl(originLat, originLng, destLat, destLng, destName);

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
