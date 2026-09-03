#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QJsonObject>
#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class ApiClient;

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient *api, QWidget *parent = nullptr);

signals:
    void loginSucceeded(const QJsonObject &admin);

private slots:
    void onLoginClicked();

private:
    ApiClient *m_api = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_loginButton = nullptr;
};

#endif // LOGINWINDOW_H
