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
    setWindowTitle(QStringLiteral("充电桩管理系统"));
    setObjectName(QStringLiteral("loginRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    resize(760, 520);
    setMinimumSize(700, 500);

    // ========== 表单输入 ==========
    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    m_userEdit->setText(QStringLiteral("admin"));

    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("请输入密码（默认 123456）"));

    const int inputWidth = 360;
    m_userEdit->setFixedWidth(inputWidth);
    m_passEdit->setFixedWidth(inputWidth);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(18);
    form->addRow(QStringLiteral("账号"), m_userEdit);
    form->addRow(QStringLiteral("密码"), m_passEdit);

    // ========== 登录按钮 ==========
    m_loginButton = new QPushButton(QStringLiteral("登录"), this);
    m_loginButton->setProperty("class", "primary");
    m_loginButton->setFixedWidth(inputWidth);
    m_loginButton->setMinimumHeight(42);
    m_loginButton->setCursor(Qt::PointingHandCursor);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);

    // ========== 标题区 ==========
    auto *title = new QLabel(QStringLiteral("充电桩管理系统"), this);
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new QLabel(QStringLiteral("Charge Pile Admin Console"), this);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);

    // ========== 白色卡片 ==========
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(48, 44, 48, 40);
    cardLayout->setSpacing(20);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(8);
    cardLayout->addLayout(form);
    cardLayout->addWidget(m_loginButton, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_errorLabel);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(40, 36, 40, 36);
    rootLayout->addStretch(1);
    rootLayout->addWidget(card, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(m_passEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
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
