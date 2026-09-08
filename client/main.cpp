#include "loginwindow.h"
#include "mainwindow.h"
#include "apiclient.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QRect>
#include <QScreen>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 统一字体（与 Admin 端一致）
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

    QObject::connect(&login, &LoginWindow::loginSucceeded, &app, [&](const QJsonObject &user) {
        auto *mainWin = new MainWindow(&api, user);
        // 登录后新窗口沿用登录窗口当前的屏幕位置，避免回到左上角
        mainWin->move(login.pos());
        mainWin->show();
        login.close();
    });

    login.show();
    // 登录窗口初始居中于屏幕主工作区
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        login.move(avail.center() - login.rect().center());
    }

    return app.exec();
}
