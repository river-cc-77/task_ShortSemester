#ifndef MAPNAVIGATIONDIALOG_H
#define MAPNAVIGATIONDIALOG_H

#include <QDialog>
#include <QString>

class MapNavigationDialog : public QDialog
{
    Q_OBJECT
public:
    MapNavigationDialog(double originLat, double originLng,
                        double destLat, double destLng,
                        const QString &destName,
                        const QString &baiduAk,
                        QWidget *parent = nullptr);
};

#endif // MAPNAVIGATIONDIALOG_H
