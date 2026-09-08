#ifndef STATIONEDITDIALOG_H
#define STATIONEDITDIALOG_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
class StationEditDialog;
}

class StationEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StationEditDialog(QWidget *parent = nullptr);
    ~StationEditDialog();

    void setCreateMode();
    void setEditMode(const QJsonObject &station);

    QJsonObject getCreateParams() const;
    QJsonObject getUpdateParams() const;

private:
    Ui::StationEditDialog *ui;
    bool m_createMode = true;
    int m_stationId = 0;
};

#endif // STATIONEDITDIALOG_H
