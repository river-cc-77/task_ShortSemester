#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "apiclient.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>


MainWindow::MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_api(api)
{
    Q_UNUSED(admin);
    ui->setupUi(this);
    this->setWindowTitle("充电桩管理系统");

    resetAllBtnSelect();
    ui->btnOverview->setProperty("selected", true);
    ui->btnOverview->setStyleSheet(ui->btnOverview->styleSheet());
    ui->stackedWidget->setCurrentIndex(0);

    //侧边栏切换
    connect(ui->btnOverview,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnOverview->setProperty("selected", true);
        ui->btnOverview->setStyleSheet(ui->btnOverview->styleSheet());
        ui->stackedWidget->setCurrentIndex(0);
    });
    connect(ui->btnPile,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnPile->setProperty("selected", true);
        ui->btnPile->setStyleSheet(ui->btnPile->styleSheet());
        ui->stackedWidget->setCurrentIndex(1);
    });
    connect(ui->btnStation,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnStation->setProperty("selected", true);
        ui->btnStation->setStyleSheet(ui->btnStation->styleSheet());
        ui->stackedWidget->setCurrentIndex(2);
    });
    connect(ui->btnUser,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnUser->setProperty("selected", true);
        ui->btnUser->setStyleSheet(ui->btnUser->styleSheet());
        ui->stackedWidget->setCurrentIndex(3);
    });
    connect(ui->btnLog,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnLog->setProperty("selected", true);
        ui->btnLog->setStyleSheet(ui->btnLog->styleSheet());
        ui->stackedWidget->setCurrentIndex(4);
    });

    //用户表格初始化
    ui->tableUser->setColumnCount(7);
    QStringList headers = {
        QStringLiteral("用户ID"),
        QStringLiteral("手机号"),
        QStringLiteral("昵称"),
        QStringLiteral("余额"),
        QStringLiteral("注册时间"),
        QStringLiteral("状态"),
        QStringLiteral("操作")
    };
    ui->tableUser->setHorizontalHeaderLabels(headers);
    ui->tableUser->verticalHeader()->setVisible(false);
    ui->tableUser->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableUser->verticalHeader()->setDefaultSectionSize(44);
    ui->tableUser->setAlternatingRowColors(true);
    ui->tableUser->setStyleSheet(R"(
QTableWidget{
    border:1px solid #cccccc;
    gridline-color:#e8e8e8;
    background-color:#ffffff;
    alternate-background-color:#f7f9fc;
}
QHeaderView::section{
    background-color:#4078d8;
    color:white;
    padding:6px;
    border:none;
    font-size:13px;
}
QTableWidget::item{
    padding:4px;
}
QPushButton{
    padding:4px 10px;
    border-radius:4px;
    background-color:#4078d8;
    color:#fff;
    border:none;
}
QPushButton:hover{
    background-color:#2e64c2;
}
    )");

    //切到用户页面自动加载全部
    connect(ui->stackedWidget, &QStackedWidget::currentChanged, this, [=](int index){
        if(index == 0)
        {
            reloadOverviewStat();
        }
        else if(index == 3)
        {
            reloadUserList("");
        }
    });

    //搜索按钮
    connect(ui->btnSearch, &QPushButton::clicked, this, [=](){
        QString key = ui->editSearchPhone->text().trimmed();
        reloadUserList(key);
    });
    //回车搜索
    connect(ui->editSearchPhone, &QLineEdit::returnPressed, this, [=](){
        QString key = ui->editSearchPhone->text().trimmed();
        reloadUserList(key);
    });

    connect(ui->editSearchPhone,&QLineEdit::textChanged,this,[=](const QString &txt){
        if(txt.isEmpty()){
            reloadUserList("");
        }
    });

    reloadUserList("");
    //加载总览统计
    reloadOverviewStat();

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resetAllBtnSelect()
{
    auto btns = {ui->btnOverview, ui->btnPile, ui->btnStation, ui->btnUser, ui->btnLog};
    for(auto btn : btns)
    {
        btn->setProperty("selected", false);
        btn->setStyleSheet(btn->styleSheet());
    }
}

void MainWindow::reloadUserList(const QString &keyword)
{
    ui->tableUser->setRowCount(0);
#if 1
// ========== 真实后端 user.admin.list ==========
    QJsonObject param;
    if (!keyword.isEmpty())
        param["phone_keyword"] = keyword;

    QJsonObject resp = m_api->call("user.admin.list", param);
    if (!resp["ok"].toBool())
    {
        QMessageBox::warning(this, "错误", "获取用户列表失败");
        return;
    }
    QJsonObject dataObj = resp["data"].toObject();
    QJsonArray items = dataObj["items"].toArray();
    if (items.isEmpty() && !keyword.isEmpty())
    {
        QMessageBox::information(this, "提示", "未找到相关用户");
    }
    for (auto obj : items)
    {
        addUserRow(obj.toObject());
    }
#else
// ========== Mock模拟（调试UI用，字段对齐协议） ==========
    QJsonObject u1, u2;
    u1["user_id"] = 1;
    u1["phone"] = "13800138000";
    u1["nickname"] = "超级管理员";
    u1["balance"] = 1000.50;
    u1["created_at"] = "2026‑09‑01 10:00:00";
    u1["status"] = "正常";

    u2["user_id"] = 2;
    u2["phone"] = "13900139000";
    u2["nickname"] = "测试用户";
    u2["balance"] = 200.00;
    u2["created_at"] = "2026‑09‑02 14:20:00";
    u2["status"] = "冻结";

    QJsonArray arr;
    if (keyword.isEmpty())
    {
        arr << u1 << u2;
    }
    else
    {
        if(u1["phone"].toString().contains(keyword)) arr << u1;
        if(u2["phone"].toString().contains(keyword)) arr << u2;
        if(arr.isEmpty())
        {
            QMessageBox::information(this, "提示", "未找到相关用户");
        }
    }
    for(auto o : arr)
    {
        addUserRow(o.toObject());
    }
#endif
}

void MainWindow::onUserFreezeClick(int userId, bool wantFreeze)
{
    //====新增确认弹窗====
    QString tip = wantFreeze
        ? "确定要冻结该用户？冻结后用户将无法登录系统。"
        : "确定要解冻该用户，恢复登录权限？";

    auto res = QMessageBox::question(this, "操作确认", tip,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if(res != QMessageBox::Yes)
    {
        // 用户点否，直接退出函数，不调用接口
        return;
    }

    QJsonObject params;
    params["user_id"] = userId;
    params["freeze"] = wantFreeze;
    QJsonObject resp = m_api->call(QStringLiteral("user.freeze"), params);
    if(!resp["ok"].toBool())
    {
        QString errMsg;
        if(resp.contains("error"))
        {
            errMsg = resp["error"].toObject()["message"].toString();
        }
        else
        {
            errMsg = resp["msg"].toString();
        }
        if(errMsg.isEmpty())
        {
            errMsg = "操作未知错误";
        }
        QMessageBox::warning(this,QStringLiteral("操作失败"), errMsg);
        return;
    }
    reloadUserList("");
}


void MainWindow::addUserRow(const QJsonObject &userObj)
{
    int row = ui->tableUser->rowCount();
    ui->tableUser->insertRow(row);

    // user_id int
    int uid = userObj["user_id"].toInt();
    auto *itemId = new QTableWidgetItem(QString::number(uid));
    itemId->setFlags(itemId->flags() & ~Qt::ItemIsEditable);
    ui->tableUser->setItem(row,0,itemId);

    auto *itemPhone = new QTableWidgetItem(userObj["phone"].toString());
    itemPhone->setFlags(itemPhone->flags() & ~Qt::ItemIsEditable);
    ui->tableUser->setItem(row,1,itemPhone);

    ui->tableUser->setItem(row,2, new QTableWidgetItem(userObj["nickname"].toString()));
    ui->tableUser->setItem(row,3, new QTableWidgetItem(QString::number(userObj["balance"].toDouble())));
    ui->tableUser->setItem(row,4, new QTableWidgetItem(userObj["created_at"].toString()));

    QString statusText = userObj["status"].toString();
    bool isFrozen = (statusText == QStringLiteral("冻结"));
    ui->tableUser->setItem(row,5, new QTableWidgetItem(statusText));

    QWidget *btnContainer = new QWidget();
    QHBoxLayout *btnLayout = new QHBoxLayout(btnContainer);
    btnLayout->setContentsMargins(4,2,4,2);
    QPushButton *opBtn = new QPushButton(isFrozen ? QStringLiteral("解冻") : QStringLiteral("冻结"));
    btnLayout->addWidget(opBtn);
    ui->tableUser->setCellWidget(row,6, btnContainer);

    connect(opBtn,&QPushButton::clicked,this,[=](){
        onUserFreezeClick(uid, !isFrozen);
    });
}

void MainWindow::reloadOverviewStat()
{
    QJsonObject param;
    param["days"] = 7;
    QJsonObject resp = m_api->call("stats.overview", param);
    if (!resp["ok"].toBool())
    {
        QMessageBox::warning(this, "提示", "获取统计数据失败");
        return;
    }
    QJsonObject d = resp["data"].toObject();
    ui->labIncome->setText(QString::number(d["today_revenue"].toDouble()));
    ui->labOrderCnt->setText(QString::number(d["today_orders"].toInt()));
    ui->labUserCnt->setText(QString::number(d["user_count"].toInt()));

    QJsonObject pileStat = d["pile_status"].toObject();
    const int online = pileStat.value(QStringLiteral("在用")).toInt(0)
                     + pileStat.value(QStringLiteral("闲置")).toInt(0);
    ui->labPileOnline->setText(QString::number(online));
}

