#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QJsonObject>
#include <QMainWindow>
class ApiClient;
namespace Ui {
class MainWindow;
}
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUserFreezeClick(int userId, bool wantFreeze);

private:
    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    void resetAllBtnSelect();
    void reloadUserList(const QString& keyword = "");
    void addUserRow(const QJsonObject& userObj);
    void reloadOverviewStat();
};
#endif // MAINWINDOW_H
