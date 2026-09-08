#include "loginwindow.h"
#include "apiclient.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("充电桩用户端"));
    setObjectName(QStringLiteral("loginRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(390, 844);          // 模拟手机竖屏尺寸，锁定大小

    // ========== 标题区 ==========
    auto *title = new QLabel(QStringLiteral("电动汽车充电桩"), this);
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new QLabel(QStringLiteral("EV Charging · 用户端"), this);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);

    auto *phoneHint = new QLabel(QStringLiteral("手机号快捷登录（免密）"), this);
    phoneHint->setObjectName(QStringLiteral("loginPhoneHint"));
    phoneHint->setAlignment(Qt::AlignCenter);

    // ========== 手机号输入 ==========
    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    m_phoneEdit->setMaxLength(11);
    m_phoneEdit->setMinimumHeight(44);

    // ========== 登录按钮（品牌蓝主按钮） ==========
    m_loginButton = new QPushButton(QStringLiteral("登 录"), this);
    m_loginButton->setObjectName(QStringLiteral("loginButton"));
    m_loginButton->setProperty("class", "primary");
    m_loginButton->setMinimumHeight(46);
    m_loginButton->setCursor(Qt::PointingHandCursor);

    // ========== 错误/提示 ==========
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);

    // ========== 白色居中卡片 ==========
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 32, 26, 30);
    cardLayout->setSpacing(16);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(6);
    cardLayout->addWidget(phoneHint);
    cardLayout->addSpacing(14);
    cardLayout->addWidget(m_phoneEdit);
    cardLayout->addWidget(m_loginButton);
    cardLayout->addSpacing(2);
    cardLayout->addWidget(m_errorLabel);

    // ========== 根布局：上下留白让卡片居中偏上 ==========
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(30, 60, 30, 80);
    rootLayout->addStretch(2);
    rootLayout->addWidget(card);
    rootLayout->addStretch(3);

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(m_phoneEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
}

LoginWindow::~LoginWindow() = default;

void LoginWindow::onLoginClicked()
{
    m_errorLabel->clear();
    const QString phone = m_phoneEdit->text().trimmed();
    if (phone.size() != 11) {
        m_errorLabel->setText(QStringLiteral("请输入 11 位手机号"));
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
