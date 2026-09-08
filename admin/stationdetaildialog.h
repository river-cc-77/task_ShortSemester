#ifndef STATIONDETAILDIALOG_H
#define STATIONDETAILDIALOG_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
class StationDetailDialog;
}

class StationDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StationDetailDialog(QWidget *parent = nullptr);
    ~StationDetailDialog();

    void setDetail(const QJsonObject &detail);
    int stationId() const;

signals:
    void viewPilesRequested(int stationId);

private slots:
    void onViewPilesClicked();

private:
    Ui::StationDetailDialog *ui;
    int m_stationId = 0;
};

#endif // STATIONDETAILDIALOG_H
