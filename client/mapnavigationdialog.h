#ifndef MAPNAVIGATIONDIALOG_H
#define MAPNAVIGATIONDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QRadioButton;
class QNetworkAccessManager;

class MapNavigationDialog : public QDialog
{
    Q_OBJECT
public:
    enum class NavMode { Driving, Walking };

    MapNavigationDialog(const QString &originDesc,
                        double originLat, double originLng,
                        const QString &destName, double destLat, double destLng,
                        const QString &destAddress = QString(),
                        QWidget *parent = nullptr);

private slots:
    void checkRouteAvailability();
    void onStartNavigation();

private:
    QString buildBaiduDirectionUrl(NavMode mode) const;
    NavMode selectedMode() const;

    QString m_originDesc;
    double m_originLat = 0;
    double m_originLng = 0;
    QString m_destName;
    double m_destLat = 0;
    double m_destLng = 0;
    QString m_region;

    QLabel *m_originLabel = nullptr;
    QLabel *m_destLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QRadioButton *m_drivingRadio = nullptr;
    QRadioButton *m_walkingRadio = nullptr;
    QPushButton *m_startBtn = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};

#endif // MAPNAVIGATIONDIALOG_H
