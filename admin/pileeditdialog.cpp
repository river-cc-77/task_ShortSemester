#include "pileeditdialog.h"
#include "ui_pileeditdialog.h"

PileEditDialog::PileEditDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PileEditDialog)
{
    ui->setupUi(this);
    // 手动绑定确定/取消按钮
    connect(ui->btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

PileEditDialog::~PileEditDialog()
{
    delete ui;
}

//回填旧数据
void PileEditDialog::setData(const QString &pileNo, const QString &type, double powerKw, const QString &status)
{
    m_oldPileNo = pileNo;
    m_oldType = type;
    m_oldPower = powerKw;
    m_oldStatus = status;

    // 桩号仅展示，假设UI控件叫 labelPileNo（QLabel），如果你的label名字不一样，改成你UI真实objectName
    ui->labelPileNo->setText(pileNo);
    ui->cboxType->setCurrentText(type);
    ui->spinPower->setValue(powerKw);
    ui->cboxStatus->setCurrentText(status);
}

//对比新旧，只返回被修改的字段（协议要求，不传的字段后端保留原值）
QJsonObject PileEditDialog::getUpdateParams()
{
    QJsonObject obj;
    QString newType = ui->cboxType->currentText();
    double newPower = ui->spinPower->value();
    QString newStatus = ui->cboxStatus->currentText();

    if(newType != m_oldType){
        obj["type"] = newType;
    }
    if(newPower != m_oldPower){
        obj["power_kw"] = newPower;
    }
    if(newStatus != m_oldStatus){
        obj["status"] = newStatus;
    }
    return obj;
}
