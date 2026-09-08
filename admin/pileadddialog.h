#ifndef PILEADDDIALOG_H
#define PILEADDDIALOG_H

#include <QDialog>
#include <QJsonObject>

class ApiClient;

namespace Ui {
class PileAddDialog;
}

class PileAddDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PileAddDialog(QWidget *parent = nullptr);
    ~PileAddDialog();

    bool loadStations(ApiClient *api);
    QJsonObject getCreateParams() const;

private slots:
    void onTypeChanged(const QString &type);

private:
    Ui::PileAddDialog *ui;
};

#endif // PILEADDDIALOG_H
