#ifndef PILEEDITDIALOG_H
#define PILEEDITDIALOG_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
class PileEditDialog;
}

class PileEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PileEditDialog(QWidget *parent = nullptr);
    ~PileEditDialog();

    //回填原始数据到界面
    void setData(const QString& pileNo,
                 const QString& type,
                 double powerKw,
                 const QString& status);

    //读取界面，获取要修改的json（只返回改动字段）
    QJsonObject getUpdateParams();

private:
    Ui::PileEditDialog *ui;
    //保存原始值，用来对比用户有没有修改
    QString m_oldPileNo;
    QString m_oldType;
    double m_oldPower;
    QString m_oldStatus;
};

#endif // PILEEDITDIALOG_H
