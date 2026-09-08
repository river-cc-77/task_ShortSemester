#include "orderdetaildialog.h"
#include "ui_orderdetaildialog.h"

OrderDetailDialog::OrderDetailDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OrderDetailDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("订单详情"));
    ui->btnSettle->setProperty("class", "success");
    ui->btnSettle->setCursor(Qt::PointingHandCursor);
    ui->btnClose->setCursor(Qt::PointingHandCursor);
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnSettle, &QPushButton::clicked, this, &OrderDetailDialog::onSettleClicked);
}

OrderDetailDialog::~OrderDetailDialog()
{
    delete ui;
}

void OrderDetailDialog::setOrder(const QJsonObject &order)
{
    m_order = order;
    ui->valOrderNo->setText(order.value("order_no").toString());
    ui->valPhone->setText(order.value("phone").toString());
    ui->valStation->setText(order.value("station_name").toString());
    ui->valPileNo->setText(order.value("pile_no").toString());
    ui->valStatus->setText(order.value("status").toString());
    ui->valReserveAt->setText(order.value("reserve_at").toString());
    ui->valStartAt->setText(order.value("start_at").toString());
    ui->valEndAt->setText(order.value("end_at").toString());
    ui->valKwh->setText(QString::number(order.value("kwh").toDouble()));
    ui->valAmount->setText(QString::number(order.value("amount").toDouble()));

    const bool canSettle = order.value("status").toString() == QStringLiteral("待支付");
    ui->btnSettle->setVisible(canSettle);
}

QString OrderDetailDialog::orderNo() const
{
    return m_order.value("order_no").toString();
}

void OrderDetailDialog::onSettleClicked()
{
    emit adminSettleRequested(orderNo());
}
