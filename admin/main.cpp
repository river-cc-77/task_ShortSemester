#include "loginwindow.h"
#include "mainwindow.h"
#include "apiclient.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

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
