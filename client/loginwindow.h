#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
#include <QJsonObject>

class ApiClient;

namespace Ui {
class LoginWindow;
}

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient *api, QWidget *parent = nullptr);
    ~LoginWindow();
protected:
    void paintEvent(QPaintEvent *event) override;
signals:
    void loginSucceeded(const QJsonObject &user);

private slots:
    void on_loginButton_clicked(); //点击登录按钮的槽函数

private:
    ApiClient *m_api = nullptr;
private:
    Ui::LoginWindow *ui;
};

#endif // LOGINWINDOW_H
