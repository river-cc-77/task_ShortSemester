#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonObject>
#include <QMainWindow>

class ApiClient;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent = nullptr);

private:
    ApiClient *m_api = nullptr;
    QLabel *m_welcomeLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
};

#endif // MAINWINDOW_H
