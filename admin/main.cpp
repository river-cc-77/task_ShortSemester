#include "loginwindow.h"
#include "mainwindow.h"
#include "apiclient.h"

#include "../client/imsetup.h"

#include <QApplication>
#include <QFile>
#include <QFont>

int main(int argc, char *argv[])
{
    setupInputMethodEnv();

    QApplication app(argc, argv);
    logInputMethodStatus();

    // 统一字体
    QFont baseFont(QStringLiteral("Microsoft YaHei UI"));
    baseFont.setPointSize(10);
    app.setFont(baseFont);

    // 全局主题（打包在资源中，随程序分发，无需外部文件）
    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    ApiClient api;
    LoginWindow login(&api);

    QObject::connect(&login, &LoginWindow::loginSucceeded, &app, [&](const QJsonObject &admin) {
        auto *mainWin = new MainWindow(&api, admin);
        mainWin->show();
        login.close();
    });

    login.show();
    return app.exec();
}
