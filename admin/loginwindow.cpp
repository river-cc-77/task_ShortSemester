#include "loginwindow.h"

#include "apiclient.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("充电桩管理端 - 登录"));

    m_userEdit = new QLineEdit(QStringLiteral("admin"), this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("默认 123456"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_userEdit);
    form->addRow(QStringLiteral("密码"), m_passEdit);

    m_loginButton = new QPushButton(QStringLiteral("登录"), this);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_loginButton);
    layout->addWidget(m_errorLabel);
    setLayout(layout);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    resize(400, 200);
}

void LoginWindow::onLoginClicked()
{
    m_errorLabel->clear();
    QJsonObject data;
    data["username"] = m_userEdit->text().trimmed();
    data["password"] = m_passEdit->text();

    m_loginButton->setEnabled(false);
    const QJsonObject resp = m_api->call(QStringLiteral("admin.login"), data);
    m_loginButton->setEnabled(true);

    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        m_errorLabel->setText(err.value(QStringLiteral("message")).toString());
        return;
    }

    const QJsonObject admin = resp.value(QStringLiteral("data")).toObject();
    m_api->setToken(admin.value(QStringLiteral("token")).toString());
    emit loginSucceeded(admin);
}
