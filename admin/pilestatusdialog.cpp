#include "pilestatusdialog.h"
#include "ui_pilestatusdialog.h"

PileStatusDialog::PileStatusDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PileStatusDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("电桩状态详情"));
    ui->btnClose->setCursor(Qt::PointingHandCursor);
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::accept);
}

PileStatusDialog::~PileStatusDialog()
{
    delete ui;
}

void PileStatusDialog::setDetail(const QJsonObject &detail)
{
    ui->valPileNo->setText(detail.value("pile_no").toString());
    ui->valStation->setText(detail.value("station_name").toString());
    ui->valType->setText(detail.value("type").toString());
    ui->valPower->setText(QString::number(detail.value("power_kw").toDouble()));
    ui->valStatus->setText(detail.value("status").toString());
    ui->valChargeCount->setText(QString::number(detail.value("charge_count").toInt()));
    ui->valChargeMinutes->setText(QString::number(detail.value("charge_minutes").toInt()));

    const QJsonValue orderVal = detail.value("current_order");
    if (orderVal.isNull() || orderVal.isUndefined() || !orderVal.isObject()) {
        ui->groupOrder->setVisible(false);
        return;
    }

    const QJsonObject order = orderVal.toObject();
    ui->groupOrder->setVisible(true);
    ui->valOrderNo->setText(order.value("order_no").toString());
    ui->valOrderStatus->setText(order.value("status").toString());
    ui->valPhone->setText(order.value("phone").toString());
    ui->valKwh->setText(QString::number(order.value("kwh").toDouble()));
    ui->valAmount->setText(QString::number(order.value("amount").toDouble()));
    ui->valReserveAt->setText(order.value("reserve_at").toString());
    ui->valStartAt->setText(order.value("start_at").toString());
}
