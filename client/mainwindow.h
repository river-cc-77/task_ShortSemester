#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonObject>
#include <QMainWindow>

class ApiClient;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ApiClient *api, const QJsonObject &user, QWidget *parent = nullptr);

private slots:
    void onRefreshStations();

private:
    void loadStations();

    ApiClient *m_api = nullptr;
    QJsonObject m_user;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_latEdit = nullptr;
    QLineEdit *m_lngEdit = nullptr;
    QListWidget *m_stationList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
};

#endif // MAINWINDOW_H
