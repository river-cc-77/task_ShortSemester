#include "mainwindow.h"

#include "apiclient.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
{
    Q_UNUSED(m_api);
    setWindowTitle(QStringLiteral("充电桩管理端"));

    auto *central = new QWidget(this);
    m_welcomeLabel = new QLabel(
        QStringLiteral("欢迎，管理员 %1").arg(admin.value(QStringLiteral("username")).toString()),
        central);
    m_hintLabel = new QLabel(
        QStringLiteral("管理主界面壳子已就绪。\n"
                        "B 同学在此扩展：KPI 图表、电桩/电站/用户管理等功能。\n"
                        "业务 API 见 docs/protocal.md（如 stats.overview、pile.list）。"),
        central);
    m_hintLabel->setWordWrap(true);

    auto *layout = new QVBoxLayout(central);
    layout->addWidget(m_welcomeLabel);
    layout->addWidget(m_hintLabel);
    layout->addStretch();
    central->setLayout(layout);
    setCentralWidget(central);
    resize(960, 600);
}
