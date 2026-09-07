#ifndef PILESTATUSDIALOG_H
#define PILESTATUSDIALOG_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
class PileStatusDialog;
}

class PileStatusDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PileStatusDialog(QWidget *parent = nullptr);
    ~PileStatusDialog();

    void setDetail(const QJsonObject &detail);

private:
    Ui::PileStatusDialog *ui;
};

#endif // PILESTATUSDIALOG_H
