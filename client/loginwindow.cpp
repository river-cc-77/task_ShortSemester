#include "loginwindow.h"
#include "ui_loginwindow.h"
#include "apiclient.h"
#include <QPainter>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
    , ui(new Ui::LoginWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("充电桩用户端 - 登录"));
    ui->phoneEdit->setMaxLength(11);
    ui->promptMsgLabel->setStyleSheet(QStringLiteral("color: red;"));

    this->setStyleSheet(R"(
        /* 手机号输入框：半透明白底 + 圆角 */
        QLineEdit#phoneEdit{
            background-color: rgba(255, 255, 255, 40);   /* 白色，alpha=180，约70%不透明 */
            border: 1px solid rgba(255, 255, 255, 200);
            border-radius: 6px;                            /* 圆角 */
            padding: 8px 12px;                             /* 内边距，让文字不贴边 */
            font-size: 14px;
            color: #333333;
        }
        /* 输入框获得焦点时的样式 */
        QLineEdit#phoneEdit:focus{
            background-color: rgba(255, 255, 255, 220);   /* 聚焦时更不透明一点 */
            border: 1px solid rgba(0, 150, 255, 200);
        }

        /* 登录按钮：半透明 + 圆角 */
        QPushButton#loginButton{
            background-color: rgba(0, 120, 215, 160);     /* 蓝色半透明，alpha=160约63% */
            border: none;
            border-radius: 6px;
            padding: 10px;
            font-size: 15px;
            color: white;
            font-weight: bold;
        }
        /* 按钮鼠标悬停 */
        QPushButton#loginButton:hover{
            background-color: rgba(0, 120, 215, 200);     /* 悬停时更不透明 */
        }
        /* 按钮按下 */
        QPushButton#loginButton:pressed{
            background-color: rgba(0, 100, 180, 220);
        }
    )");
    resize(860, 580);
}

LoginWindow::~LoginWindow()
{
    delete ui;
}

void LoginWindow::on_loginButton_clicked()
{
    ui->promptMsgLabel->clear();
    const QString phone = ui->phoneEdit->text().trimmed();
    if (phone.size() != 11) {
        ui->promptMsgLabel->setText(QStringLiteral("请输入11位手机号"));
        return;
    }

    ui->loginButton->setEnabled(false);
    QJsonObject data;
    data["phone"] = phone;
    const QJsonObject resp = m_api->call(QStringLiteral("user.login"), data);
    ui->loginButton->setEnabled(true);

    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        ui->promptMsgLabel->setText(err.value(QStringLiteral("message")).toString());
        return;
    }

    const QJsonObject user = resp.value(QStringLiteral("data")).toObject();
    m_api->setToken(user.value(QStringLiteral("token")).toString());
    emit loginSucceeded(user);
}

//重写paintEvent以设置登录页背景
void LoginWindow::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    QPixmap bgPix(":/res/resources/login_background.png");
    // KeepAspectRatioByExpanding = 等价CSS cover，填满窗口，图片超出部分裁剪
    QPixmap scaledBg = bgPix.scaled(this->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    painter.drawPixmap(0,0, scaledBg);
}
