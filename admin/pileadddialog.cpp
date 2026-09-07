#include "pileadddialog.h"
#include "ui_pileadddialog.h"
#include "apiclient.h"

#include <QJsonArray>
#include <QMessageBox>

PileAddDialog::PileAddDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PileAddDialog)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("新增电桩"));

    connect(ui->btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->cboxType, &QComboBox::currentTextChanged, this, &PileAddDialog::onTypeChanged);

    ui->spinPower->setDecimals(1);
    ui->spinPower->setMinimum(0.1);
    ui->spinPower->setMaximum(1000.0);
    onTypeChanged(ui->cboxType->currentText());
}

PileAddDialog::~PileAddDialog()
{
    delete ui;
}

bool PileAddDialog::loadStations(ApiClient *api)
{
    ui->comboStation->clear();
    const QJsonObject resp = api->call(QStringLiteral("station.admin.list"));
    if (!resp.value("ok").toBool()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             resp.value("error").toObject().value("message").toString(
                                 QStringLiteral("获取电站列表失败")));
        return false;
    }

    const QJsonArray items = resp.value("data").toObject().value("items").toArray();
    if (items.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("暂无可用电站"));
        return false;
    }

    for (const QJsonValue &value : items) {
        const QJsonObject row = value.toObject();
        ui->comboStation->addItem(row.value("name").toString(), row.value("id").toInt());
    }
    return true;
}

QJsonObject PileAddDialog::getCreateParams() const
{
    QJsonObject obj;
    obj["station_id"] = ui->comboStation->currentData().toInt();
    obj["type"] = ui->cboxType->currentText();
    obj["power_kw"] = ui->spinPower->value();

    const QString pileNo = ui->editPileNo->text().trimmed();
    if (!pileNo.isEmpty()) {
        obj["pile_no"] = pileNo;
    }
    return obj;
}

void PileAddDialog::onTypeChanged(const QString &type)
{
    if (type == QStringLiteral("快充")) {
        ui->spinPower->setValue(120.0);
    } else {
        ui->spinPower->setValue(7.0);
    }
}
