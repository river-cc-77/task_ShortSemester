#include "loginwindow.h"

#include "apiclient.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("充电桩用户端 - 登录"));

    auto *title = new QLabel(QStringLiteral("手机号登录（11位，免密）"), this);
    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setPlaceholderText(QStringLiteral("例如 13800138001"));
    m_phoneEdit->setMaxLength(11);

    m_loginButton = new QPushButton(QStringLiteral("登录"), this);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(m_phoneEdit);
    layout->addWidget(m_loginButton);
    layout->addWidget(m_errorLabel);
    setLayout(layout);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    resize(360, 180);
}

void LoginWindow::onLoginClicked()
{
    m_errorLabel->clear();
    const QString phone = m_phoneEdit->text().trimmed();
    if (phone.size() != 11) {
        m_errorLabel->setText(QStringLiteral("请输入11位手机号"));
        return;
    }

    m_loginButton->setEnabled(false);
    QJsonObject data;
    data["phone"] = phone;
    const QJsonObject resp = m_api->call(QStringLiteral("user.login"), data);
    m_loginButton->setEnabled(true);

    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        m_errorLabel->setText(err.value(QStringLiteral("message")).toString());
        return;
    }

    const QJsonObject user = resp.value(QStringLiteral("data")).toObject();
    m_api->setToken(user.value(QStringLiteral("token")).toString());
    emit loginSucceeded(user);
}
