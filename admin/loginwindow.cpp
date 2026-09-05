#include "loginwindow.h"
#include "apiclient.h"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("充电桩管理端 - 登录"));
    resize(660, 400);

    setStyleSheet(R"(
    QWidget{
        background-color:#f7f8fa;
    }
    QLineEdit{
        border:1px solid #dddddd;
        border-radius:8px;
        padding:10px 12px;
        font-size:11pt;
        background-color:#ffffff;
    }
    QLineEdit:focus{
        border:1px solid #4078e8;
    }
    QPushButton{
        background-color:#4078e8;
        color:#ffffff;
        border:none;
        border-radius:8px;
        padding:11px;
        font-size:11pt;
        font-weight:500;
    }
    QPushButton:hover{
        background-color:#2f62cc;
    }
    QPushButton:pressed{
        background-color:#2552ad;
    }
    QPushButton:disabled{
        background-color:#88a8e0;
    }
    QLabel#errorLabel{
        color:#dd3333;
    }
    )");

    m_userEdit = new QLineEdit(QStringLiteral("admin"), this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("默认 123456"));

    // ========== 定死500像素宽度 ==========
    const int inputWidth = 375;
    m_userEdit->setFixedWidth(inputWidth);
    m_passEdit->setFixedWidth(inputWidth);

    auto *form = new QFormLayout;
    // 关键：关闭表单自动拉伸右侧输入框！否则会覆盖fixedWidth
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignRight|Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(20);
    form->addRow(QStringLiteral("账号"), m_userEdit);
    form->addRow(QStringLiteral("密码"), m_passEdit);

    m_loginButton = new QPushButton(QStringLiteral("登录"), this);
    m_loginButton->setFixedWidth(inputWidth);
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setAlignment(Qt::AlignCenter);

    QWidget *centerCard = new QWidget(this);
    centerCard->setMaximumWidth(inputWidth + 80);
    auto *cardLayout = new QVBoxLayout(centerCard);
    cardLayout->setContentsMargins(0,0,0,0);
    cardLayout->setSpacing(22);
    cardLayout->addLayout(form);
    cardLayout->addWidget(m_loginButton,0,Qt::AlignHCenter); //按钮居中
    cardLayout->addWidget(m_errorLabel);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(30,40,30,40);
    rootLayout->addStretch(1);
    rootLayout->addWidget(centerCard, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);
    setLayout(rootLayout);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
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
