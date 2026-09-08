#ifndef ORDERDETAILDIALOG_H
#define ORDERDETAILDIALOG_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
class OrderDetailDialog;
}

class OrderDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OrderDetailDialog(QWidget *parent = nullptr);
    ~OrderDetailDialog();

    void setOrder(const QJsonObject &order);
    QString orderNo() const;

signals:
    void adminSettleRequested(const QString &orderNo);

private slots:
    void onSettleClicked();

private:
    Ui::OrderDetailDialog *ui;
    QJsonObject m_order;
};

#endif // ORDERDETAILDIALOG_H
