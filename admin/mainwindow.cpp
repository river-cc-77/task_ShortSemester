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
#include <QSet>
#include <QDate>
#include <QTableWidgetItem>

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

    // ========= 侧边栏切换 =========
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
    connect(ui->btnOrder,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnOrder->setProperty("selected", true);
        ui->btnOrder->setStyleSheet(ui->btnOrder->styleSheet());
        ui->stackedWidget->setCurrentIndex(6);
    });
    connect(ui->btnLog,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnLog->setProperty("selected", true);
        ui->btnLog->setStyleSheet(ui->btnLog->styleSheet());
        ui->stackedWidget->setCurrentIndex(4);
    });

    // ========= 营收趋势页面 index=5 =========
    connect(ui->btnChart,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnChart->setProperty("selected", true);
        ui->btnChart->setStyleSheet(ui->btnChart->styleSheet());
        ui->stackedWidget->setCurrentIndex(5);
        m_currentDays =7;
        ui->btnShift->setText("查看近30日");
        QJsonObject param;
        param["days"] = m_currentDays;
        QJsonObject resp = m_api->call("stats.overview", param);
        qDebug() << "[stats.overview] resp:" << resp;
        if(resp["ok"].toBool())
        {
            QJsonObject data = resp["data"].toObject();
            QJsonArray trend = data["revenue_trend"].toArray();
            qDebug() << "revenue_trend array:" << trend;
            drawRevenueChartFromJson(trend);
        }
        else
        {
            QString errMsg = resp["error"].toObject()["message"].toString("未知错误");
            QMessageBox::warning(this,"错误",QString("获取营收趋势失败：%1").arg(errMsg));
        }
    });

    // ========= 总览页面按钮 =========
    connect(ui->btnRefresh, &QPushButton::clicked, this, [=](){
        reloadOverviewStat();
    });

    // ========= 初始化图表控件 =========
    m_chart = new QCustomPlot();
    QVBoxLayout* lay = new QVBoxLayout(ui->widget_chart);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_chart);
    m_currentDays =7;
    m_chartTitle = new QCPTextElement(m_chart, "营收趋势 / 万元", QFont("sans",11,QFont::Bold));
    m_chart->plotLayout()->insertRow(0);
    m_chart->plotLayout()->addElement(0,0, m_chartTitle);

    // ========= 用户表格初始化 =========
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

    //页面切换信号
    connect(ui->stackedWidget, &QStackedWidget::currentChanged, this, [=](int index){
        if(index == 0)
        {
            reloadOverviewStat();
        }
        else if(index == 3)
        {
            reloadUserList("");
        }
        else if(index == 1) //电桩页面
        {
            loadStationCombo();
            reloadPileList();
        }
        else if(index == 6)
        {
            reloadOrderList();
        }
    });

    // 用户搜索
    connect(ui->btnSearch, &QPushButton::clicked, this, [=](){
        QString key = ui->editSearchPhone->text().trimmed();
        reloadUserList(key);
    });
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
    reloadOverviewStat();

    //==================== 电桩表格初始化【已修复：增加列数、表头】 ====================
    ui->tableWidgetPile->setColumnCount(8);
    QStringList pileHeaders = {
        "电桩编号",
        "所属电站",
        "电桩类型",
        "功率(kW)",
        "状态",
        "累计充电次数",
        "累计电量",
        "操作"
    };
    ui->tableWidgetPile->setHorizontalHeaderLabels(pileHeaders);
    ui->tableWidgetPile->verticalHeader()->setVisible(false);
    ui->tableWidgetPile->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetPile->verticalHeader()->setDefaultSectionSize(44);
    ui->tableWidgetPile->setAlternatingRowColors(true);

    // 初始化电桩状态下拉框
    ui->comboPileStatus->addItem("全部状态");
    ui->comboPileStatus->addItem("闲置");
    ui->comboPileStatus->addItem("预约");
    ui->comboPileStatus->addItem("在用");
    ui->comboPileStatus->addItem("故障");

    // 查询按钮（只保留一份connect，修复重复绑定）
    connect(ui->btnPileQuery,&QPushButton::clicked,this,[=](){
        reloadPileList();
    });

    //=====右上角电桩页面其他按钮=====
    connect(ui->btnGoPileStatus,&QPushButton::clicked,this,[=](){
        goToSelectedPileStatus();
    });
    connect(ui->btnAddPile,&QPushButton::clicked,this,[=](){
        onAddPileClicked();
    });
    connect(ui->btnRemoteReboot,&QPushButton::clicked,this,[=](){
        qDebug()<<"点击：远程重启(批量)";
        batchPileRestart();
    });
    connect(ui->btnBatchDelete,&QPushButton::clicked,this,[=](){
        qDebug()<<"点击：批量删除";
        batchDeletePile();
    });
    connect(ui->lineEditPileId, &QLineEdit::returnPressed, this, [=](){
        reloadPileList();
    });

    //==================== 订单管理初始化 ====================
    ui->comboOrderStatus->addItem(QStringLiteral("全部状态"));
    ui->comboOrderStatus->addItem(QStringLiteral("预约"));
    ui->comboOrderStatus->addItem(QStringLiteral("充电中"));
    ui->comboOrderStatus->addItem(QStringLiteral("待支付"));
    ui->comboOrderStatus->addItem(QStringLiteral("已完成"));
    ui->dateOrderFrom->setCalendarPopup(true);
    ui->dateOrderTo->setCalendarPopup(true);
    ui->dateOrderFrom->setDate(QDate(2026, 8, 1));
    ui->dateOrderTo->setDate(QDate(2026, 9, 30));

    ui->tableOrder->setColumnCount(9);
    ui->tableOrder->setHorizontalHeaderLabels({
        QStringLiteral("订单号"),
        QStringLiteral("手机号"),
        QStringLiteral("电站"),
        QStringLiteral("电桩"),
        QStringLiteral("状态"),
        QStringLiteral("电量(kWh)"),
        QStringLiteral("金额(元)"),
        QStringLiteral("开始时间"),
        QStringLiteral("操作"),
    });
    ui->tableOrder->verticalHeader()->setVisible(false);
    ui->tableOrder->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableOrder->verticalHeader()->setDefaultSectionSize(44);
    ui->tableOrder->setAlternatingRowColors(true);
    ui->tableOrder->setStyleSheet(ui->tableUser->styleSheet());

    connect(ui->btnOrderQuery, &QPushButton::clicked, this, [=]() {
        reloadOrderList();
    });
    connect(ui->editOrderPhone, &QLineEdit::returnPressed, this, [=]() {
        reloadOrderList();
    });

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resetAllBtnSelect()
{
    auto btns = {ui->btnOverview, ui->btnPile, ui->btnStation, ui->btnUser, ui->btnOrder, ui->btnLog};
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
    QString tip = wantFreeze
        ? "确定要冻结该用户？冻结后用户将无法登录系统。"
        : "确定要解冻该用户，恢复登录权限？";
    auto res = QMessageBox::question(this, "操作确认", tip,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if(res != QMessageBox::Yes)
    {
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
    const int online = pileStat.value(QStringLiteral("闲置")).toInt(0)
                     + pileStat.value(QStringLiteral("预约")).toInt(0)
                     + pileStat.value(QStringLiteral("在用")).toInt(0);
    ui->labPileOnline->setText(QString::number(online));

    // ==========新增：本月营收、累计总营收赋值 ==========
    ui->labMonthIncome->setText(QString::number(d["month_revenue"].toDouble()));
    ui->labTotalIncome->setText(QString::number(d["total_revenue"].toDouble()));
}


//====================电桩模块全部函数====================
void MainWindow::loadStationCombo()
{
    ui->comboStation->clear();
    ui->comboStation->addItem("全部电站");
    // 关键修复：清空状态下拉，防止重复叠加选项！！
    ui->comboPileStatus->clear();
    ui->comboPileStatus->addItem("全部状态");
    ui->comboPileStatus->addItem("闲置");
    ui->comboPileStatus->addItem("预约");
    ui->comboPileStatus->addItem("在用");
    ui->comboPileStatus->addItem("故障");

    QJsonObject resp = m_api->call(QStringLiteral("station.admin.list"));
    qDebug()<<"station.admin.list resp:"<<resp;
    if(!resp["ok"].toBool())
    {
        qDebug()<<"获取电站列表失败:"<<resp["error"].toObject()["message"].toString();
        return;
    }
    QJsonArray items = resp["data"].toObject()["items"].toArray();
    for(auto v : items)
    {
        QJsonObject o = v.toObject();
        QString name = o["name"].toString();
        int sid = o["id"].toInt();
        ui->comboStation->addItem(name, sid);
    }
}

void MainWindow::reloadPileList()
{
    ui->tableWidgetPile->setRowCount(0);
    QJsonObject params;
    QString pileRaw = ui->lineEditPileId->text();
    QString pileNo = pileRaw.trimmed();
    qDebug()<<"[电桩搜索]原始文本:"<<pileRaw;
    qDebug()<<"[电桩搜索]trimmed后:"<<pileNo;

    if (!pileNo.isEmpty())
    {
        params["keyword"] = pileNo;
    }

    QVariant stationData = ui->comboStation->currentData();
    if (stationData.isValid())
    {
        params["station_id"] = stationData.toInt();
    }

    QString statusText = ui->comboPileStatus->currentText();
    if (statusText != "全部状态")
    {
        params["status"] = statusText;
    }

    qDebug()<<"【pile.list请求参数】"<<params;
    QJsonObject resp = m_api->call("pile.list", params);
    qDebug() << "pile.list 返回：" << resp;
    if(resp["ok"].toBool())
    {
        QJsonArray items = resp["data"].toObject()["items"].toArray();
        qDebug()<<"返回电桩数量："<<items.size();
        for(auto item : items)
        {
            addPileRow(item.toObject());
        }
    }
}

void MainWindow::addPileRow(const QJsonObject &obj)
{
    qDebug()<<"单条桩数据:"<<obj;
    int row = ui->tableWidgetPile->rowCount();
    ui->tableWidgetPile->insertRow(row);
    QString pileNo = obj["pile_no"].toString();
    ui->tableWidgetPile->setItem(row,0, new QTableWidgetItem(pileNo));
    ui->tableWidgetPile->setItem(row,1, new QTableWidgetItem(obj["station_name"].toString()));
    ui->tableWidgetPile->setItem(row,2, new QTableWidgetItem(obj["type"].toString()));
    ui->tableWidgetPile->setItem(row,3, new QTableWidgetItem(QString::number(obj["power_kw"].toInt())));
    ui->tableWidgetPile->setItem(row,4, new QTableWidgetItem(obj["status"].toString()));
    ui->tableWidgetPile->setItem(row,5, new QTableWidgetItem(QString::number(obj["charge_count"].toInt())));
    ui->tableWidgetPile->setItem(row,6, new QTableWidgetItem(QString::number(obj["charge_minutes"].toInt())));

    QWidget *container = new QWidget();
    QHBoxLayout *hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(4,2,4,2);
    QPushButton *btnEdit = new QPushButton("编辑");
    QPushButton *btnRestart = new QPushButton("重启");
    QPushButton *btnDel = new QPushButton("删除");
    hlay->addWidget(btnEdit);
    hlay->addWidget(btnRestart);
    hlay->addWidget(btnDel);
    ui->tableWidgetPile->setCellWidget(row,7, container);

    // 修改这里：点击btnEdit直接调用onEditPileBtnClicked
    connect(btnEdit,&QPushButton::clicked,this,[=](){
        qDebug()<<"编辑电桩 pileNo="<<pileNo;
        onEditPileBtnClicked(pileNo);
    });
    connect(btnRestart,&QPushButton::clicked,this,[=](){
        onPileRestart(pileNo);
    });
    connect(btnDel,&QPushButton::clicked,this,[=](){
        auto ret = QMessageBox::question(this,"确认","确定删除该电桩？");
        if(ret == QMessageBox::Yes)
        {
            QJsonObject p;
            p["pile_no"] = pileNo;
            auto r = m_api->call("pile.delete",p);
            if(r["ok"].toBool())
            {
                reloadPileList();
            }
            else
            {
                QMessageBox::warning(this,"失败",r["error"].toObject()["message"].toString());
            }
        }
    });
}

void MainWindow::onPileRestart(const QString &pileNo)
{
    auto res = QMessageBox::question(this,"确认",QString("确定远程重启电桩 %1？").arg(pileNo));
    if(res != QMessageBox::Yes)
        return;
    QJsonObject param;
    param["pile_no"] = pileNo;
    QJsonObject resp = m_api->call("pile.restart", param);
    qDebug()<<"pile.restart resp:"<<resp;
    if(resp["ok"].toBool())
    {
        QMessageBox::information(this,"成功","远程重启指令已下发");
        reloadPileList();
    }
    else
    {
        QString msg = resp["error"].toObject()["message"].toString("操作失败");
        QMessageBox::warning(this,"失败",msg);
    }
}

void MainWindow::on_btnPileQuery_clicked()
{
    qDebug()<<"点击电桩查询";
    reloadPileList();
}

void MainWindow::batchPileRestart()
{
    qDebug() << "执行批量远程重启";
    QSet<QString> pileNoSet;
    auto items = ui->tableWidgetPile->selectedItems();
    for(auto item : items)
    {
        if(item->column() == 0)
        {
            pileNoSet.insert(item->text());
        }
    }
    if(pileNoSet.isEmpty())
    {
        QMessageBox::information(this,"提示","请先勾选要操作的电桩");
        return;
    }
    QStringList pileNos(pileNoSet.begin(), pileNoSet.end());
    qDebug()<<"选中电桩编号："<<pileNos;
    for(const QString& no : pileNos)
    {
        m_api->call("pile.restart", QJsonObject{{"pile_no", no}});
    }
    QMessageBox::information(this,"提示","批量重启指令已下发");
    reloadPileList();
}

void MainWindow::batchDeletePile()
{
    qDebug() << "执行批量删除";
    QSet<QString> pileNoSet;
    auto items = ui->tableWidgetPile->selectedItems();
    for(auto item : items)
    {
        if(item->column() == 0)
        {
            pileNoSet.insert(item->text());
        }
    }
    if(pileNoSet.isEmpty())
    {
        QMessageBox::information(this,"提示","请先勾选要删除的电桩");
        return;
    }
    QStringList pileNos(pileNoSet.begin(), pileNoSet.end());
    qDebug()<<"选中待删除电桩编号："<<pileNos;
    auto ret = QMessageBox::question(this,"确认删除",QString("确定删除选中%1个电桩？").arg(pileNos.size()));
    if(ret != QMessageBox::Yes)
        return;
    for(const QString& no : pileNos)
    {
        m_api->call("pile.delete", QJsonObject{{"pile_no", no}});
    }
    QMessageBox::information(this,"提示","批量删除指令已下发");
    reloadPileList();
}

// 图表
void MainWindow::drawRevenueChartFromJson(const QJsonArray &trendArr)
{
    if (!m_chart) return;
    m_chart->clearGraphs();
    m_chart->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));

    QVector<double> x,y;
    QStringList labels;

    if(!trendArr.isEmpty())
    {
        qDebug()<<"使用服务端真实数据绘图，条数："<<trendArr.size();
        for(const auto& item : trendArr)
        {
            QJsonObject obj = item.toObject();
            QString dateStr = obj["date"].toString();
            double revenueYuan = obj["revenue"].toDouble();
            double revenueWan = revenueYuan / 10000.0;

            labels << dateStr.mid(5);
            x.append(x.size());
            y.append(revenueWan);
        }
    }
    else
    {
        qDebug()<<"服务端无数据，生成"<<m_currentDays<<"天y=0基线，x轴显示1到"<<m_currentDays;

        for(int i=0;i<m_currentDays;i++)
        {
            x.append(i);
            y.append(0.0);
            labels << QString::number(i + 1); // 1、2、3...30
        }
    }

    QCPGraph* graph = m_chart->addGraph();
    graph->setData(x,y);
    graph->setPen(QPen(QColor(0x4078d8),2));
    QCPScatterStyle circleStyle(QCPScatterStyle::ssCircle);
    circleStyle.setSize(4);
    graph->setScatterStyle(circleStyle);

    QSharedPointer<QCPAxisTickerText> ticker(new QCPAxisTickerText());
    for(int i=0;i<x.size();i++)
    {
        ticker->addTick(x[i], labels[i]);
    }
    m_chart->xAxis->setTicker(ticker.template staticCast<QCPAxisTicker>());

    m_chart->xAxis->setLabel("日期");
    m_chart->yAxis->setLabel("营收(万元)");
    m_chartTitle->setText("营收趋势");

    m_chart->rescaleAxes();
    m_chart->replot();
}

void MainWindow::on_btnRefresh_clicked()
{
    if(m_refreshBusy)
    {
        QMessageBox::information(this,"提示","正在请求，请稍等");
        return;
    }
    reloadOverviewStat();
}

// 返回总览
void MainWindow::on_btnBackHome_clicked()
{
    resetAllBtnSelect();
    ui->btnOverview->setProperty("selected", true);
    ui->btnOverview->setStyleSheet(ui->btnOverview->styleSheet());
    ui->stackedWidget->setCurrentIndex(0);
    reloadOverviewStat(); //切回去立刻刷新统计卡片
}

void MainWindow::on_btnShift_clicked()
{
    qDebug()<<"点击切换按钮，当前m_currentDays="<<m_currentDays;
    if(m_refreshBusy) return;

    if(m_currentDays ==7)
    {
        m_currentDays = 30;
        ui->btnShift->setText("查看近7日");
    }
    else
    {
        m_currentDays =7;
        ui->btnShift->setText("查看近30日");
    }
    qDebug()<<"切换后 m_currentDays="<<m_currentDays;

    m_refreshBusy = true;
    QJsonObject param;
    param["days"] = m_currentDays;
    QJsonObject resp = m_api->call("stats.overview", param);
    m_refreshBusy = false;

    qDebug()<<"api返回 ok="<<resp["ok"].toBool();
    if(resp["ok"].toBool())
    {
        QJsonArray trendArr = resp["data"].toObject()["revenue_trend"].toArray();
        qDebug()<<"trend数组长度="<<trendArr.size();
        drawRevenueChartFromJson(trendArr);
    }
    else
    {
        QMessageBox::warning(this,"错误","刷新营收图表失败，接口请求失败");
        drawRevenueChartFromJson(QJsonArray());
    }
}

void MainWindow::onEditPileBtnClicked(const QString &pileNo)
{
    int row = getRowByPileNo(pileNo);
    if(row < 0)
    {
        QMessageBox::warning(this,"错误","找不到该电桩行");
        return;
    }

    // 列2=type, 列3=power_kw, 列4=status
    QString oldType = ui->tableWidgetPile->item(row,2)->text();
    double oldPower = ui->tableWidgetPile->item(row,3)->text().toDouble();
    QString oldStatus = ui->tableWidgetPile->item(row,4)->text();

    PileEditDialog dlg(this);
    dlg.setData(pileNo, oldType, oldPower, oldStatus);

    int ret = dlg.exec();
    if(ret == QDialog::Accepted)
    {
        QJsonObject reqBody = dlg.getUpdateParams();
        if(reqBody.isEmpty())
        {
            QMessageBox::information(this,"提示","没有修改任何字段");
            return;
        }
        reqBody["pile_no"] = pileNo;

        QJsonObject resp = m_api->call("pile.update", reqBody);
        if(resp["ok"].toBool())
        {
            QMessageBox::information(this,"成功","电桩信息修改完成");
            reloadPileList();
        }
        else
        {
            QString errMsg = resp["error"].toObject()["message"].toString("修改失败");
            QMessageBox::warning(this,"修改失败", errMsg);
        }
    }
}

int MainWindow::getRowByPileNo(const QString &pileNo)
{
    for(int i = 0; i < ui->tableWidgetPile->rowCount(); i++)
    {
        // 列0 = pile_no 桩号
        QTableWidgetItem *item = ui->tableWidgetPile->item(i,0);
        if(item && item->text() == pileNo)
        {
            return i;
        }
    }
    return -1;
}

QString MainWindow::getSingleSelectedPileNo() const
{
    QSet<QString> pileNoSet;
    for (QTableWidgetItem *item : ui->tableWidgetPile->selectedItems()) {
        if (item->column() == 0) {
            pileNoSet.insert(item->text());
        }
    }
    if (pileNoSet.size() != 1) {
        return {};
    }
    return *pileNoSet.begin();
}

void MainWindow::goToSelectedPileStatus()
{
    const QString pileNo = getSingleSelectedPileNo();
    if (pileNo.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在表格中选中一条电桩记录（只能选一条）"));
        return;
    }

    ui->lineEditPileId->setText(pileNo);
    ui->comboStation->setCurrentIndex(0);
    ui->comboPileStatus->setCurrentText(QStringLiteral("全部状态"));
    reloadPileList();

    const int row = getRowByPileNo(pileNo);
    if (row >= 0) {
        ui->tableWidgetPile->selectRow(row);
    }

    QJsonObject resp = m_api->call(QStringLiteral("pile.detail"), QJsonObject{{"pile_no", pileNo}});
    if (!resp.value("ok").toBool()) {
        const QString errMsg = resp.value("error").toObject().value("message").toString(
            QStringLiteral("获取电桩详情失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        return;
    }

    PileStatusDialog dlg(this);
    dlg.setDetail(resp.value("data").toObject());
    dlg.exec();
}

void MainWindow::onAddPileClicked()
{
    PileAddDialog dlg(this);
    if (!dlg.loadStations(m_api)) {
        return;
    }

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QJsonObject resp = m_api->call(QStringLiteral("pile.create"), dlg.getCreateParams());
    if (resp.value("ok").toBool()) {
        const QJsonObject data = resp.value("data").toObject();
        const QString pileNo = data.value("pile_no").toString();
        const int stationId = data.value("station_id").toInt();
        QMessageBox::information(this, QStringLiteral("成功"),
                                 QStringLiteral("已新增电桩：%1").arg(pileNo));
        ui->lineEditPileId->clear();
        ui->comboPileStatus->setCurrentText(QStringLiteral("全部状态"));
        for (int i = 0; i < ui->comboStation->count(); ++i) {
            if (ui->comboStation->itemData(i).toInt() == stationId) {
                ui->comboStation->setCurrentIndex(i);
                break;
            }
        }
        reloadPileList();
        const int row = getRowByPileNo(pileNo);
        if (row >= 0) {
            ui->tableWidgetPile->selectRow(row);
        }
    } else {
        const QString errMsg = resp.value("error").toObject().value("message").toString(
            QStringLiteral("新增失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
    }
}

void MainWindow::reloadOrderList()
{
    ui->tableOrder->setRowCount(0);

    QJsonObject params;
    params["limit"] = 50;

    const QString statusText = ui->comboOrderStatus->currentText();
    if (statusText != QStringLiteral("全部状态")) {
        params["status"] = statusText;
    }

    const QString phone = ui->editOrderPhone->text().trimmed();
    if (!phone.isEmpty()) {
        params["phone"] = phone;
    }

    params["date_from"] = ui->dateOrderFrom->date().toString(QStringLiteral("yyyy-MM-dd"));
    params["date_to"] = ui->dateOrderTo->date().toString(QStringLiteral("yyyy-MM-dd"));

    const QJsonObject resp = m_api->call(QStringLiteral("order.list"), params);
    if (!resp.value("ok").toBool()) {
        const QString errMsg = resp.value("error").toObject().value("message").toString(
            QStringLiteral("获取订单列表失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        return;
    }

    const QJsonArray items = resp.value("data").toObject().value("items").toArray();
    for (const QJsonValue &value : items) {
        addOrderRow(value.toObject());
    }
}

void MainWindow::addOrderRow(const QJsonObject &obj)
{
    const int row = ui->tableOrder->rowCount();
    ui->tableOrder->insertRow(row);

    const QString orderNo = obj.value("order_no").toString();
    const QString status = obj.value("status").toString();

    auto *orderItem = new QTableWidgetItem(orderNo);
    orderItem->setFlags(orderItem->flags() & ~Qt::ItemIsEditable);
    orderItem->setData(Qt::UserRole, obj);
    ui->tableOrder->setItem(row, 0, orderItem);

    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    ui->tableOrder->setItem(row, 1, mkReadOnly(obj.value("phone").toString()));
    ui->tableOrder->setItem(row, 2, mkReadOnly(obj.value("station_name").toString()));
    ui->tableOrder->setItem(row, 3, mkReadOnly(obj.value("pile_no").toString()));
    ui->tableOrder->setItem(row, 4, mkReadOnly(status));
    ui->tableOrder->setItem(row, 5, mkReadOnly(QString::number(obj.value("kwh").toDouble())));
    ui->tableOrder->setItem(row, 6, mkReadOnly(QString::number(obj.value("amount").toDouble())));
    ui->tableOrder->setItem(row, 7, mkReadOnly(obj.value("start_at").toString()));

    QWidget *container = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(4, 2, 4, 2);

    QPushButton *btnDetail = new QPushButton(QStringLiteral("详情"));
    layout->addWidget(btnDetail);
    connect(btnDetail, &QPushButton::clicked, this, [=]() {
        onOrderDetailClicked(obj);
    });

    if (status == QStringLiteral("待支付")) {
        QPushButton *btnSettle = new QPushButton(QStringLiteral("代结算"));
        layout->addWidget(btnSettle);
        connect(btnSettle, &QPushButton::clicked, this, [=]() {
            onOrderAdminSettle(orderNo);
        });
    }

    ui->tableOrder->setCellWidget(row, 8, container);
}

void MainWindow::onOrderDetailClicked(const QJsonObject &order)
{
    OrderDetailDialog dlg(this);
    dlg.setOrder(order);
    connect(&dlg, &OrderDetailDialog::adminSettleRequested, this, [this, &dlg](const QString &orderNo) {
        dlg.accept();
        onOrderAdminSettle(orderNo);
    });
    dlg.exec();
}

void MainWindow::onOrderAdminSettle(const QString &orderNo)
{
    if (orderNo.isEmpty()) {
        return;
    }

    const auto ret = QMessageBox::question(
        this,
        QStringLiteral("代结算确认"),
        QStringLiteral("确定代用户结算订单 %1 吗？").arg(orderNo),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    const QJsonObject resp = m_api->call(
        QStringLiteral("order.admin.settle"),
        QJsonObject{{"order_no", orderNo}});
    if (resp.value("ok").toBool()) {
        QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("代结算完成"));
        reloadOrderList();
        reloadOverviewStat();
    } else {
        const QString errMsg = resp.value("error").toObject().value("message").toString(
            QStringLiteral("代结算失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
    }
}

