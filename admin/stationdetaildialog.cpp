#include "stationdetaildialog.h"
#include "ui_stationdetaildialog.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QTableWidgetItem>

StationDetailDialog::StationDetailDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::StationDetailDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("电站详情"));
    resize(560, 480);

    ui->btnViewPiles->setProperty("class", "primary");
    ui->btnViewPiles->setCursor(Qt::PointingHandCursor);
    ui->btnClose->setCursor(Qt::PointingHandCursor);

    ui->tablePiles->setColumnCount(4);
    ui->tablePiles->setHorizontalHeaderLabels({
        QStringLiteral("电桩编号"),
        QStringLiteral("类型"),
        QStringLiteral("功率(kW)"),
        QStringLiteral("状态"),
    });
    ui->tablePiles->verticalHeader()->setVisible(false);
    ui->tablePiles->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tablePiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tablePiles->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tablePiles->setAlternatingRowColors(true);

    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnViewPiles, &QPushButton::clicked, this, &StationDetailDialog::onViewPilesClicked);
}

StationDetailDialog::~StationDetailDialog()
{
    delete ui;
}

void StationDetailDialog::setDetail(const QJsonObject &detail)
{
    const QJsonObject station = detail.value(QStringLiteral("station")).toObject();
    m_stationId = station.value(QStringLiteral("id")).toInt();

    ui->valId->setText(QString::number(m_stationId));
    ui->valName->setText(station.value(QStringLiteral("name")).toString());
    ui->valAddress->setText(station.value(QStringLiteral("address")).toString());
    ui->valLatLng->setText(
        QStringLiteral("%1, %2")
            .arg(station.value(QStringLiteral("lat")).toDouble(), 0, 'f', 6)
            .arg(station.value(QStringLiteral("lng")).toDouble(), 0, 'f', 6));
    ui->valPrice->setText(
        QStringLiteral("%1 元/kWh").arg(station.value(QStringLiteral("price")).toDouble(), 0, 'f', 2));
    const double onlineRate = station.value(QStringLiteral("online_rate")).toDouble();
    ui->valOnlineRate->setText(QStringLiteral("%1%").arg(onlineRate * 100.0, 0, 'f', 1));

    ui->tablePiles->setRowCount(0);
    const QJsonArray piles = detail.value(QStringLiteral("piles")).toArray();
    for (const QJsonValue &value : piles) {
        const QJsonObject pile = value.toObject();
        const int row = ui->tablePiles->rowCount();
        ui->tablePiles->insertRow(row);
        ui->tablePiles->setItem(row, 0, new QTableWidgetItem(pile.value(QStringLiteral("pile_no")).toString()));
        ui->tablePiles->setItem(row, 1, new QTableWidgetItem(pile.value(QStringLiteral("type")).toString()));
        ui->tablePiles->setItem(row, 2, new QTableWidgetItem(
            QString::number(pile.value(QStringLiteral("power_kw")).toDouble(), 'f', 1)));
        ui->tablePiles->setItem(row, 3, new QTableWidgetItem(pile.value(QStringLiteral("status")).toString()));
    }
}

int StationDetailDialog::stationId() const
{
    return m_stationId;
}

void StationDetailDialog::onViewPilesClicked()
{
    if (m_stationId > 0) {
        emit viewPilesRequested(m_stationId);
    }
}
