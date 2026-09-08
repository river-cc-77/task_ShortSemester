#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QJsonObject>
#include <QWidget>

class ApiClient;
class QLineEdit;
class QPushButton;
class QLabel;

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient *api, QWidget *parent = nullptr);
    ~LoginWindow() override;

signals:
    void loginSucceeded(const QJsonObject &user);

private slots:
    void onLoginClicked();

private:
    ApiClient *m_api = nullptr;
    QLineEdit *m_phoneEdit = nullptr;
    QPushButton *m_loginButton = nullptr;
    QLabel *m_errorLabel = nullptr;
};

#endif // LOGINWINDOW_H
