#include "mainwindow.h"
#include "apiclient.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QPainter>
#include <QDialog>
#include <QTextEdit>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrlQuery>
#include <QEventLoop>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QFileDialog>
#include <QMessageBox>
#include <QDoubleValidator>

MainWindow::MainWindow(ApiClient *api, const QJsonObject &user, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_user(user)
{
    setWindowTitle(QStringLiteral("充电桩用户端"));
    /*构建界面*/
    auto *central = new QWidget(this);
    m_userLabel = new QLabel(central);
    m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                             .arg(m_user.value(QStringLiteral("nickname")).toString())
                             .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2));

    auto *locLabel = new QLabel(QStringLiteral("当前位置（模拟 GPS）："), central);
    m_latEdit = new QLineEdit(QStringLiteral("22.5431"), central);
    m_lngEdit = new QLineEdit(QStringLiteral("114.0579"), central);
    m_latEdit->setPlaceholderText(QStringLiteral("纬度 lat"));
    m_lngEdit->setPlaceholderText(QStringLiteral("经度 lng"));

    auto *locLayout = new QHBoxLayout;
    auto *latLabel = new QLabel(QStringLiteral("lat"), central);
    auto *lngLabel = new QLabel(QStringLiteral("lng"), central);
    locLayout->addWidget(latLabel);
    locLayout->addWidget(m_latEdit);
    locLayout->addWidget(lngLabel);
    locLayout->addWidget(m_lngEdit);

    // 区域下拉  地址输入  地理编码按钮
    auto *regionLabel = new QLabel(QStringLiteral("选择区域："), central);
    m_regionCombo = new QComboBox(central);
    m_regionCombo->addItem(QStringLiteral("— 请选择 —"), QVariant());
    m_regionCombo->addItem(QStringLiteral("北京 天安门"),   QVariantList{39.9042, 116.4074});
    m_regionCombo->addItem(QStringLiteral("上海 外滩"),     QVariantList{31.2397, 121.4908});
    m_regionCombo->addItem(QStringLiteral("深圳 福田"),     QVariantList{22.5431, 114.0579});
    m_regionCombo->addItem(QStringLiteral("广州 天河"),     QVariantList{23.1291, 113.2644});
    m_regionCombo->addItem(QStringLiteral("杭州 西湖"),     QVariantList{30.2741, 120.1551});

    auto *addressLabel = new QLabel(QStringLiteral("或输入地址："), central);
    m_addressEdit = new QLineEdit(central);
    m_addressEdit->setPlaceholderText(QStringLiteral("例如：深圳市南山区科技园"));
    m_geocodeButton = new QPushButton(QStringLiteral("地理编码"), central);

    auto *geoLayout = new QHBoxLayout;
    geoLayout->addWidget(regionLabel);
    geoLayout->addWidget(m_regionCombo);
    geoLayout->addWidget(addressLabel);
    geoLayout->addWidget(m_addressEdit, 1);   // 地址框占剩余空间
    geoLayout->addWidget(m_geocodeButton);

    m_refreshButton = new QPushButton(QStringLiteral("刷新附近充电站"), central);
    m_refreshButton->setStyleSheet(R"(
        QPushButton{
            background-color: rgba(255, 255, 255, 200);   /* 浅白底 */
            color: #1a5fb4;                                /* 深蓝色文字 */
            border: 1px solid rgba(26, 95, 180, 150);
            border-radius: 6px;
            padding: 10px 20px;
            font-size: 14px;
            font-weight: bold;
        }
        /* 鼠标悬浮：背景变蓝，文字变白 */
        QPushButton:hover{
            background-color: rgba(26, 95, 180, 220);
            color: white;
            border: 1px solid rgba(26, 95, 180, 255);
        }
        /* 鼠标按下：颜色更深，有按压感 */
        QPushButton:pressed{
            background-color: rgba(20, 75, 140, 240);
            color: white;
            padding-top: 11px;   /* 文字轻微下移1px，模拟按下效果 */
            padding-bottom: 9px;
        }
    )");
    m_stationList = new QListWidget(central);
    m_statusLabel = new QLabel(central);

    auto *layout = new QVBoxLayout(central);
    // 欢迎语 + 个人中心 + 订单历史 + 我的收藏
    m_profileButton = new QPushButton(QStringLiteral("个人中心"), central);
    m_profileButton->setStyleSheet(m_refreshButton->styleSheet());  // 复用刷新按钮样式
    m_orderButton = new QPushButton(QStringLiteral("订单历史"), central);
    m_orderButton->setStyleSheet(m_refreshButton->styleSheet());
    m_favoriteButton = new QPushButton(QStringLiteral("我的收藏"), central);
    m_favoriteButton->setStyleSheet(m_refreshButton->styleSheet());

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(m_userLabel);
    topLayout->addStretch();
    topLayout->addWidget(m_profileButton);
    topLayout->addWidget(m_orderButton);
    topLayout->addWidget(m_favoriteButton);
    layout->addLayout(topLayout);
    layout->addWidget(locLabel);
    layout->addLayout(locLayout);
    layout->addLayout(geoLayout);
    layout->addWidget(m_refreshButton);
    layout->addWidget(m_stationList);
    layout->addWidget(m_statusLabel);
    central->setLayout(layout);
    // 让 centralWidget 背景透明，这样 QMainWindow 的 paintEvent 背景图才能透出来
    central->setStyleSheet("background: transparent;");
    setCentralWidget(central);

    // 欢迎标签：大字号加粗
    m_userLabel->setStyleSheet(R"(
        QLabel{
            color: #ffffff;            /* 深蓝色 */
            font-size: 17px;
            font-weight: bold;
            padding: 4px 0px;
        }
    )");

    // 说明标签（当前位置、lat/lng、选择区域、输入地址） 白色
    const QString whiteLabel = QStringLiteral(
        "color: #ffffff; font-size: 13px; padding: 2px 0px;");
    locLabel->setStyleSheet(whiteLabel);
    latLabel->setStyleSheet(whiteLabel);
    lngLabel->setStyleSheet(whiteLabel);
    regionLabel->setStyleSheet(whiteLabel);
    addressLabel->setStyleSheet(whiteLabel);

    // 经纬度输入框：半透明白底
    m_latEdit->setStyleSheet(R"(
        QLineEdit{
            background-color: rgba(255, 255, 255, 200);
            color: #222222;
            border: 1px solid rgba(26, 95, 180, 120);
            border-radius: 4px;
            padding: 6px 8px;
            font-size: 13px;
        }
        QLineEdit:focus{
            background-color: rgba(255, 255, 255, 240);
            border: 1px solid rgba(26, 95, 180, 200);
        }
    )");
    m_lngEdit->setStyleSheet(m_latEdit->styleSheet());  // 复用同样式

    // 充电站列表：半透明白板
    m_stationList->setStyleSheet(R"(
        QListWidget{
            background-color: rgba(255, 255, 255, 185);
            color: #222222;
            border: 1px solid rgba(26, 95, 180, 100);
            border-radius: 6px;
            padding: 4px;
            font-size: 13px;
            outline: none;             /* 去掉选中时的虚线框 */
        }
        QListWidget::item{
            padding: 8px 6px;
            border-bottom: 1px solid rgba(0, 0, 0, 30);
        }
        QListWidget::item:hover{
            background-color: rgba(26, 95, 180, 40);   /* 鼠标悬停行：淡蓝高亮 */
        }
        QListWidget::item:selected{
            background-color: rgba(26, 95, 180, 210);  /* 选中行：深蓝 */
            color: white;
        }
    )");

    // 底部状态标签
    m_statusLabel->setStyleSheet(R"(
        QLabel{
            color: #555555;            /* 中灰色 */
            font-size: 12px;
            font-style: italic;
            padding: 4px 0px;
        }
    )");

    // "选择区域：" 和 "或输入地址：" 两个提示标签：白色字
    regionLabel->setStyleSheet(R"(
        QLabel{
            color: #ffffff;
            font-size: 13px;
            padding: 2px 0px;
        }
    )");
    addressLabel->setStyleSheet(regionLabel->styleSheet());  // 复用同样式

    // ===== 地址输入框：毛玻璃白色（半透明白底深字）=====
    m_addressEdit->setStyleSheet(R"(
        QLineEdit{
            background-color: rgba(255, 255, 255, 180);
            color: #222222;
            border: 1px solid rgba(255, 255, 255, 120);
            border-radius: 4px;
            padding: 6px 8px;
            font-size: 13px;
        }
        QLineEdit:focus{
            background-color: rgba(255, 255, 255, 230);
            border: 1px solid rgba(255, 255, 255, 180);
        }
    )");

    // 区域下拉框：毛玻璃白色（未点击时半透明白底）
    m_regionCombo->setStyleSheet(R"(
        /* 下拉框本体（未展开状态） */
        QComboBox{
            background-color: rgba(255, 255, 255, 180);   /* 半透明白底，毛玻璃感 */
            color: #222222;                              /* 深色字，在白底上清晰 */
            border: 1px solid rgba(255, 255, 255, 120);
            border-radius: 4px;
            padding: 6px 8px;
            font-size: 13px;
        }
        /* 鼠标悬停下拉框 */
        QComboBox:hover{
            background-color: rgba(255, 255, 255, 220);
            border: 1px solid rgba(255, 255, 255, 180);
        }
        /* 下拉框右侧的下拉箭头按钮区域 */
        QComboBox::drop-down{
            border: none;
            width: 24px;
        }
        /* 下拉箭头图标 */
        QComboBox::down-arrow{
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 6px solid #1a5fb4;   /* 深蓝色小三角箭头 */
            width: 0;
            height: 0;
            margin-right: 8px;
        }
        /* 展开后的下拉列表（弹出菜单） */
        QComboBox QAbstractItemView{
            background-color: rgba(255, 255, 255, 240);   /* 弹出列表接近不透明白 */
            color: #222222;
            border: 1px solid rgba(26, 95, 180, 150);
            border-radius: 4px;
            padding: 4px;
            outline: none;
        }
        QComboBox QAbstractItemView::item{
            padding: 6px 8px;
            min-height: 24px;
        }
        QComboBox QAbstractItemView::item:hover{
            background-color: rgba(26, 95, 180, 50);
        }
        QComboBox QAbstractItemView::item:selected{
            background-color: rgba(26, 95, 180, 210);
            color: white;
        }
    )");
    m_geocodeButton->setStyleSheet(m_refreshButton->styleSheet());

    resize(1500, 1125);

    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshStations);
    connect(m_stationList,&QListWidget::itemClicked,this,&MainWindow::onStationItemClicked);// 绑定站点列表项点击信号和对应的点击事件
    m_netMgr = new QNetworkAccessManager(this); //初始化网络管理器
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRegionChanged);
    connect(m_geocodeButton, &QPushButton::clicked, this, &MainWindow::onGeocodeAddress);
    connect(m_addressEdit, &QLineEdit::returnPressed, this, &MainWindow::onGeocodeAddress);
    connect(m_profileButton, &QPushButton::clicked, this, &MainWindow::onProfileCenter);
    connect(m_orderButton, &QPushButton::clicked, this, &MainWindow::onOrderHistory);
    connect(m_favoriteButton, &QPushButton::clicked, this, &MainWindow::onFavoriteList);
    loadStations();

    QTimer::singleShot(0, this, [this]() { checkOpenOrder(false); });
}

void MainWindow::onRefreshStations()
{
    loadStations();
}

void MainWindow::loadStations()
{
    m_statusLabel->clear();
    m_stationList->clear();

    // 位置为空校验
    if (m_latEdit->text().trimmed().isEmpty() || m_lngEdit->text().trimmed().isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请输入当前位置"));
        return;
    }

    QJsonObject data;
    data["lat"] = m_latEdit->text().toDouble();
    data["lng"] = m_lngEdit->text().toDouble();
    data["keyword"] = QString();
    const QJsonObject resp = m_api->call(QStringLiteral("station.list"), data);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        m_statusLabel->setText(err.value(QStringLiteral("message")).toString());
        return;
    }
    const QJsonArray items = resp.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("items")).toArray();

    // 无结果提示
    if (items.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("附近暂无充电站"));
        return;
    }

    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString line = QStringLiteral("%1 | %2元/度 | 空闲 %3/%4 | %5 km")
                                 .arg(item.value(QStringLiteral("name")).toString())
                                 .arg(item.value(QStringLiteral("price")).toDouble(), 0, 'f', 2)
                                 .arg(item.value(QStringLiteral("idle_piles")).toInt())
                                 .arg(item.value(QStringLiteral("total_piles")).toInt())
                                 .arg(item.value(QStringLiteral("distance_km")).toDouble(), 0, 'f', 1);
        auto *listItem = new QListWidgetItem(line);
        listItem->setData(Qt::UserRole, item.value("id").toInt());
        m_stationList->addItem(listItem);
    }
    m_statusLabel->setText(QStringLiteral("共 %1 个充电站（按距离排序）").arg(items.size()));
}

//重写paintEvent，设置背景
void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    QPixmap bgPix(":/res/resources/mainwindow_background.png");

    if (bgPix.isNull()) {
        qDebug() << "MainWindow背景图加载失败，检查路径";
        return;
    }

    // 拉伸铺满整个窗口
    QPixmap scaledBg = bgPix.scaled(this->size(),
                                    Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
    painter.drawPixmap(rect(), scaledBg);
    // 盖一层半透明白色遮罩，把深色背景压淡
    // alpha 越大背景越亮、越淡；alpha 越小背景越深、越透
    painter.fillRect(rect(), QColor(255, 255, 255, 0));  // 140≈55%透明度，可调
}

void MainWindow::onStationItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    const int stationId = item->data(Qt::UserRole).toInt();
    showStationDetail(stationId);
}

// 显示站点详情
void MainWindow::showStationDetail(int stationId)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("充电站电桩详情"));
    dlg.resize(680, 520);

    auto *lay = new QVBoxLayout(&dlg);

    // 站点信息
    auto *stationLabel = new QLabel(&dlg);
    const QString blackLabel = QStringLiteral(
            "color: #000000; font-size: 15px; font-weight: bold; padding: 4px 0px;");
    stationLabel->setStyleSheet(blackLabel);

    // 电桩列表（可选中）
    auto *pileList = new QListWidget(&dlg);
    pileList->setStyleSheet(m_stationList->styleSheet());

    // 按钮行
    auto *reserveBtn = new QPushButton(QStringLiteral("预约选中桩"), &dlg);
    auto *favoriteBtn = new QPushButton(QStringLiteral("收藏该站"), &dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    reserveBtn->setStyleSheet(m_refreshButton->styleSheet());
    favoriteBtn->setStyleSheet(m_refreshButton->styleSheet());
    closeBtn->setStyleSheet(m_refreshButton->styleSheet());

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(reserveBtn);
    btnRow->addWidget(favoriteBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);

    lay->addWidget(stationLabel);
    lay->addWidget(pileList);
    lay->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);

    // 拉取站点详情
    QJsonObject reqData;
    reqData["station_id"] = stationId;
    const QJsonObject resp = m_api->call(QStringLiteral("station.detail"), reqData);

    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        QMessageBox::warning(&dlg, QStringLiteral("提示"),
                             err.value(QStringLiteral("message")).toString());
        dlg.exec();
        return;
    }

    const QJsonObject detail = resp.value(QStringLiteral("data")).toObject();
    const QJsonObject station = detail.value(QStringLiteral("station")).toObject();
    const QJsonArray piles = detail.value(QStringLiteral("piles")).toArray();

    stationLabel->setText(QStringLiteral("【%1】地址：%2 | 电价：%3元/度 | 共%4个电桩")
                             .arg(station.value(QStringLiteral("name")).toString())
                             .arg(station.value(QStringLiteral("address")).toString())
                             .arg(station.value(QStringLiteral("price")).toDouble(), 0, 'f', 2)
                             .arg(piles.size()));

    // 填充电桩列表
    for (const QJsonValue &v : piles) {
        const QJsonObject pile = v.toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();

        QString statusMark;
        if (status == QStringLiteral("闲置"))      statusMark = QStringLiteral("🟢闲置(可预约)");
        else if (status == QStringLiteral("预约")) statusMark = QStringLiteral("🟡预约");
        else if (status == QStringLiteral("在用")) statusMark = QStringLiteral("🔴在用");
        else if (status == QStringLiteral("故障")) statusMark = QStringLiteral("⚠️故障");
        else                                       statusMark = status;

        const QString line = QStringLiteral("编号 %1 | %2 | %3 | 功率 %4 kW")
                                 .arg(pile.value(QStringLiteral("pile_no")).toString())
                                 .arg(pile.value(QStringLiteral("type")).toString())
                                 .arg(statusMark)
                                 .arg(pile.value(QStringLiteral("power_kw")).toDouble(), 0, 'f', 1);

        auto *item = new QListWidgetItem(line);
        item->setData(Qt::UserRole, pile.value(QStringLiteral("pile_no")).toString());
        item->setData(Qt::UserRole + 1, status);
        // 非闲置桩置灰、不可选
        if (status != QStringLiteral("闲置")) {
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setForeground(QColor("#aaaaaa"));
        }
        pileList->addItem(item);
    }

    // 收藏按钮
    connect(favoriteBtn, &QPushButton::clicked, &dlg, [&]() {
        QJsonObject favData;
        favData["station_id"] = stationId;
        const QJsonObject favResp = m_api->call(QStringLiteral("station.favorite.add"), favData);
        if (!favResp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = favResp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("收藏成功"));
    });

    // 预约按钮（检查 + 预约/开始充电）
    connect(reserveBtn, &QPushButton::clicked, &dlg, [&]() {
        QListWidgetItem *item = pileList->currentItem();
        if (!item) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("请先选择一个闲置电桩"));
            return;
        }
        const QString pileNo = item->data(Qt::UserRole).toString();
        const QString status = item->data(Qt::UserRole + 1).toString();
        if (status != QStringLiteral("闲置")) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 QStringLiteral("该电桩不可预约"));
            return;
        }

        // NO 13.0：先检查未完成订单，有则拦截
        if (checkOpenOrder(true)) return;

        // NO 14.0：预约电桩
        QJsonObject reserveData;
        reserveData["pile_no"] = pileNo;
        const QJsonObject reserveResp = m_api->call(QStringLiteral("charge.reserve"), reserveData);
        if (!reserveResp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = reserveResp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("预约失败"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        const QString orderNo = reserveResp.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("order_no")).toString();

        // 预约成功，问是否立即开始充电
        const auto ret = QMessageBox::question(&dlg, QStringLiteral("预约成功"),
            QStringLiteral("预约成功！订单号：%1\n电桩 %2 已锁定为预约状态。\n是否立即开始充电？")
                .arg(orderNo).arg(pileNo),
            QMessageBox::Yes | QMessageBox::No);

        if (ret != QMessageBox::Yes) {
            dlg.accept();
            return;
        }

        // 开始充电
        QJsonObject startData;
        startData["order_no"] = orderNo;
        const QJsonObject startResp = m_api->call(QStringLiteral("charge.start"), startData);
        if (!startResp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = startResp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("开始充电失败"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        QMessageBox::information(&dlg, QStringLiteral("充电已开始"),
            QStringLiteral("充电已开始！订单号：%1\n").arg(orderNo));
        dlg.accept();
        showChargingProgress(orderNo);  // 打开充电中页面
    });

    dlg.exec();
}

// 下拉选区域：直接把预设经纬度填到输入框
void MainWindow::onRegionChanged(int index)
{
    const QVariant data = m_regionCombo->itemData(index);
    if (!data.isValid()) return;
    const QVariantList pos = data.toList();
    if (pos.size() != 2) return;
    m_latEdit->setText(QString::number(pos[0].toDouble(), 'f', 6));
    m_lngEdit->setText(QString::number(pos[1].toDouble(), 'f', 6));
    m_statusLabel->setText(QStringLiteral("已选择区域：%1")
                               .arg(m_regionCombo->itemText(index)));
}

// 地址输入框回车或点按钮：调百度地图 API 转经纬度
void MainWindow::onGeocodeAddress()
{
    const QString address = m_addressEdit->text().trimmed();
    if (address.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请输入地址"));
        return;
    }
    if (m_baiduAk.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("未配置百度地图 AK，无法地理编码；请使用上方下拉选择区域，或在代码中填写 m_baiduAk"));
        return;
    }
    m_geocodeButton->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在地理编码…"));

    const QJsonObject result = geocodeByBaidu(address);
    m_geocodeButton->setEnabled(true);

    if (result.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("地理编码失败，请检查地址或网络"));
        return;
    }
    m_latEdit->setText(QString::number(result.value(QStringLiteral("lat")).toDouble(), 'f', 6));
    m_lngEdit->setText(QString::number(result.value(QStringLiteral("lng")).toDouble(), 'f', 6));
    m_statusLabel->setText(QStringLiteral("地理编码成功：%1 → lat %2, lng %3")
                               .arg(address)
                               .arg(m_latEdit->text())
                               .arg(m_lngEdit->text()));
}

// 调用百度地图地理编码 API（同步等待）
QJsonObject MainWindow::geocodeByBaidu(const QString &address)
{
    QUrl url(QStringLiteral("https://api.map.baidu.com/geocoding/v3/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), address);
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("ak"), m_baiduAk);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = m_netMgr->get(request);

    // 用事件循环同步等待响应（最多8秒）
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);   // 超时兜底
    loop.exec();

    QJsonObject result;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "geocode error:" << reply->errorString();
        reply->deleteLater();
        return result;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return result;
    const QJsonObject root = doc.object();
    // 百度返回 status=0 表示成功
    if (root.value(QStringLiteral("status")).toInt() != 0) return result;

    const QJsonObject location = root.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("location")).toObject();
    result["lat"] = location.value(QStringLiteral("lat")).toDouble();
    result["lng"] = location.value(QStringLiteral("lng")).toDouble();
    return result;
}

// 个人中心
void MainWindow::onProfileCenter()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("个人中心"));
    dlg.resize(380, 420);

    auto *lay = new QVBoxLayout(&dlg);

    // 头像区
    auto *avatarLabel = new QLabel(&dlg);
    avatarLabel->setFixedSize(90, 90);
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setStyleSheet(QStringLiteral(
        "border: 2px solid #1a5fb4; border-radius: 45px; background-color: #f0f0f0;"));
    QString avatarPath = m_user.value(QStringLiteral("avatar_path")).toString();
    QPixmap avatarPix(avatarPath);
    if (avatarPix.isNull()) {
        avatarLabel->setText(QStringLiteral("无头像"));
    } else {
        avatarLabel->setPixmap(avatarPix.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    auto *changeAvatarBtn = new QPushButton(QStringLiteral("更换头像"), &dlg);
    changeAvatarBtn->setStyleSheet(m_refreshButton->styleSheet());

    auto *avatarRow = new QHBoxLayout;
    avatarRow->addStretch();
    avatarRow->addWidget(avatarLabel);
    avatarRow->addWidget(changeAvatarBtn);
    avatarRow->addStretch();
    lay->addLayout(avatarRow);

    // 用变量记录新选的头像路径，初始为空表示未改
    QString newAvatarPath;

    connect(changeAvatarBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dlg, QStringLiteral("选择头像"), QString(),
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp)"));
        if (path.isEmpty()) return;
        QPixmap pix(path);
        if (pix.isNull()) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"), QStringLiteral("图片加载失败"));
            return;
        }
        newAvatarPath = path;
        avatarLabel->setPixmap(pix.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    // 信息区
    auto *phoneLabel = new QLabel(
        QStringLiteral("手机号：%1").arg(m_user.value(QStringLiteral("phone")).toString()), &dlg);
    auto *balanceLabel = new QLabel(
        QStringLiteral("余额：%1 元").arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2), &dlg);
    auto *rechargeBtn = new QPushButton(QStringLiteral("充值"), &dlg);
    rechargeBtn->setStyleSheet(m_refreshButton->styleSheet());
    auto *balanceRow = new QHBoxLayout;
    balanceRow->addWidget(balanceLabel);
    balanceRow->addStretch();
    balanceRow->addWidget(rechargeBtn);
    auto *statusLabel = new QLabel(
        QStringLiteral("状态：%1").arg(m_user.value(QStringLiteral("status")).toString()), &dlg);

    auto *nicknameLabel = new QLabel(QStringLiteral("昵称："), &dlg);
    auto *nicknameEdit = new QLineEdit(
        m_user.value(QStringLiteral("nickname")).toString(), &dlg);
    nicknameEdit->setMaxLength(20);
    nicknameEdit->setPlaceholderText(QStringLiteral("1~20个字符"));

    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->addWidget(nicknameLabel);
    nicknameRow->addWidget(nicknameEdit);

    lay->addSpacing(10);
    lay->addWidget(phoneLabel);
    lay->addLayout(nicknameRow);
    lay->addLayout(balanceRow);
    lay->addWidget(statusLabel);
    lay->addStretch();

    // 统一样式
    phoneLabel->setStyleSheet(m_statusLabel->styleSheet());
    balanceLabel->setStyleSheet(m_statusLabel->styleSheet());
    statusLabel->setStyleSheet(m_statusLabel->styleSheet());
    nicknameLabel->setStyleSheet(m_statusLabel->styleSheet());
    nicknameEdit->setStyleSheet(m_latEdit->styleSheet());

    // 按钮区
    auto *saveBtn = new QPushButton(QStringLiteral("保存"), &dlg);
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), &dlg);
    saveBtn->setStyleSheet(m_refreshButton->styleSheet());
    cancelBtn->setStyleSheet(m_refreshButton->styleSheet());

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(cancelBtn);
    lay->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(saveBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString nickname = nicknameEdit->text().trimmed();
        // 昵称校验：1~20字
        if (nickname.isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"), QStringLiteral("昵称不能为空"));
            return;
        }
        if (nickname.length() > 20) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"), QStringLiteral("昵称不能超过20个字符"));
            return;
        }
        // 至少改了一项才调用接口
        if (nickname == m_user.value(QStringLiteral("nickname")).toString() && newAvatarPath.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("没有修改内容"));
            return;
        }

        QJsonObject reqData;
        reqData["nickname"] = nickname;
        if (!newAvatarPath.isEmpty()) {
            reqData["avatar_path"] = newAvatarPath;
        }
        const QJsonObject resp = m_api->call(QStringLiteral("user.profile.update"), reqData);
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("保存失败"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        // 成功：更新本地用户信息
        const QJsonObject data = resp.value(QStringLiteral("data")).toObject();
        m_user["nickname"] = data.value(QStringLiteral("nickname")).toString();
        m_user["avatar_path"] = data.value(QStringLiteral("avatar_path")).toString();
        // 刷新主窗口欢迎语
        m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                                 .arg(m_user.value(QStringLiteral("nickname")).toString())
                                 .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2));
        QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("保存成功"));
        dlg.accept();
    });

    // 余额充值
    connect(rechargeBtn, &QPushButton::clicked, &dlg, [&]() {
        QDialog rechargeDlg(&dlg);
        rechargeDlg.setWindowTitle(QStringLiteral("余额充值"));
        rechargeDlg.resize(320, 200);

        auto *rLay = new QVBoxLayout(&rechargeDlg);
        auto *curBalanceLabel = new QLabel(
            QStringLiteral("当前余额：%1 元")
                .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2), &rechargeDlg);
        auto *amountEdit = new QLineEdit(&rechargeDlg);
        amountEdit->setPlaceholderText(QStringLiteral("请输入充值金额（最多2位小数）"));
        amountEdit->setValidator(new QDoubleValidator(0.01, 999999.0, 2, &rechargeDlg));

        auto *confirmBtn = new QPushButton(QStringLiteral("确认充值"), &rechargeDlg);
        auto *cancelBtn2 = new QPushButton(QStringLiteral("取消"), &rechargeDlg);
        confirmBtn->setStyleSheet(m_refreshButton->styleSheet());
        cancelBtn2->setStyleSheet(m_refreshButton->styleSheet());

        auto *btnRow2 = new QHBoxLayout;
        btnRow2->addStretch();
        btnRow2->addWidget(confirmBtn);
        btnRow2->addWidget(cancelBtn2);

        rLay->addWidget(curBalanceLabel);
        rLay->addWidget(amountEdit);
        rLay->addStretch();
        rLay->addLayout(btnRow2);

        curBalanceLabel->setStyleSheet(m_statusLabel->styleSheet());
        amountEdit->setStyleSheet(m_latEdit->styleSheet());

        connect(cancelBtn2, &QPushButton::clicked, &rechargeDlg, &QDialog::reject);

        connect(confirmBtn, &QPushButton::clicked, &rechargeDlg, [&]() {
            const QString amountStr = amountEdit->text().trimmed();
            // 校验：非空、>0、最多2位小数
            if (amountStr.isEmpty()) {
                QMessageBox::warning(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("请输入有效充值金额"));
                return;
            }
            const QStringList parts = amountStr.split('.');
            if (parts.size() == 2 && parts[1].length() > 2) {
                QMessageBox::warning(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("请输入有效充值金额"));
                return;
            }
            const double amount = amountStr.toDouble();
            if (amount <= 0) {
                QMessageBox::warning(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("请输入有效充值金额"));
                return;
            }

            QJsonObject reqData;
            reqData["amount"] = amount;
            const QJsonObject resp = m_api->call(QStringLiteral("user.recharge"), reqData);
            if (!resp.value(QStringLiteral("ok")).toBool()) {
                const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
                QMessageBox::warning(&rechargeDlg, QStringLiteral("充值失败"),
                                     err.value(QStringLiteral("message")).toString());
                return;
            }
            const double newBalance = resp.value(QStringLiteral("data")).toObject()
                                         .value(QStringLiteral("balance")).toDouble();
            // 更新本地余额 + 个人中心显示 + 主窗口欢迎语
            m_user["balance"] = newBalance;
            balanceLabel->setText(QStringLiteral("余额：%1 元").arg(newBalance, 0, 'f', 2));
            curBalanceLabel->setText(QStringLiteral("当前余额：%1 元").arg(newBalance, 0, 'f', 2));
            m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                                     .arg(m_user.value(QStringLiteral("nickname")).toString())
                                     .arg(newBalance, 0, 'f', 2));
            QMessageBox::information(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("充值成功"));
            rechargeDlg.accept();
        });

        rechargeDlg.exec();
    });


    dlg.exec();
}

void MainWindow::onOrderHistory()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("订单历史"));
    dlg.resize(720, 500);

    auto *lay = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget(&dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    closeBtn->setStyleSheet(m_refreshButton->styleSheet());
    listWidget->setStyleSheet(m_stationList->styleSheet());

    lay->addWidget(listWidget);
    lay->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);

    QJsonObject reqData;
    reqData["limit"] = 100;
    const QJsonObject resp = m_api->call(QStringLiteral("order.list"), reqData);

    if (!resp.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
        QMessageBox::warning(&dlg, QStringLiteral("提示"),
                             err.value(QStringLiteral("message")).toString());
        dlg.exec();
        return;
    }

    const QJsonArray items = resp.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) {
        listWidget->addItem(QStringLiteral("暂无订单记录"));
    }

    for (const QJsonValue &val : items) {
        const QJsonObject order = val.toObject();
        const QString line = QStringLiteral("订单号:%1 | %2 | 电桩:%3 | 电量:%4 kWh | 金额:%5元 | %6 | %7")
                                 .arg(order.value(QStringLiteral("order_no")).toString())
                                 .arg(order.value(QStringLiteral("station_name")).toString())
                                 .arg(order.value(QStringLiteral("pile_no")).toString())
                                 .arg(order.value(QStringLiteral("kwh")).toDouble(), 0, 'f', 2)
                                 .arg(order.value(QStringLiteral("amount")).toDouble(), 0, 'f', 2)
                                 .arg(order.value(QStringLiteral("reserve_at")).toString())
                                 .arg(order.value(QStringLiteral("status")).toString());
        auto *item = new QListWidgetItem(line);
        // 把完整订单对象存到 item（转成 VariantMap 存，点击时还原）
        item->setData(Qt::UserRole, order.toVariantMap());
        listWidget->addItem(item);
    }

    // 点击列表项 → 弹出订单详情
    connect(listWidget, &QListWidget::itemClicked, &dlg, [&](QListWidgetItem *item) {
        if (!item || item->data(Qt::UserRole).isNull()) return;
        const QJsonObject order = QJsonObject::fromVariantMap(item->data(Qt::UserRole).toMap());
        const QString orderNo = order.value(QStringLiteral("order_no")).toString();
        const QString status = order.value(QStringLiteral("status")).toString();
        const double kwh = order.value(QStringLiteral("kwh")).toDouble();
        const double amount = order.value(QStringLiteral("amount")).toDouble();

        // 订单详情弹窗
        QDialog detailDlg(&dlg);
        detailDlg.setWindowTitle(QStringLiteral("订单详情"));
        detailDlg.resize(420, 380);
        auto *dLay = new QVBoxLayout(&detailDlg);

        auto *infoLabel = new QLabel(&detailDlg);
        infoLabel->setWordWrap(true);
        infoLabel->setStyleSheet(m_statusLabel->styleSheet());
        infoLabel->setText(QStringLiteral(
            "订单号：%1\n"
            "手机号：%2\n"
            "站点：%3\n"
            "电桩：%4\n"
            "状态：%5\n"
            "预约时间：%6\n"
            "开始时间：%7\n"
            "结束时间：%8\n"
            "电量：%9 kWh\n"
            "金额：%10 元")
            .arg(orderNo)
            .arg(order.value(QStringLiteral("phone")).toString())
            .arg(order.value(QStringLiteral("station_name")).toString())
            .arg(order.value(QStringLiteral("pile_no")).toString())
            .arg(status)
            .arg(order.value(QStringLiteral("reserve_at")).toString())
            .arg(order.value(QStringLiteral("start_at")).toString())
            .arg(order.value(QStringLiteral("end_at")).toString())
            .arg(kwh, 0, 'f', 2)
            .arg(amount, 0, 'f', 2));

        dLay->addWidget(infoLabel);
        dLay->addStretch();

        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();

        // 根据状态显示对应操作按钮
        if (status == QStringLiteral("预约")) {
            auto *startBtn = new QPushButton(QStringLiteral("开始充电"), &detailDlg);
            startBtn->setStyleSheet(m_refreshButton->styleSheet());
            btnRow->addWidget(startBtn);
            connect(startBtn, &QPushButton::clicked, &detailDlg, [&]() {
                QJsonObject startData;
                startData["order_no"] = orderNo;
                const QJsonObject startResp = m_api->call(QStringLiteral("charge.start"), startData);
                if (!startResp.value(QStringLiteral("ok")).toBool()) {
                    const QJsonObject err = startResp.value(QStringLiteral("error")).toObject();
                    QMessageBox::warning(&detailDlg, QStringLiteral("失败"),
                                         err.value(QStringLiteral("message")).toString());
                    return;
                }
                detailDlg.accept();
                dlg.accept();
                showChargingProgress(orderNo);
            });
        } else if (status == QStringLiteral("充电中")) {
            auto *progressBtn = new QPushButton(QStringLiteral("查看充电进度"), &detailDlg);
            progressBtn->setStyleSheet(m_refreshButton->styleSheet());
            btnRow->addWidget(progressBtn);
            connect(progressBtn, &QPushButton::clicked, &detailDlg, [&]() {
                detailDlg.accept();
                dlg.accept();
                showChargingProgress(orderNo);
            });
        } else if (status == QStringLiteral("待支付")) {
            auto *settleBtn = new QPushButton(QStringLiteral("去结算"), &detailDlg);
            settleBtn->setStyleSheet(m_refreshButton->styleSheet());
            btnRow->addWidget(settleBtn);
            connect(settleBtn, &QPushButton::clicked, &detailDlg, [&]() {
                detailDlg.accept();
                dlg.accept();
                showSettleDialog(orderNo, kwh, amount);
            });
        }

        auto *okBtn = new QPushButton(QStringLiteral("关闭"), &detailDlg);
        okBtn->setStyleSheet(m_refreshButton->styleSheet());
        btnRow->addWidget(okBtn);
        connect(okBtn, &QPushButton::clicked, &detailDlg, &QDialog::accept);

        dLay->addLayout(btnRow);
        detailDlg.exec();
    });

    dlg.exec();
}

void MainWindow::onFavoriteList()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("我的收藏"));
    dlg.resize(620, 450);

    auto *lay = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget(&dlg);
    auto *removeBtn = new QPushButton(QStringLiteral("取消收藏选中项"), &dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    removeBtn->setStyleSheet(m_refreshButton->styleSheet());
    closeBtn->setStyleSheet(m_refreshButton->styleSheet());
    listWidget->setStyleSheet(m_stationList->styleSheet());

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);

    lay->addWidget(listWidget);
    lay->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);

    // 加载收藏列表（lambda，取消收藏后可复用刷新）
    auto loadFavorites = [&]() {
        listWidget->clear();
        const QJsonObject resp = m_api->call(QStringLiteral("station.favorite.list"), QJsonObject());
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        const QJsonArray items = resp.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            listWidget->addItem(QStringLiteral("暂无收藏的充电站"));
            return;
        }
        for (const QJsonValue &val : items) {
            const QJsonObject station = val.toObject();
            const QString line = QStringLiteral("%1 | 地址:%2 | %3元/度 | 空闲桩:%4")
                                     .arg(station.value(QStringLiteral("name")).toString())
                                     .arg(station.value(QStringLiteral("address")).toString())
                                     .arg(station.value(QStringLiteral("price")).toDouble(), 0, 'f', 2)
                                     .arg(station.value(QStringLiteral("idle_piles")).toInt());
            auto *item = new QListWidgetItem(line);
            item->setData(Qt::UserRole, station.value(QStringLiteral("id")).toInt());
            listWidget->addItem(item);
        }
    };

    loadFavorites();

    connect(removeBtn, &QPushButton::clicked, &dlg, [&]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item || item->data(Qt::UserRole).isNull()) {
            QMessageBox::information(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("请先选择要取消收藏的电站"));
            return;
        }
        const int stationId = item->data(Qt::UserRole).toInt();
        QJsonObject reqData;
        reqData["station_id"] = stationId;
        const QJsonObject resp = m_api->call(QStringLiteral("station.favorite.remove"), reqData);
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        QMessageBox::information(&dlg, QStringLiteral("提示"), QStringLiteral("已取消收藏"));
        loadFavorites();
    });

    dlg.exec();
}

bool MainWindow::checkOpenOrder(bool failClosed)
{
    const QJsonObject resp = m_api->call(QStringLiteral("order.check_open"), QJsonObject());
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        if (failClosed) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 err.value(QStringLiteral("message")).toString(
                                     QStringLiteral("无法检查未完成订单，请稍后重试")));
            return true;
        }
        return false;
    }

    const QJsonObject data = resp.value(QStringLiteral("data")).toObject();
    if (!data.value(QStringLiteral("has_open")).toBool()) return false;

    const QJsonObject order = data.value(QStringLiteral("order")).toObject();
    const QString orderNo = order.value(QStringLiteral("order_no")).toString();
    const QString status = order.value(QStringLiteral("status")).toString();
    const double kwh = order.value(QStringLiteral("kwh")).toDouble();
    const double amount = order.value(QStringLiteral("amount")).toDouble();

    // 自定义弹窗（替代 QMessageBox，方便加跳转按钮）
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("提示"));
    dlg.resize(380, 280);
    auto *lay = new QVBoxLayout(&dlg);
    auto *label = new QLabel(&dlg);
    label->setWordWrap(true);
    label->setText(QStringLiteral(
        "您有未完成的充电订单，请先处理\n\n"
        "订单号：%1\n"
        "状态：%2\n"
        "站点：%3\n"
        "电桩：%4\n"
        "电量：%5 kWh\n"
        "金额：%6 元")
        .arg(orderNo)
        .arg(status)
        .arg(order.value(QStringLiteral("station_name")).toString())
        .arg(order.value(QStringLiteral("pile_no")).toString())
        .arg(kwh, 0, 'f', 2)
        .arg(amount, 0, 'f', 2));
    label->setStyleSheet(m_statusLabel->styleSheet());

    auto *actionBtn = new QPushButton(&dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), &dlg);
    actionBtn->setStyleSheet(m_refreshButton->styleSheet());
    closeBtn->setStyleSheet(m_refreshButton->styleSheet());

    if (status == QStringLiteral("充电中"))
        actionBtn->setText(QStringLiteral("查看充电进度"));
    else if (status == QStringLiteral("预约"))
        actionBtn->setText(QStringLiteral("去开始充电"));
    else
        actionBtn->setText(QStringLiteral("去结算"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    btnRow->addWidget(actionBtn);
    lay->addWidget(label);
    lay->addStretch();
    lay->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    bool handled = false;
    connect(actionBtn, &QPushButton::clicked, &dlg, [&]() { handled = true; dlg.accept(); });
    dlg.exec();

    if (handled) {
        if (status == QStringLiteral("充电中")) {
            showChargingProgress(orderNo);
        } else if (status == QStringLiteral("待支付")) {
            showSettleDialog(orderNo, kwh, amount);
        } else if (status == QStringLiteral("预约")) {
            const auto ret = QMessageBox::question(this, QStringLiteral("预约订单"),
                QStringLiteral("订单 %1 处于预约状态，是否开始充电？").arg(orderNo),
                QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                QJsonObject startData;
                startData["order_no"] = orderNo;
                const QJsonObject startResp = m_api->call(QStringLiteral("charge.start"), startData);
                if (startResp.value(QStringLiteral("ok")).toBool()) {
                    showChargingProgress(orderNo);
                } else {
                    const QJsonObject err = startResp.value(QStringLiteral("error")).toObject();
                    QMessageBox::warning(this, QStringLiteral("失败"),
                                         err.value(QStringLiteral("message")).toString());
                }
            }
        }
    }
    return true;
}

void MainWindow::showChargingProgress(const QString &orderNo)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("充电中"));
    dlg.resize(420, 360);
    dlg.setModal(true);

    auto *lay = new QVBoxLayout(&dlg);
    auto *orderLabel = new QLabel(QStringLiteral("订单号：%1").arg(orderNo), &dlg);
    auto *kwhLabel = new QLabel(&dlg);
    auto *timeLabel = new QLabel(&dlg);
    auto *remainLabel = new QLabel(&dlg);
    auto *amountLabel = new QLabel(&dlg);
    auto *statusLabel = new QLabel(&dlg);
    auto *stopBtn = new QPushButton(QStringLiteral("停止充电"), &dlg);

    const QString blackLabel = QStringLiteral(
        "color: #000000; font-size: 15px; font-weight: bold; padding: 4px 0px;");
    orderLabel->setStyleSheet(blackLabel);
    kwhLabel->setStyleSheet(blackLabel);
    timeLabel->setStyleSheet(blackLabel);
    remainLabel->setStyleSheet(blackLabel);
    amountLabel->setStyleSheet(blackLabel);

    statusLabel->setStyleSheet(QStringLiteral("color: #333333; font-size: 13px; font-style: italic; padding: 4px 0px;"));
    stopBtn->setStyleSheet(m_refreshButton->styleSheet());

    lay->addWidget(orderLabel);
    lay->addSpacing(10);
    lay->addWidget(kwhLabel);
    lay->addWidget(timeLabel);
    lay->addWidget(remainLabel);
    lay->addWidget(amountLabel);
    lay->addStretch();
    lay->addWidget(statusLabel);
    lay->addWidget(stopBtn);

    QTimer *timer = new QTimer(&dlg);

    // 刷新进度的 lambda
    auto refresh = [&]() {
        QJsonObject data;
        data["order_no"] = orderNo;
        const QJsonObject resp = m_api->call(QStringLiteral("charge.progress"), data);
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            statusLabel->setText(QStringLiteral("状态：刷新失败，请检查网络"));
            return;
        }
        const QJsonObject d = resp.value(QStringLiteral("data")).toObject();
        const QString orderStatus = d.value(QStringLiteral("status")).toString();
        if (orderStatus != QStringLiteral("充电中")) {
            timer->stop();
            dlg.accept();
            if (orderStatus == QStringLiteral("待支付")) {
                showSettleDialog(orderNo,
                                 d.value(QStringLiteral("kwh")).toDouble(),
                                 d.value(QStringLiteral("amount")).toDouble());
            } else {
                QMessageBox::information(this, QStringLiteral("提示"),
                    QStringLiteral("订单状态已变为「%1」，充电窗口已关闭。").arg(orderStatus));
            }
            return;
        }
        const double kwh = d.value(QStringLiteral("kwh")).toDouble();
        const double amount = d.value(QStringLiteral("amount")).toDouble();
        const qint64 elapsed = d.value(QStringLiteral("elapsed_seconds")).toInteger();
        const qint64 remain = d.value(QStringLiteral("estimated_remain_seconds")).toInteger();
        const int h = static_cast<int>(elapsed / 3600);
        const int m = static_cast<int>((elapsed % 3600) / 60);
        const int s = static_cast<int>(elapsed % 60);

        kwhLabel->setText(QStringLiteral("已充电量：%1 kWh").arg(kwh, 0, 'f', 2));
        timeLabel->setText(QStringLiteral("已用时长：%1时%2分%3秒").arg(h).arg(m).arg(s));
        remainLabel->setText(remain > 0
            ? QStringLiteral("预估剩余：%1秒").arg(remain)
            : QStringLiteral("预估剩余：--"));
        amountLabel->setText(QStringLiteral("累计费用：%1 元").arg(amount, 0, 'f', 2));
        statusLabel->setText(QStringLiteral("状态：%1（实时刷新中…）")
                                 .arg(d.value(QStringLiteral("status")).toString()));
    };

    refresh();  // 立即刷一次

    connect(timer, &QTimer::timeout, &dlg, refresh);
    timer->start(2000);

    // 停止充电
    connect(stopBtn, &QPushButton::clicked, &dlg, [&]() {
        timer->stop();
        const auto ret = QMessageBox::question(&dlg, QStringLiteral("确认"),
            QStringLiteral("确定要停止充电吗？"), QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            timer->start(2000);
            return;
        }
        QJsonObject data;
        data["order_no"] = orderNo;
        const QJsonObject resp = m_api->call(QStringLiteral("charge.stop"), data);
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("停止失败"),
                                 err.value(QStringLiteral("message")).toString());
            timer->start(2000);
            return;
        }
        const double kwh = resp.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("kwh")).toDouble();
        const double amount = resp.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("amount")).toDouble();
        dlg.accept();
        showSettleDialog(orderNo, kwh, amount);  // 进入结算
    });

    dlg.exec();
}

void MainWindow::showSettleDialog(const QString &orderNo, double kwh, double amount)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("订单结算"));
    dlg.resize(400, 300);
    dlg.setModal(true);

    auto *lay = new QVBoxLayout(&dlg);
    auto *orderLabel = new QLabel(QStringLiteral("订单号：%1").arg(orderNo), &dlg);
    auto *kwhLabel = new QLabel(QStringLiteral("充电量：%1 kWh").arg(kwh, 0, 'f', 2), &dlg);
    auto *amountLabel = new QLabel(
        QStringLiteral("应付金额：%1 元").arg(amount, 0, 'f', 2), &dlg);
    auto *balanceLabel = new QLabel(
        QStringLiteral("当前余额：%1 元")
            .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2), &dlg);
    auto *settleBtn = new QPushButton(QStringLiteral("确认结算"), &dlg);
    auto *rechargeBtn = new QPushButton(QStringLiteral("去充值"), &dlg);
    auto *closeBtn = new QPushButton(QStringLiteral("稍后结算"), &dlg);

    const QString blackLabel = QStringLiteral(
        "color: #000000; font-size: 15px; font-weight: bold; padding: 4px 0px;");
    orderLabel->setStyleSheet(blackLabel);
    kwhLabel->setStyleSheet(blackLabel);
    amountLabel->setStyleSheet(blackLabel);
    balanceLabel->setStyleSheet(QStringLiteral(
        "color: #333333; font-size: 13px; padding: 4px 0px;"));

    settleBtn->setStyleSheet(m_refreshButton->styleSheet());
    rechargeBtn->setStyleSheet(m_refreshButton->styleSheet());
    closeBtn->setStyleSheet(m_refreshButton->styleSheet());

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(rechargeBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    btnRow->addWidget(settleBtn);

    lay->addWidget(orderLabel);
    lay->addSpacing(10);
    lay->addWidget(kwhLabel);
    lay->addWidget(amountLabel);
    lay->addWidget(balanceLabel);
    lay->addStretch();
    lay->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(rechargeBtn, &QPushButton::clicked, &dlg, [&]() {
        QDialog rechargeDlg(&dlg);
        rechargeDlg.setWindowTitle(QStringLiteral("余额充值"));
        rechargeDlg.resize(320, 200);

        auto *rLay = new QVBoxLayout(&rechargeDlg);
        auto *hint = new QLabel(
            QStringLiteral("当前余额：%1 元，应付：%2 元")
                .arg(m_user.value(QStringLiteral("balance")).toDouble(), 0, 'f', 2)
                .arg(amount, 0, 'f', 2),
            &rechargeDlg);
        auto *amountEdit = new QLineEdit(&rechargeDlg);
        amountEdit->setPlaceholderText(QStringLiteral("请输入充值金额（最多2位小数）"));
        amountEdit->setValidator(new QDoubleValidator(0.01, 999999.0, 2, &rechargeDlg));
        const double need = qMax(0.01, amount - m_user.value(QStringLiteral("balance")).toDouble());
        amountEdit->setText(QString::number(qMax(need, 1.0), 'f', 2));

        auto *confirmBtn = new QPushButton(QStringLiteral("确认充值"), &rechargeDlg);
        auto *cancelBtn2 = new QPushButton(QStringLiteral("取消"), &rechargeDlg);
        confirmBtn->setStyleSheet(m_refreshButton->styleSheet());
        cancelBtn2->setStyleSheet(m_refreshButton->styleSheet());

        auto *btnRow2 = new QHBoxLayout;
        btnRow2->addStretch();
        btnRow2->addWidget(cancelBtn2);
        btnRow2->addWidget(confirmBtn);
        rLay->addWidget(hint);
        rLay->addWidget(amountEdit);
        rLay->addLayout(btnRow2);

        connect(cancelBtn2, &QPushButton::clicked, &rechargeDlg, &QDialog::reject);
        connect(confirmBtn, &QPushButton::clicked, &rechargeDlg, [&]() {
            const QString text = amountEdit->text().trimmed();
            bool ok = false;
            const double rechargeAmount = text.toDouble(&ok);
            if (!ok || rechargeAmount <= 0) {
                QMessageBox::warning(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("请输入有效充值金额"));
                return;
            }
            QJsonObject reqData;
            reqData["amount"] = rechargeAmount;
            const QJsonObject resp = m_api->call(QStringLiteral("user.recharge"), reqData);
            if (!resp.value(QStringLiteral("ok")).toBool()) {
                const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
                QMessageBox::warning(&rechargeDlg, QStringLiteral("充值失败"),
                                     err.value(QStringLiteral("message")).toString());
                return;
            }
            const double newBalance = resp.value(QStringLiteral("data")).toObject()
                                          .value(QStringLiteral("balance_after")).toDouble();
            m_user["balance"] = newBalance;
            m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                                     .arg(m_user.value(QStringLiteral("nickname")).toString())
                                     .arg(newBalance, 0, 'f', 2));
            balanceLabel->setText(QStringLiteral("当前余额：%1 元").arg(newBalance, 0, 'f', 2));
            QMessageBox::information(&rechargeDlg, QStringLiteral("提示"), QStringLiteral("充值成功"));
            rechargeDlg.accept();
        });

        rechargeDlg.exec();
    });

    connect(settleBtn, &QPushButton::clicked, &dlg, [&]() {
        QJsonObject data;
        data["order_no"] = orderNo;
        const QJsonObject resp = m_api->call(QStringLiteral("charge.settle"), data);
        if (!resp.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject err = resp.value(QStringLiteral("error")).toObject();
            QMessageBox::warning(&dlg, QStringLiteral("结算失败"),
                                 err.value(QStringLiteral("message")).toString());
            return;
        }
        const double newBalance = resp.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("balance_after")).toDouble();
        m_user["balance"] = newBalance;
        m_userLabel->setText(QStringLiteral("欢迎，%1 | 余额 %2 元")
                                 .arg(m_user.value(QStringLiteral("nickname")).toString())
                                 .arg(newBalance, 0, 'f', 2));
        QMessageBox::information(&dlg, QStringLiteral("结算成功"),
            QStringLiteral("结算成功！扣除 %1 元，余额 %2 元")
                .arg(amount, 0, 'f', 2).arg(newBalance, 0, 'f', 2));
        dlg.accept();
    });

    dlg.exec();
}

