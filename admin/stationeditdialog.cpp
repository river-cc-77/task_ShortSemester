#include "stationeditdialog.h"
#include "ui_stationeditdialog.h"

#include <QMessageBox>

StationEditDialog::StationEditDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::StationEditDialog)
{
    ui->setupUi(this);

    ui->spinLat->setDecimals(6);
    ui->spinLat->setRange(-90.0, 90.0);
    ui->spinLng->setDecimals(6);
    ui->spinLng->setRange(-180.0, 180.0);
    ui->spinPrice->setDecimals(2);
    ui->spinPrice->setRange(0.01, 999.0);
    ui->spinFastCount->setRange(0, 99);
    ui->spinSlowCount->setRange(0, 99);

    connect(ui->btnOk, &QPushButton::clicked, this, [this]() {
        if (ui->editName->text().trimmed().isEmpty() || ui->editAddress->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("站名和地址不能为空"));
            return;
        }
        if (m_createMode
            && ui->spinFastCount->value() <= 0 && ui->spinSlowCount->value() <= 0) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("至少创建一个快充或慢充电桩"));
            return;
        }
        accept();
    });
    connect(ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

StationEditDialog::~StationEditDialog()
{
    delete ui;
}

void StationEditDialog::setCreateMode()
{
    m_createMode = true;
    m_stationId = 0;
    setWindowTitle(QStringLiteral("新增电站"));
    ui->widgetPileCount->setVisible(true);

    ui->editName->clear();
    ui->editAddress->clear();
    ui->spinLat->setValue(22.5431);
    ui->spinLng->setValue(114.0579);
    ui->spinPrice->setValue(1.20);
    ui->spinFastCount->setValue(2);
    ui->spinSlowCount->setValue(2);
}

void StationEditDialog::setEditMode(const QJsonObject &station)
{
    m_createMode = false;
    m_stationId = station.value(QStringLiteral("id")).toInt();
    setWindowTitle(QStringLiteral("编辑电站"));
    ui->widgetPileCount->setVisible(false);

    ui->editName->setText(station.value(QStringLiteral("name")).toString());
    ui->editAddress->setText(station.value(QStringLiteral("address")).toString());
    ui->spinLat->setValue(station.value(QStringLiteral("lat")).toDouble());
    ui->spinLng->setValue(station.value(QStringLiteral("lng")).toDouble());
    ui->spinPrice->setValue(station.value(QStringLiteral("price")).toDouble());
}

QJsonObject StationEditDialog::getCreateParams() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = ui->editName->text().trimmed();
    obj[QStringLiteral("address")] = ui->editAddress->text().trimmed();
    obj[QStringLiteral("lat")] = ui->spinLat->value();
    obj[QStringLiteral("lng")] = ui->spinLng->value();
    obj[QStringLiteral("price")] = ui->spinPrice->value();
    obj[QStringLiteral("fast_count")] = ui->spinFastCount->value();
    obj[QStringLiteral("slow_count")] = ui->spinSlowCount->value();
    return obj;
}

QJsonObject StationEditDialog::getUpdateParams() const
{
    QJsonObject obj;
    obj[QStringLiteral("station_id")] = m_stationId;
    obj[QStringLiteral("name")] = ui->editName->text().trimmed();
    obj[QStringLiteral("address")] = ui->editAddress->text().trimmed();
    obj[QStringLiteral("lat")] = ui->spinLat->value();
    obj[QStringLiteral("lng")] = ui->spinLng->value();
    obj[QStringLiteral("price")] = ui->spinPrice->value();
    return obj;
}
