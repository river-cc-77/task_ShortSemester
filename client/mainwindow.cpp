#include "mainwindow.h"

#include "apiclient.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(ApiClient *api, const QJsonObject &user, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_user(user)
{
    setWindowTitle(QStringLiteral("充电桩用户端"));

    auto *central = new QWidget(this);
    m_userLabel = new QLabel(central);
    m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                             .arg(m_user.value(QStringLiteral("nickname")).toString())
                             .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2));

    auto *locLabel = new QLabel(QStringLiteral("当前位置（模拟 GPS）："), central);
    m_latEdit = new QLineEdit(QStringLiteral("22.5431"), central);
    m_lngEdit = new QLineEdit(QStringLiteral("114.0579"), central);
    m_latEdit->setPlaceholderText(QStringLiteral("纬度 lat"));
    m_lngEdit->setPlaceholderText(QStringLiteral("经度 lng"));

    auto *locLayout = new QHBoxLayout;
    locLayout->addWidget(new QLabel(QStringLiteral("lat"), central));
    locLayout->addWidget(m_latEdit);
    locLayout->addWidget(new QLabel(QStringLiteral("lng"), central));
    locLayout->addWidget(m_lngEdit);

    m_refreshButton = new QPushButton(QStringLiteral("刷新附近充电站"), central);
    m_stationList = new QListWidget(central);
    m_statusLabel = new QLabel(central);

    auto *layout = new QVBoxLayout(central);
    layout->addWidget(m_userLabel);
    layout->addWidget(locLabel);
    layout->addLayout(locLayout);
    layout->addWidget(m_refreshButton);
    layout->addWidget(m_stationList);
    layout->addWidget(m_statusLabel);
    central->setLayout(layout);
    setCentralWidget(central);
    resize(640, 480);

    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshStations);
    loadStations();
}

void MainWindow::onRefreshStations()
{
    loadStations();
}

void MainWindow::loadStations()
{
    m_statusLabel->clear();
    m_stationList->clear();

    QJsonObject data;
    data["lat"] = m_latEdit->text().toDouble();
    data["lng"] = m_lngEdit->text().toDouble();
    data["keyword"] = QString();

    const QJsonObject resp = m_api->call(QStringLiteral("station.list"), data);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        m_statusLabel->setText(err.value(QStringLiteral("message")).toString());
        return;
    }

    const QJsonArray items = resp.value(QStringLiteral("data")).toObject().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString line = QStringLiteral("%1 | %2元/度 | 空闲 %3/%4 | %5 km")
                                 .arg(item.value(QStringLiteral("name")).toString())
                                 .arg(item.value(QStringLiteral("price")).toDouble(), 0, 'f', 2)
                                 .arg(item.value(QStringLiteral("idle_piles")).toInt())
                                 .arg(item.value(QStringLiteral("total_piles")).toInt())
                                 .arg(item.value(QStringLiteral("distance_km")).toDouble(), 0, 'f', 1);
        m_stationList->addItem(line);
    }

    m_statusLabel->setText(QStringLiteral("共 %1 个充电站（按距离排序）").arg(items.size()));
}
