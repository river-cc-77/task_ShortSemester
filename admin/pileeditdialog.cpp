#include "pileeditdialog.h"
#include "ui_pileeditdialog.h"

PileEditDialog::PileEditDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PileEditDialog)
{
    ui->setupUi(this);
    ui->spinPower->setRange(0.01, 500.0);
    ui->spinPower->setDecimals(2);
    ui->cboxStatus->clear();
    ui->cboxStatus->addItems({
        QStringLiteral("闲置"),
        QStringLiteral("预约"),
        QStringLiteral("在用"),
        QStringLiteral("故障"),
    });
    connect(ui->btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

PileEditDialog::~PileEditDialog()
{
    delete ui;
}

void PileEditDialog::setData(const QString &pileNo, const QString &type, double powerKw, const QString &status)
{
    m_oldPileNo = pileNo;
    m_oldType = type;
    m_oldPower = powerKw;
    m_oldStatus = status;

    ui->lblPileNoValue->setText(pileNo);
    ui->cboxType->setCurrentText(type);
    ui->spinPower->setValue(powerKw);
    ui->cboxStatus->setCurrentText(status);

    const bool inUse = (status == QStringLiteral("预约") || status == QStringLiteral("在用"));
    ui->cboxStatus->setEnabled(!inUse);
}

QJsonObject PileEditDialog::getUpdateParams()
{
    QJsonObject obj;
    const QString newType = ui->cboxType->currentText();
    const double newPower = ui->spinPower->value();
    const QString newStatus = ui->cboxStatus->currentText();

    if (newType != m_oldType) {
        obj["type"] = newType;
    }
    if (newPower != m_oldPower) {
        obj["power_kw"] = newPower;
    }
    if (newStatus != m_oldStatus && ui->cboxStatus->isEnabled()) {
        obj["status"] = newStatus;
    }
    return obj;
}
