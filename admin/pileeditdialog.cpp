#include "pileeditdialog.h"
#include "ui_pileeditdialog.h"

PileEditDialog::PileEditDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PileEditDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("编辑电桩"));
    ui->btnOk->setProperty("class", "primary");
    ui->btnOk->setCursor(Qt::PointingHandCursor);
    ui->btnCancel->setCursor(Qt::PointingHandCursor);
    ui->spinPower->setRange(0.01, 500.0);
    ui->spinPower->setDecimals(2);
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

    ui->cboxStatus->clear();
    const bool inUse = (status == QStringLiteral("预约") || status == QStringLiteral("在用"));
    if (inUse) {
        ui->cboxStatus->addItem(status);
        ui->cboxStatus->setCurrentIndex(0);
        ui->cboxStatus->setEnabled(false);
    } else {
        ui->cboxStatus->addItems({
            QStringLiteral("闲置"),
            QStringLiteral("故障"),
        });
        ui->cboxStatus->setCurrentText(status == QStringLiteral("故障")
                                           ? QStringLiteral("故障")
                                           : QStringLiteral("闲置"));
        ui->cboxStatus->setEnabled(true);
    }
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
