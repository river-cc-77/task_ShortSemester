#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "apiclient.h"
#include "announcementeditdialog.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSet>
#include <QDate>
#include <QTableWidgetItem>
#include <QVector>
#include <QLabel>
#include <QFrame>
#include <QBrush>
#include <QComboBox>

namespace {

void configureTable(QTableWidget *t,
                    const QVector<int> &stretchCols,
                    const QVector<int> &contentCols,
                    int opCol = -1, int opWidth = 0)
{
    QHeaderView *header = t->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    for (int c : stretchCols) {
        header->setSectionResizeMode(c, QHeaderView::Stretch);
    }
    for (int c : contentCols) {
        header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    if (opCol >= 0) {
        header->setSectionResizeMode(opCol, QHeaderView::Fixed);
        t->setColumnWidth(opCol, opWidth);
    }
    header->setStretchLastSection(false);
}

void populateLogActionCombo(QComboBox *combo)
{
    combo->clear();
    const QStringList actions = {
        QStringLiteral("全部操作"),
        QStringLiteral("登录"),
        QStringLiteral("新增电站"),
        QStringLiteral("修改电站"),
        QStringLiteral("删除电站"),
        QStringLiteral("新增电桩"),
        QStringLiteral("修改电桩"),
        QStringLiteral("删除电桩"),
        QStringLiteral("远程重启电桩"),
        QStringLiteral("冻结用户"),
        QStringLiteral("解冻用户"),
        QStringLiteral("代结算"),
        QStringLiteral("新增公告"),
        QStringLiteral("修改公告"),
        QStringLiteral("删除公告"),
    };
    combo->addItems(actions);
}

void populateLogTargetTypeCombo(QComboBox *combo)
{
    combo->clear();
    combo->addItem(QStringLiteral("全部对象"), QString());
    combo->addItem(QStringLiteral("电站"), QStringLiteral("station"));
    combo->addItem(QStringLiteral("电桩"), QStringLiteral("pile"));
    combo->addItem(QStringLiteral("用户"), QStringLiteral("user"));
    combo->addItem(QStringLiteral("订单"), QStringLiteral("order"));
    combo->addItem(QStringLiteral("公告"), QStringLiteral("announcement"));
    combo->addItem(QStringLiteral("管理员"), QStringLiteral("admin"));
}

// 表格操作列按钮：统一最小尺寸、手型光标与语义配色（btnType）
QPushButton *makeOpButton(const QString &text, const char *btnType, int minWidth)
{
    auto *btn = new QPushButton(text);
    btn->setProperty("btnType", btnType);
    btn->setMinimumWidth(minWidth);
    btn->setMinimumHeight(30);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

// 操作列容器：按钮组整体居中，四周留白，按钮之间保留 6px 间距
QWidget *makeOpCell(const QList<QPushButton *> &btns)
{
    auto *container = new QWidget();
    auto *lay = new QHBoxLayout(container);
    lay->setContentsMargins(8, 3, 8, 3);
    lay->setSpacing(6);
    lay->addStretch(1);
    for (auto *b : btns) {
        lay->addWidget(b);
    }
    lay->addStretch(1);
    return container;
}

} // namespace

MainWindow::MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_api(api)
{
    Q_UNUSED(admin);
    ui->setupUi(this);
    this->setWindowTitle("充电桩管理系统");

    // ========= 侧边栏品牌区（样式见 theme.qss） =========
    auto *brand = new QLabel(QStringLiteral("充电桩管理系统"), ui->sideBarWidget);
    brand->setObjectName(QStringLiteral("sidebarBrand"));
    ui->verticalLayout->insertWidget(0, brand);
    auto *brandSub = new QLabel(QStringLiteral("CHARGE ADMIN CONSOLE"), ui->sideBarWidget);
    brandSub->setObjectName(QStringLiteral("sidebarBrandSub"));
    ui->verticalLayout->insertWidget(1, brandSub);
    auto *statusLabel = new QLabel(QStringLiteral("● 系统运行正常"), ui->sideBarWidget);
    statusLabel->setObjectName(QStringLiteral("sidebarStatus"));
    ui->verticalLayout->insertWidget(2, statusLabel);
    auto *brandDivider = new QFrame(ui->sideBarWidget);
    brandDivider->setObjectName(QStringLiteral("sidebarDivider"));
    brandDivider->setFrameShape(QFrame::NoFrame);
    brandDivider->setAttribute(Qt::WA_StyledBackground, true);
    ui->verticalLayout->insertWidget(3, brandDivider);
    auto *versionLabel = new QLabel(QStringLiteral("ADMIN PLATFORM v2.0"), ui->sideBarWidget);
    versionLabel->setObjectName(QStringLiteral("sidebarVersion"));
    ui->verticalLayout->addWidget(versionLabel);

    // ========= 内容区工具条卡片化（样式见 theme.qss） =========
    const QList<QWidget *> toolbarCards = {
        ui->widget_4, ui->widget_6, ui->widget_stationFilter, ui->widget,
        ui->widget_logFilter, ui->widget_orderFilter, ui->widget_announcementFilter,
        ui->widget_2, ui->widget_top_bar, ui->widget_chart
    };
    for (QWidget *w : toolbarCards) {
        w->setAttribute(Qt::WA_StyledBackground, true);
        if (QLayout *lay = w->layout()) {
            lay->setContentsMargins(14, 10, 14, 10);
        }
    }

    // 统一主界面操作按钮的语义配色与手型光标（样式见 theme.qss）
    auto markBtn = [](QPushButton *b, const char *cls) {
        b->setProperty("class", cls);
        b->setCursor(Qt::PointingHandCursor);
    };
    markBtn(ui->btnRefresh, "primary");
    markBtn(ui->btnChart, "primary");
    markBtn(ui->btnPileQuery, "primary");
    markBtn(ui->btnAddPile, "primary");
    markBtn(ui->btnRemoteReboot, "primary");
    markBtn(ui->btnBatchDelete, "danger");
    markBtn(ui->btnStationQuery, "primary");
    markBtn(ui->btnStationAdd, "primary");
    markBtn(ui->btnSearch, "primary");
    markBtn(ui->btnLogQuery, "primary");
    markBtn(ui->btnOrderQuery, "primary");
    markBtn(ui->btnBackHome, "primary");
    markBtn(ui->btnAnnouncementAdd, "primary");
    markBtn(ui->btnAnnouncementRefresh, "primary");
    ui->btnGoPileStatus->setCursor(Qt::PointingHandCursor);
    ui->btnShift->setCursor(Qt::PointingHandCursor);
    ui->btnAnnouncement->setCursor(Qt::PointingHandCursor);
    ui->btnSearch->setText(QStringLiteral("查询"));
    ui->btnSearch->setMinimumWidth(100);

    resetAllBtnSelect();
    ui->btnOverview->setProperty("selected", true);
    refreshBtnStyle(ui->btnOverview);
    ui->stackedWidget->setCurrentIndex(0);

    // ========= 侧边栏切换 =========
    connect(ui->btnOverview,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnOverview->setProperty("selected", true);
        refreshBtnStyle(ui->btnOverview);
        ui->stackedWidget->setCurrentIndex(0);
    });
    connect(ui->btnPile,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnPile->setProperty("selected", true);
        refreshBtnStyle(ui->btnPile);
        ui->stackedWidget->setCurrentIndex(1);
    });
    connect(ui->btnStation,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnStation->setProperty("selected", true);
        refreshBtnStyle(ui->btnStation);
        ui->stackedWidget->setCurrentIndex(2);
    });
    connect(ui->btnUser,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnUser->setProperty("selected", true);
        refreshBtnStyle(ui->btnUser);
        ui->stackedWidget->setCurrentIndex(3);
    });
    connect(ui->btnOrder,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnOrder->setProperty("selected", true);
        refreshBtnStyle(ui->btnOrder);
        ui->stackedWidget->setCurrentIndex(6);
    });
    connect(ui->btnLog,&QPushButton::clicked,this,[=](){
        resetAllBtnSelect();
        ui->btnLog->setProperty("selected", true);
        refreshBtnStyle(ui->btnLog);
        ui->stackedWidget->setCurrentIndex(4);
    });
    connect(ui->btnAnnouncement, &QPushButton::clicked, this, [=]() {
        resetAllBtnSelect();
        ui->btnAnnouncement->setProperty("selected", true);
        refreshBtnStyle(ui->btnAnnouncement);
        ui->stackedWidget->setCurrentIndex(7);
    });

    // ========= 营收趋势页面 index=5 =========
    connect(ui->btnChart,&QPushButton::clicked,this,[=](){
        if (m_refreshBusy) {
            return;
        }
        resetAllBtnSelect();
        ui->stackedWidget->setCurrentIndex(5);
        m_currentDays =7;
        ui->btnShift->setText("查看近30日");
        m_refreshBusy = true;
        QJsonObject param;
        param["days"] = m_currentDays;
        QJsonObject resp = m_api->call("stats.overview", param);
        m_refreshBusy = false;
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

    // ========= 初始化图表控件 =========
    m_chart = new QCustomPlot();
    QVBoxLayout* lay = new QVBoxLayout(ui->widget_chart);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->addWidget(m_chart);
    m_currentDays =7;
    m_chartTitle = new QCPTextElement(m_chart, "营收趋势 / 万元", QFont("sans",11,QFont::Bold));
    m_chart->plotLayout()->insertRow(0);
    m_chart->plotLayout()->addElement(0,0, m_chartTitle);

    // 图表配色与全局主题统一
    m_chart->setBackground(QBrush(QColor("#ffffff")));
    m_chart->axisRect()->setBackground(QBrush(QColor("#ffffff")));
    m_chart->xAxis->setBasePen(QPen(QColor("#cdd6e4")));
    m_chart->yAxis->setBasePen(QPen(QColor("#cdd6e4")));
    m_chart->xAxis->setTickPen(QPen(QColor("#cdd6e4")));
    m_chart->yAxis->setTickPen(QPen(QColor("#cdd6e4")));
    m_chart->xAxis->setSubTickPen(QPen(QColor("#e3e9f2")));
    m_chart->yAxis->setSubTickPen(QPen(QColor("#e3e9f2")));
    m_chart->xAxis->setTickLabelColor(QColor("#5a6780"));
    m_chart->yAxis->setTickLabelColor(QColor("#5a6780"));
    m_chart->xAxis->setLabelColor(QColor("#6b7688"));
    m_chart->yAxis->setLabelColor(QColor("#6b7688"));
    m_chart->xAxis->grid()->setPen(QPen(QColor("#eef2f8"), 1, Qt::DashLine));
    m_chart->yAxis->grid()->setPen(QPen(QColor("#eef2f8"), 1, Qt::DashLine));
    m_chartTitle->setTextColor(QColor("#22304a"));

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
    configureTable(ui->tableUser, {2}, {0, 1, 3, 4, 5}, 6, 90);

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
        else if(index == 2)
        {
            reloadStationList();
        }
        else if(index == 4)
        {
            reloadOperationLogList();
        }
        else if(index == 7)
        {
            reloadAnnouncementList();
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

    //==================== 电桩表格初始化 ====================
    ui->tableWidgetPile->setColumnCount(8);
    QStringList pileHeaders = {
        "电桩编号",
        "所属电站",
        "电桩类型",
        "功率(kW)",
        "状态",
        "累计充电次数",
        "充电时长(分)",
        "操作"
    };
    ui->tableWidgetPile->setHorizontalHeaderLabels(pileHeaders);
    ui->tableWidgetPile->verticalHeader()->setVisible(false);
    configureTable(ui->tableWidgetPile, {1}, {0, 2, 3, 4, 5, 6}, 7, 212);
    ui->tableWidgetPile->verticalHeader()->setDefaultSectionSize(44);
    ui->tableWidgetPile->setAlternatingRowColors(true);

    // 初始化电桩状态下拉框
    ui->comboPileStatus->addItem("全部状态");
    ui->comboPileStatus->addItem("闲置");
    ui->comboPileStatus->addItem("预约");
    ui->comboPileStatus->addItem("在用");
    ui->comboPileStatus->addItem("故障");

    // 查询按钮使用 on_btnPileQuery_clicked 自动槽

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
    {
        const QDate today = QDate::currentDate();
        ui->dateOrderFrom->setDate(today.addMonths(-2));
        ui->dateOrderTo->setDate(today.addMonths(1));
    }

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
    configureTable(ui->tableOrder, {2, 7}, {0, 1, 3, 4, 5, 6}, 8, 170);
    ui->tableOrder->verticalHeader()->setDefaultSectionSize(44);
    ui->tableOrder->setAlternatingRowColors(true);

    connect(ui->btnOrderQuery, &QPushButton::clicked, this, [=]() {
        reloadOrderList();
    });
    connect(ui->editOrderPhone, &QLineEdit::returnPressed, this, [=]() {
        reloadOrderList();
    });

    //==================== 电站管理初始化 ====================
    ui->tableStation->setColumnCount(9);
    ui->tableStation->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("站名"),
        QStringLiteral("地址"),
        QStringLiteral("电价"),
        QStringLiteral("电桩数"),
        QStringLiteral("闲置桩(预警)"),
        QStringLiteral("可用率"),
        QStringLiteral("创建时间"),
        QStringLiteral("操作"),
    });
    ui->tableStation->verticalHeader()->setVisible(false);
    configureTable(ui->tableStation, {2}, {0, 1, 3, 4, 5, 6, 7}, 8, 260);
    ui->tableStation->verticalHeader()->setDefaultSectionSize(44);
    ui->tableStation->setAlternatingRowColors(true);

    connect(ui->btnStationQuery, &QPushButton::clicked, this, [=]() {
        reloadStationList();
    });
    connect(ui->editStationKeyword, &QLineEdit::returnPressed, this, [=]() {
        reloadStationList();
    });
    connect(ui->btnStationAdd, &QPushButton::clicked, this, &MainWindow::onAddStationClicked);

    //==================== 操作日志初始化 ====================
    populateLogActionCombo(ui->comboLogAction);

    m_comboLogTargetType = new QComboBox(ui->widget_logFilter);
    m_comboLogTargetType->setMinimumWidth(110);
    populateLogTargetTypeCombo(m_comboLogTargetType);
    ui->horizontalLayout_logFilter->insertWidget(1, m_comboLogTargetType);

    m_editLogKeyword = new QLineEdit(ui->widget_logFilter);
    m_editLogKeyword->setPlaceholderText(QStringLiteral("关键字(管理员/对象/详情)"));
    m_editLogKeyword->setMinimumWidth(180);
    ui->horizontalLayout_logFilter->insertWidget(5, m_editLogKeyword);

    ui->dateLogFrom->setCalendarPopup(true);
    ui->dateLogTo->setCalendarPopup(true);
    {
        const QDate today = QDate::currentDate();
        ui->dateLogFrom->setDate(today.addMonths(-2));
        ui->dateLogTo->setDate(today.addMonths(1));
    }

    ui->tableLog->setColumnCount(6);
    ui->tableLog->setHorizontalHeaderLabels({
        QStringLiteral("时间"),
        QStringLiteral("管理员"),
        QStringLiteral("操作"),
        QStringLiteral("对象类型"),
        QStringLiteral("对象ID"),
        QStringLiteral("详情"),
    });
    ui->tableLog->verticalHeader()->setVisible(false);
    configureTable(ui->tableLog, {5}, {0, 1, 2, 3, 4}, -1, 0);
    ui->tableLog->verticalHeader()->setDefaultSectionSize(40);
    ui->tableLog->setAlternatingRowColors(true);

    connect(ui->btnLogQuery, &QPushButton::clicked, this, [=]() {
        reloadOperationLogList();
    });
    connect(m_editLogKeyword, &QLineEdit::returnPressed, this, [=]() {
        reloadOperationLogList();
    });
    connect(ui->comboLogAction, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        reloadOperationLogList();
    });
    connect(m_comboLogTargetType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        reloadOperationLogList();
    });

    //==================== 公告管理初始化 ====================
    ui->tableAnnouncement->setColumnCount(5);
    ui->tableAnnouncement->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("标题"),
        QStringLiteral("状态"),
        QStringLiteral("发布时间"),
        QStringLiteral("操作"),
    });
    ui->tableAnnouncement->verticalHeader()->setVisible(false);
    ui->tableAnnouncement->setEditTriggers(QAbstractItemView::NoEditTriggers);
    configureTable(ui->tableAnnouncement, {1}, {0, 2, 3}, 4, 180);
    ui->tableAnnouncement->verticalHeader()->setDefaultSectionSize(44);
    ui->tableAnnouncement->setAlternatingRowColors(true);

    connect(ui->btnAnnouncementAdd, &QPushButton::clicked, this, &MainWindow::onAddAnnouncementClicked);
    connect(ui->btnAnnouncementRefresh, &QPushButton::clicked, this, &MainWindow::reloadAnnouncementList);

    m_cardPileStatus = new QWidget(ui->page_0);
    m_cardPileStatus->setObjectName(QStringLiteral("cardPileStatus"));
    auto *pileStatusLay = new QVBoxLayout(m_cardPileStatus);
    pileStatusLay->setContentsMargins(16, 12, 16, 12);
    auto *pileStatusTitle = new QLabel(QStringLiteral("电桩状态分布与健康度"), m_cardPileStatus);
    pileStatusTitle->setObjectName(QStringLiteral("labPileStatusTitle"));
    m_labPileStatusDetail = new QLabel(m_cardPileStatus);
    m_labPileStatusDetail->setWordWrap(true);
    m_labPileHealth = new QLabel(m_cardPileStatus);
    pileStatusLay->addWidget(pileStatusTitle);
    pileStatusLay->addWidget(m_labPileStatusDetail);
    pileStatusLay->addWidget(m_labPileHealth);
    ui->gridLayout->addWidget(m_cardPileStatus, 4, 0, 1, 2);

    setupPagination();
    reloadUserList("");
    reloadOverviewStat();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resetAllBtnSelect()
{
    auto btns = {ui->btnOverview, ui->btnPile, ui->btnStation, ui->btnUser,
                 ui->btnOrder, ui->btnAnnouncement, ui->btnLog};
    for(auto btn : btns)
    {
        btn->setProperty("selected", false);
        refreshBtnStyle(btn);
    }
}

void MainWindow::refreshBtnStyle(QPushButton *btn)
{
    btn->style()->unpolish(btn);
    btn->style()->polish(btn);
    btn->update();
}

void MainWindow::reloadUserList(const QString &keyword)
{
    m_userPager.page = 0;
    m_userItems = QJsonArray();
#if 1
// ========== 真实后端 user.admin.list ==========
    QJsonObject param;
    if (!keyword.isEmpty())
        param["phone_keyword"] = keyword;
    QJsonObject resp = m_api->call("user.admin.list", param);
    if (!resp["ok"].toBool())
    {
        QMessageBox::warning(this, "错误", "获取用户列表失败");
        renderUserPage();
        return;
    }
    QJsonObject dataObj = resp["data"].toObject();
    m_userItems = dataObj["items"].toArray();
    if (m_userItems.isEmpty() && !keyword.isEmpty())
    {
        QMessageBox::information(this, "提示", "未找到相关用户");
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
        m_userItems.append(o);
    }
#endif
    renderUserPage();
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
    reloadUserList(ui->editSearchPhone->text().trimmed());
}

void MainWindow::addUserRow(const QJsonObject &userObj)
{
    int row = ui->tableUser->rowCount();
    ui->tableUser->insertRow(row);
    int uid = userObj["user_id"].toInt();
    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };
    ui->tableUser->setItem(row, 0, mkReadOnly(QString::number(uid)));
    ui->tableUser->setItem(row, 1, mkReadOnly(userObj["phone"].toString()));
    ui->tableUser->setItem(row, 2, mkReadOnly(userObj["nickname"].toString()));
    ui->tableUser->setItem(row, 3, mkReadOnly(QString::number(userObj["balance"].toDouble())));
    ui->tableUser->setItem(row, 4, mkReadOnly(userObj["created_at"].toString()));
    QString statusText = userObj["status"].toString();
    bool isFrozen = (statusText == QStringLiteral("冻结"));
    ui->tableUser->setItem(row, 5, mkReadOnly(statusText));
    QPushButton *opBtn = makeOpButton(isFrozen ? QStringLiteral("解冻") : QStringLiteral("冻结"),
                                      isFrozen ? "success" : "warning", 64);
    ui->tableUser->setCellWidget(row, 6, makeOpCell({opBtn}));
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

    const int pileTotal = d.value(QStringLiteral("pile_total")).toInt(
        pileStat.value(QStringLiteral("闲置")).toInt(0)
        + pileStat.value(QStringLiteral("预约")).toInt(0)
        + pileStat.value(QStringLiteral("在用")).toInt(0)
        + pileStat.value(QStringLiteral("故障")).toInt(0));
    const double healthRate = d.value(QStringLiteral("pile_health_rate")).toDouble(
        pileTotal > 0
            ? (pileTotal - pileStat.value(QStringLiteral("故障")).toInt(0)) / static_cast<double>(pileTotal)
            : 1.0);
    updatePileStatusOverview(pileStat, pileTotal, healthRate);
}


//====================电桩模块全部函数====================
void MainWindow::loadStationCombo()
{
    ui->comboStation->clear();
    ui->comboStation->addItem("全部电站");

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
    m_pilePager.page = 0;
    m_pileItems = QJsonArray();
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
    if (!resp["ok"].toBool()) {
        const QString errMsg = resp["error"].toObject()["message"].toString(
            QStringLiteral("获取电桩列表失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        renderPilePage();
        return;
    }
    m_pileItems = resp["data"].toObject()["items"].toArray();
    qDebug()<<"返回电桩数量："<<m_pileItems.size();
    renderPilePage();
}

void MainWindow::addPileRow(const QJsonObject &obj)
{
    qDebug()<<"单条桩数据:"<<obj;
    int row = ui->tableWidgetPile->rowCount();
    ui->tableWidgetPile->insertRow(row);
    const QString pileNo = obj["pile_no"].toString();
    const QString pileStatus = obj["status"].toString();

    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    ui->tableWidgetPile->setItem(row, 0, mkReadOnly(pileNo));
    ui->tableWidgetPile->setItem(row, 1, mkReadOnly(obj["station_name"].toString()));
    ui->tableWidgetPile->setItem(row, 2, mkReadOnly(obj["type"].toString()));
    ui->tableWidgetPile->setItem(row, 3, mkReadOnly(QString::number(obj["power_kw"].toDouble(), 'f', 1)));
    ui->tableWidgetPile->setItem(row, 4, mkReadOnly(pileStatus));
    ui->tableWidgetPile->setItem(row, 5, mkReadOnly(QString::number(obj["charge_count"].toInt())));
    ui->tableWidgetPile->setItem(row, 6, mkReadOnly(QString::number(obj["charge_minutes"].toInt())));

    QPushButton *btnEdit = makeOpButton(QStringLiteral("编辑"), "primary", 56);
    QPushButton *btnRestart = nullptr;
    QPushButton *btnDel = makeOpButton(QStringLiteral("删除"), "danger", 56);
    QList<QPushButton*> opBtns{btnEdit};
    if (pileStatus == QStringLiteral("故障")) {
        btnRestart = makeOpButton(QStringLiteral("重启"), "warning", 56);
        opBtns << btnRestart;
    }
    opBtns << btnDel;
    ui->tableWidgetPile->setCellWidget(row, 7, makeOpCell(opBtns));

    // 修改这里：点击btnEdit直接调用onEditPileBtnClicked
    connect(btnEdit,&QPushButton::clicked,this,[=](){
        qDebug()<<"编辑电桩 pileNo="<<pileNo;
        onEditPileBtnClicked(pileNo);
    });
    if (btnRestart) {
        connect(btnRestart,&QPushButton::clicked,this,[=](){
            onPileRestart(pileNo);
        });
    }
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
    if (pileNoSet.isEmpty()) {
        QMessageBox::information(this,"提示","请先勾选要操作的电桩");
        return;
    }
    const auto confirm = QMessageBox::question(
        this, QStringLiteral("确认"),
        QStringLiteral("确定远程重启选中的 %1 个电桩吗？").arg(pileNoSet.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }
    QStringList pileNos(pileNoSet.begin(), pileNoSet.end());
    qDebug()<<"选中电桩编号："<<pileNos;
    int okCount = 0;
    int failCount = 0;
    QString lastErr;
    for (const QString &no : pileNos) {
        const QJsonObject resp = m_api->call("pile.restart", QJsonObject{{"pile_no", no}});
        if (resp["ok"].toBool()) {
            ++okCount;
        } else {
            ++failCount;
            lastErr = resp["error"].toObject()["message"].toString(QStringLiteral("重启失败"));
        }
    }
    reloadPileList();
    if (failCount == 0) {
        QMessageBox::information(this, QStringLiteral("成功"),
                                 QStringLiteral("批量重启指令已下发（%1 个）").arg(okCount));
    } else {
        QMessageBox::warning(this, QStringLiteral("部分失败"),
                             QStringLiteral("成功 %1 个，失败 %2 个。最近错误：%3")
                                 .arg(okCount).arg(failCount).arg(lastErr));
    }
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
    int okCount = 0;
    int failCount = 0;
    QString lastErr;
    for(const QString& no : pileNos)
    {
        const QJsonObject r = m_api->call(QStringLiteral("pile.delete"), QJsonObject{{QStringLiteral("pile_no"), no}});
        if (r.value(QStringLiteral("ok")).toBool()) {
            ++okCount;
        } else {
            ++failCount;
            lastErr = r.value(QStringLiteral("error")).toObject()
                          .value(QStringLiteral("message")).toString(QStringLiteral("删除失败"));
        }
    }
    reloadPileList();
    if (failCount == 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("已成功删除 %1 个电桩").arg(okCount));
    } else if (okCount == 0) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("删除失败：%1").arg(lastErr));
    } else {
        QMessageBox::warning(this, QStringLiteral("部分失败"),
                             QStringLiteral("成功 %1 个，失败 %2 个。最近错误：%3")
                                 .arg(okCount).arg(failCount).arg(lastErr));
    }
}

// 图表
void MainWindow::drawRevenueChartFromJson(const QJsonArray &trendArr)
{
    if (!m_chart) return;
    m_chart->clearGraphs();
    m_chart->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));

    if (trendArr.isEmpty()) {
        qDebug() << "revenue_trend 为空，显示暂无数据";
        if (m_chartTitle) {
            m_chartTitle->setText(QStringLiteral("营收趋势 / 万元（暂无数据）"));
        }
        m_chart->xAxis->setLabel(QStringLiteral("日期"));
        m_chart->yAxis->setLabel(QStringLiteral("营收(万元)"));
        m_chart->xAxis->setRange(0, 1);
        m_chart->yAxis->setRange(0, 1);
        m_chart->replot();
        return;
    }

    QVector<double> x, y;
    QStringList labels;

    qDebug() << "使用服务端趋势数据绘图，条数：" << trendArr.size();
    for (const auto &item : trendArr) {
        const QJsonObject obj = item.toObject();
        const QString dateStr = obj.value(QStringLiteral("date")).toString();
        if (dateStr.isEmpty()) {
            continue;
        }
        const double revenueWan = obj.value(QStringLiteral("revenue")).toDouble() / 10000.0;

        labels << (dateStr.length() >= 10 ? dateStr.mid(5) : dateStr);
        x.append(x.size());
        y.append(revenueWan);
    }

    if (x.isEmpty()) {
        if (m_chartTitle) {
            m_chartTitle->setText(QStringLiteral("营收趋势 / 万元（暂无数据）"));
        }
        m_chart->xAxis->setLabel(QStringLiteral("日期"));
        m_chart->yAxis->setLabel(QStringLiteral("营收(万元)"));
        m_chart->xAxis->setRange(0, 1);
        m_chart->yAxis->setRange(0, 1);
        m_chart->replot();
        return;
    }

    QCPGraph *graph = m_chart->addGraph();
    graph->setData(x, y);
    graph->setPen(QPen(QColor("#2B6BFF"), 2.5));
    graph->setBrush(QBrush(QColor(43, 107, 255, 36)));
    QCPScatterStyle circleStyle(QCPScatterStyle::ssCircle);
    circleStyle.setSize(5);
    circleStyle.setPen(QPen(QColor("#2B6BFF"), 1.5));
    circleStyle.setBrush(QBrush(QColor("#FFFFFF")));
    graph->setScatterStyle(circleStyle);

    QSharedPointer<QCPAxisTickerText> ticker(new QCPAxisTickerText());
    for (int i = 0; i < x.size(); ++i) {
        ticker->addTick(x[i], labels[i]);
    }
    m_chart->xAxis->setTicker(ticker.template staticCast<QCPAxisTicker>());

    m_chart->xAxis->setLabel(QStringLiteral("日期"));
    m_chart->yAxis->setLabel(QStringLiteral("营收(万元)"));
    if (m_chartTitle) {
        m_chartTitle->setText(QStringLiteral("营收趋势 / 万元"));
    }

    m_chart->rescaleAxes();
    double maxY = 0.0;
    for (double v : y) {
        if (v > maxY) {
            maxY = v;
        }
    }
    if (maxY <= 0.0) {
        m_chart->yAxis->setRange(0, 0.01);
    }
    m_chart->replot();
}

void MainWindow::on_btnRefresh_clicked()
{
    if (m_refreshBusy) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("正在请求，请稍等"));
        return;
    }
    m_refreshBusy = true;
    reloadOverviewStat();
    m_refreshBusy = false;
}

// 返回总览
void MainWindow::on_btnBackHome_clicked()
{
    resetAllBtnSelect();
    ui->btnOverview->setProperty("selected", true);
    refreshBtnStyle(ui->btnOverview);
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
    QTableWidgetItem *typeItem = ui->tableWidgetPile->item(row, 2);
    QTableWidgetItem *powerItem = ui->tableWidgetPile->item(row, 3);
    QTableWidgetItem *statusItem = ui->tableWidgetPile->item(row, 4);
    if (!typeItem || !powerItem || !statusItem) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("电桩行数据不完整"));
        return;
    }
    const QString oldType = typeItem->text();
    const double oldPower = powerItem->text().toDouble();
    const QString oldStatus = statusItem->text();

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
    m_orderPager.page = 0;
    m_orderItems = QJsonArray();

    QJsonObject params;
    params["limit"] = 500;

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
        renderOrderPage();
        return;
    }

    m_orderItems = resp.value("data").toObject().value("items").toArray();
    renderOrderPage();
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

    QPushButton *btnDetail = makeOpButton(QStringLiteral("详情"), "primary", 60);
    QList<QPushButton*> opBtns{btnDetail};
    connect(btnDetail, &QPushButton::clicked, this, [=]() {
        onOrderDetailClicked(obj);
    });

    if (status == QStringLiteral("待支付")) {
        QPushButton *btnSettle = makeOpButton(QStringLiteral("代结算"), "success", 78);
        opBtns << btnSettle;
        connect(btnSettle, &QPushButton::clicked, this, [=]() {
            onOrderAdminSettle(orderNo);
        });
    }

    ui->tableOrder->setCellWidget(row, 8, makeOpCell(opBtns));
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

void MainWindow::reloadStationList()
{
    m_stationPager.page = 0;
    m_stationFilteredItems = QJsonArray();
    m_stationItems = QJsonArray();

    const QJsonObject resp = m_api->call(QStringLiteral("station.admin.list"));
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("获取电站列表失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        renderStationPage();
        return;
    }

    m_stationItems = resp.value(QStringLiteral("data")).toObject().value(QStringLiteral("items")).toArray();
    m_forecastMinIdle.clear();
    const QJsonObject fcResp = m_api->call(
        QStringLiteral("forecast.list"), QJsonObject{{QStringLiteral("horizon"), QStringLiteral("1h")}});
    if (fcResp.value(QStringLiteral("ok")).toBool()) {
        const QJsonArray fcItems = fcResp.value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("items")).toArray();
        for (const QJsonValue &fv : fcItems) {
            const QJsonObject fo = fv.toObject();
            const int sid = fo.value(QStringLiteral("station_id")).toInt();
            const int predIdle = fo.value(QStringLiteral("predicted_idle_piles")).toInt();
            if (!m_forecastMinIdle.contains(sid) || predIdle < m_forecastMinIdle[sid]) {
                m_forecastMinIdle[sid] = predIdle;
            }
        }
    }

    const QString keyword = ui->editStationKeyword->text().trimmed();

    for (const QJsonValue &value : m_stationItems) {
        const QJsonObject obj = value.toObject();
        if (!keyword.isEmpty()) {
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString address = obj.value(QStringLiteral("address")).toString();
            if (!name.contains(keyword) && !address.contains(keyword)) {
                continue;
            }
        }
        m_stationFilteredItems.append(obj);
    }
    renderStationPage();
}

void MainWindow::addStationRow(const QJsonObject &obj)
{
    const int row = ui->tableStation->rowCount();
    ui->tableStation->insertRow(row);

    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    ui->tableStation->setItem(row, 0, mkReadOnly(QString::number(obj.value(QStringLiteral("id")).toInt())));
    ui->tableStation->setItem(row, 1, mkReadOnly(obj.value(QStringLiteral("name")).toString()));
    ui->tableStation->setItem(row, 2, mkReadOnly(obj.value(QStringLiteral("address")).toString()));
    ui->tableStation->setItem(row, 3, mkReadOnly(QString::number(obj.value(QStringLiteral("price")).toDouble(), 'f', 2)));
    ui->tableStation->setItem(row, 4, mkReadOnly(QString::number(obj.value(QStringLiteral("total_piles")).toInt())));

    const int stationId = obj.value(QStringLiteral("id")).toInt();
    const int idle = obj.value(QStringLiteral("idle_piles")).toInt();
    const int total = obj.value(QStringLiteral("total_piles")).toInt();
    const double idleRate = total > 0 ? idle / static_cast<double>(total) : 1.0;
    const int predIdle = m_forecastMinIdle.value(stationId, idle);
    const double predRate = total > 0 ? predIdle / static_cast<double>(total) : idleRate;
    const bool loadWarn = total > 0 && (idleRate < 0.30 || predRate < 0.30);

    QString idleText = QString::number(idle);
    if (loadWarn) {
        idleText += QStringLiteral(" ⚠");
    }
    auto *idleItem = mkReadOnly(idleText);
    if (loadWarn) {
        idleItem->setForeground(QBrush(QColor(QStringLiteral("#D93025"))));
        idleItem->setToolTip(QStringLiteral(
            "负荷预警：当前空闲率 %1%，预测(1h)空闲率 %2%，低于 30% 阈值")
                                 .arg(idleRate * 100.0, 0, 'f', 0)
                                 .arg(predRate * 100.0, 0, 'f', 0));
    }
    ui->tableStation->setItem(row, 5, idleItem);

    const double onlineRate = obj.value(QStringLiteral("online_rate")).toDouble();
    ui->tableStation->setItem(row, 6, mkReadOnly(QStringLiteral("%1%").arg(onlineRate * 100.0, 0, 'f', 1)));
    ui->tableStation->setItem(row, 7, mkReadOnly(obj.value(QStringLiteral("created_at")).toString()));

    QPushButton *btnDetail = makeOpButton(QStringLiteral("详情"), "primary", 52);
    QPushButton *btnPiles = makeOpButton(QStringLiteral("电桩"), "primary", 52);
    QPushButton *btnEdit = makeOpButton(QStringLiteral("编辑"), "primary", 52);
    QPushButton *btnDelete = makeOpButton(QStringLiteral("删除"), "danger", 52);
    ui->tableStation->setCellWidget(row, 8,
        makeOpCell({btnDetail, btnPiles, btnEdit, btnDelete}));

    connect(btnDetail, &QPushButton::clicked, this, [=]() {
        onStationDetailClicked(obj);
    });
    connect(btnPiles, &QPushButton::clicked, this, [=]() {
        goToStationPiles(obj.value(QStringLiteral("id")).toInt());
    });
    connect(btnEdit, &QPushButton::clicked, this, [=]() {
        onEditStationClicked(obj);
    });
    connect(btnDelete, &QPushButton::clicked, this, [=]() {
        onDeleteStationClicked(obj);
    });
}

void MainWindow::onStationDetailClicked(const QJsonObject &station)
{
    const int stationId = station.value(QStringLiteral("id")).toInt();
    QJsonObject params;
    params[QStringLiteral("station_id")] = stationId;
    const QJsonObject resp = m_api->call(QStringLiteral("station.detail"), params);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("获取电站详情失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        return;
    }

    StationDetailDialog dlg(this);
    dlg.setDetail(resp.value(QStringLiteral("data")).toObject());
    connect(&dlg, &StationDetailDialog::viewPilesRequested, this, [this, &dlg](int sid) {
        dlg.accept();
        goToStationPiles(sid);
    });
    dlg.exec();
}

void MainWindow::goToStationPiles(int stationId)
{
    if (stationId <= 0) {
        return;
    }

    resetAllBtnSelect();
    ui->btnPile->setProperty("selected", true);
    refreshBtnStyle(ui->btnPile);

    loadStationCombo();

    int targetIndex = 0;
    for (int i = 0; i < ui->comboStation->count(); ++i) {
        if (ui->comboStation->itemData(i).toInt() == stationId) {
            targetIndex = i;
            break;
        }
    }
    ui->comboStation->setCurrentIndex(targetIndex);
    ui->lineEditPileId->clear();
    ui->comboPileStatus->setCurrentText(QStringLiteral("全部状态"));

    // 先设好筛选再切页，避免 currentChanged 里 reload 覆盖为「全部电站」
    ui->stackedWidget->blockSignals(true);
    ui->stackedWidget->setCurrentIndex(1);
    ui->stackedWidget->blockSignals(false);
    reloadPileList();
}

void MainWindow::onAddStationClicked()
{
    StationEditDialog dlg(this);
    dlg.setCreateMode();
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QJsonObject resp = m_api->call(QStringLiteral("station.create"), dlg.getCreateParams());
    if (resp.value(QStringLiteral("ok")).toBool()) {
        QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("新增电站成功"));
        reloadStationList();
        loadStationCombo();
    } else {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("新增失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
    }
}

void MainWindow::onEditStationClicked(const QJsonObject &station)
{
    StationEditDialog dlg(this);
    dlg.setEditMode(station);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QJsonObject resp = m_api->call(QStringLiteral("station.update"), dlg.getUpdateParams());
    if (resp.value(QStringLiteral("ok")).toBool()) {
        QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("电站信息已更新"));
        reloadStationList();
        loadStationCombo();
    } else {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("更新失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
    }
}

void MainWindow::onDeleteStationClicked(const QJsonObject &station)
{
    const int stationId = station.value(QStringLiteral("id")).toInt();
    const QString name = station.value(QStringLiteral("name")).toString();
    const auto ret = QMessageBox::question(
        this,
        QStringLiteral("删除确认"),
        QStringLiteral("确定删除电站「%1」吗？\n\n"
                       "存在未完成订单、使用中电桩或历史订单的电站无法删除。").arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    QJsonObject params;
    params[QStringLiteral("station_id")] = stationId;
    const QJsonObject resp = m_api->call(QStringLiteral("station.delete"), params);
    if (resp.value(QStringLiteral("ok")).toBool()) {
        QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("电站已删除"));
        reloadStationList();
        loadStationCombo();
    } else {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("删除失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
    }
}

void MainWindow::reloadOperationLogList()
{
    m_logPager.page = 0;
    m_logItems = QJsonArray();

    const QDate fromDate = ui->dateLogFrom->date();
    const QDate toDate = ui->dateLogTo->date();
    if (fromDate > toDate) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("开始日期不能晚于结束日期"));
        renderLogPage();
        return;
    }

    QJsonObject params;
    params[QStringLiteral("limit")] = 500;
    params[QStringLiteral("date_from")] = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    params[QStringLiteral("date_to")] = toDate.toString(QStringLiteral("yyyy-MM-dd"));

    const QString actionText = ui->comboLogAction->currentText();
    if (actionText != QStringLiteral("全部操作")) {
        params[QStringLiteral("action")] = actionText;
    }

    if (m_comboLogTargetType) {
        const QString targetType = m_comboLogTargetType->currentData().toString();
        if (!targetType.isEmpty()) {
            params[QStringLiteral("target_type")] = targetType;
        }
    }

    if (m_editLogKeyword) {
        const QString keyword = m_editLogKeyword->text().trimmed();
        if (!keyword.isEmpty()) {
            params[QStringLiteral("keyword")] = keyword;
        }
    }

    const QJsonObject resp = m_api->call(QStringLiteral("operation_log.list"), params);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("获取操作日志失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        renderLogPage();
        return;
    }

    m_logItems = resp.value(QStringLiteral("data")).toObject().value(QStringLiteral("items")).toArray();
    renderLogPage();
}

void MainWindow::addOperationLogRow(const QJsonObject &obj)
{
    const int row = ui->tableLog->rowCount();
    ui->tableLog->insertRow(row);

    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    ui->tableLog->setItem(row, 0, mkReadOnly(obj.value(QStringLiteral("created_at")).toString()));
    ui->tableLog->setItem(row, 1, mkReadOnly(obj.value(QStringLiteral("admin_username")).toString()));
    ui->tableLog->setItem(row, 2, mkReadOnly(obj.value(QStringLiteral("action")).toString()));
    ui->tableLog->setItem(row, 3, mkReadOnly(obj.value(QStringLiteral("target_type")).toString()));
    ui->tableLog->setItem(row, 4, mkReadOnly(obj.value(QStringLiteral("target_id")).toString()));
    ui->tableLog->setItem(row, 5, mkReadOnly(obj.value(QStringLiteral("detail")).toString()));
}

void MainWindow::reloadAnnouncementList()
{
    m_announcePager.page = 0;
    m_announceItems = QJsonArray();
    const QJsonObject resp = m_api->call(QStringLiteral("announcement.admin.list"));
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("获取公告列表失败"));
        QMessageBox::warning(this, QStringLiteral("错误"), errMsg);
        renderAnnouncementPage();
        return;
    }

    m_announceItems = resp.value(QStringLiteral("data")).toObject()
                          .value(QStringLiteral("items")).toArray();
    renderAnnouncementPage();
}

void MainWindow::addAnnouncementRow(const QJsonObject &obj)
{
    const int row = ui->tableAnnouncement->rowCount();
    ui->tableAnnouncement->insertRow(row);

    auto mkReadOnly = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    ui->tableAnnouncement->setItem(row, 0, mkReadOnly(QString::number(obj.value(QStringLiteral("id")).toInt())));
    ui->tableAnnouncement->setItem(row, 1, mkReadOnly(obj.value(QStringLiteral("title")).toString()));
    const bool active = obj.value(QStringLiteral("is_active")).toInt() == 1;
    ui->tableAnnouncement->setItem(row, 2, mkReadOnly(active ? QStringLiteral("启用")
                                                               : QStringLiteral("停用")));
    ui->tableAnnouncement->setItem(row, 3, mkReadOnly(obj.value(QStringLiteral("created_at")).toString()));

    QPushButton *btnEdit = makeOpButton(QStringLiteral("编辑"), "primary", 56);
    QPushButton *btnDelete = makeOpButton(QStringLiteral("删除"), "danger", 56);
    ui->tableAnnouncement->setCellWidget(row, 4, makeOpCell({btnEdit, btnDelete}));

    connect(btnEdit, &QPushButton::clicked, this, [this, obj]() {
        onEditAnnouncementClicked(obj);
    });
    connect(btnDelete, &QPushButton::clicked, this, [this, obj]() {
        onDeleteAnnouncementClicked(obj);
    });
}

void MainWindow::onAddAnnouncementClicked()
{
    AnnouncementEditDialog dlg(this);
    dlg.setCreateMode();
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QJsonObject resp = m_api->call(QStringLiteral("announcement.create"), dlg.getParams());
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("新增失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
        return;
    }
    reloadAnnouncementList();
}

void MainWindow::onEditAnnouncementClicked(const QJsonObject &announcement)
{
    AnnouncementEditDialog dlg(this);
    dlg.setEditMode(announcement);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QJsonObject resp = m_api->call(QStringLiteral("announcement.update"), dlg.getParams());
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("更新失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
        return;
    }
    reloadAnnouncementList();
}

void MainWindow::onDeleteAnnouncementClicked(const QJsonObject &announcement)
{
    const int annId = announcement.value(QStringLiteral("id")).toInt();
    const QString title = announcement.value(QStringLiteral("title")).toString();
    const auto ret = QMessageBox::question(
        this,
        QStringLiteral("删除确认"),
        QStringLiteral("确定删除公告「%1」吗？").arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    QJsonObject params;
    params[QStringLiteral("id")] = annId;
    const QJsonObject resp = m_api->call(QStringLiteral("announcement.delete"), params);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QString errMsg = resp.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString(
                                       QStringLiteral("删除失败"));
        QMessageBox::warning(this, QStringLiteral("失败"), errMsg);
        return;
    }
    reloadAnnouncementList();
}

void MainWindow::setupPagination()
{
    m_userPager.attach(ui->page_3, ui->verticalLayout_6, ui->tableUser);
    connect(m_userPager.prev, &QPushButton::clicked, this, [this]() {
        if (m_userPager.page > 0) {
            --m_userPager.page;
            renderUserPage();
        }
    });
    connect(m_userPager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_userPager.total > 0 ? (m_userPager.total - 1) / ListPager::kPageSize : 0;
        if (m_userPager.page < maxPage) {
            ++m_userPager.page;
            renderUserPage();
        }
    });

    auto *pileLay = qobject_cast<QVBoxLayout *>(ui->widget_5->layout());
    m_pilePager.attach(ui->page_1, pileLay, ui->tableWidgetPile);
    connect(m_pilePager.prev, &QPushButton::clicked, this, [this]() {
        if (m_pilePager.page > 0) {
            --m_pilePager.page;
            renderPilePage();
        }
    });
    connect(m_pilePager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_pilePager.total > 0 ? (m_pilePager.total - 1) / ListPager::kPageSize : 0;
        if (m_pilePager.page < maxPage) {
            ++m_pilePager.page;
            renderPilePage();
        }
    });

    m_orderPager.attach(ui->page_6, ui->verticalLayout_order, ui->tableOrder);
    connect(m_orderPager.prev, &QPushButton::clicked, this, [this]() {
        if (m_orderPager.page > 0) {
            --m_orderPager.page;
            renderOrderPage();
        }
    });
    connect(m_orderPager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_orderPager.total > 0 ? (m_orderPager.total - 1) / ListPager::kPageSize : 0;
        if (m_orderPager.page < maxPage) {
            ++m_orderPager.page;
            renderOrderPage();
        }
    });

    m_stationPager.attach(ui->page_2, ui->verticalLayout_station, ui->tableStation);
    connect(m_stationPager.prev, &QPushButton::clicked, this, [this]() {
        if (m_stationPager.page > 0) {
            --m_stationPager.page;
            renderStationPage();
        }
    });
    connect(m_stationPager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_stationPager.total > 0 ? (m_stationPager.total - 1) / ListPager::kPageSize : 0;
        if (m_stationPager.page < maxPage) {
            ++m_stationPager.page;
            renderStationPage();
        }
    });

    m_logPager.attach(ui->page_4, ui->verticalLayout_log, ui->tableLog);
    connect(m_logPager.prev, &QPushButton::clicked, this, [this]() {
        if (m_logPager.page > 0) {
            --m_logPager.page;
            renderLogPage();
        }
    });
    connect(m_logPager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_logPager.total > 0 ? (m_logPager.total - 1) / ListPager::kPageSize : 0;
        if (m_logPager.page < maxPage) {
            ++m_logPager.page;
            renderLogPage();
        }
    });

    m_announcePager.attach(ui->page_7, ui->verticalLayout_announcement, ui->tableAnnouncement);
    connect(m_announcePager.prev, &QPushButton::clicked, this, [this]() {
        if (m_announcePager.page > 0) {
            --m_announcePager.page;
            renderAnnouncementPage();
        }
    });
    connect(m_announcePager.next, &QPushButton::clicked, this, [this]() {
        const int maxPage = m_announcePager.total > 0 ? (m_announcePager.total - 1) / ListPager::kPageSize : 0;
        if (m_announcePager.page < maxPage) {
            ++m_announcePager.page;
            renderAnnouncementPage();
        }
    });
}

void MainWindow::renderUserPage()
{
    ui->tableUser->setRowCount(0);
    m_userPager.setTotal(m_userItems.size());
    for (int i = m_userPager.startIndex(); i < m_userPager.endIndex(); ++i) {
        addUserRow(m_userItems.at(i).toObject());
    }
}

void MainWindow::renderPilePage()
{
    ui->tableWidgetPile->setRowCount(0);
    m_pilePager.setTotal(m_pileItems.size());
    for (int i = m_pilePager.startIndex(); i < m_pilePager.endIndex(); ++i) {
        addPileRow(m_pileItems.at(i).toObject());
    }
}

void MainWindow::renderOrderPage()
{
    ui->tableOrder->setRowCount(0);
    m_orderPager.setTotal(m_orderItems.size());
    for (int i = m_orderPager.startIndex(); i < m_orderPager.endIndex(); ++i) {
        addOrderRow(m_orderItems.at(i).toObject());
    }
}

void MainWindow::renderStationPage()
{
    ui->tableStation->setRowCount(0);
    m_stationPager.setTotal(m_stationFilteredItems.size());
    for (int i = m_stationPager.startIndex(); i < m_stationPager.endIndex(); ++i) {
        addStationRow(m_stationFilteredItems.at(i).toObject());
    }
}

void MainWindow::renderLogPage()
{
    ui->tableLog->setRowCount(0);
    m_logPager.setTotal(m_logItems.size());
    for (int i = m_logPager.startIndex(); i < m_logPager.endIndex(); ++i) {
        addOperationLogRow(m_logItems.at(i).toObject());
    }
}

void MainWindow::renderAnnouncementPage()
{
    ui->tableAnnouncement->setRowCount(0);
    m_announcePager.setTotal(m_announceItems.size());
    for (int i = m_announcePager.startIndex(); i < m_announcePager.endIndex(); ++i) {
        addAnnouncementRow(m_announceItems.at(i).toObject());
    }
}

void MainWindow::updatePileStatusOverview(const QJsonObject &pileStat, int pileTotal, double healthRate)
{
    if (!m_labPileStatusDetail || !m_labPileHealth) {
        return;
    }
    if (pileTotal <= 0) {
        m_labPileStatusDetail->setText(QStringLiteral("暂无电桩数据"));
        m_labPileHealth->setText(QStringLiteral("健康度：--"));
        return;
    }

    const QStringList statuses = {
        QStringLiteral("闲置"), QStringLiteral("预约"),
        QStringLiteral("在用"), QStringLiteral("故障"),
    };
    QStringList parts;
    for (const QString &status : statuses) {
        const int count = pileStat.value(status).toInt(0);
        const double pct = count * 100.0 / pileTotal;
        parts << QStringLiteral("%1 %2 个（%3%）")
                     .arg(status)
                     .arg(count)
                     .arg(pct, 0, 'f', 1);
    }
    m_labPileStatusDetail->setText(parts.join(QStringLiteral("  |  ")));
    m_labPileHealth->setText(
        QStringLiteral("健康度（非故障占比）：%1%")
            .arg(healthRate * 100.0, 0, 'f', 1));
}

