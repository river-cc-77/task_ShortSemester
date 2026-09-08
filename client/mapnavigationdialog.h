#ifndef MAPNAVIGATIONDIALOG_H
#define MAPNAVIGATIONDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QNetworkAccessManager;

class MapNavigationDialog : public QDialog
{
    Q_OBJECT
public:
    enum class NavMode { Driving, Walking };

    MapNavigationDialog(const QString &originDesc,
                        double originLat, double originLng,
                        const QString &destName, double destLat, double destLng,
                        const QString &baiduAk,
                        const QString &destAddress = QString(),
                        QWidget *parent = nullptr);

private slots:
    void onStartNavigation();
    void onBackToSetup();

private:
    QString directionLiteUrl(NavMode mode) const;
    NavMode selectedMode() const;
    void fetchRoute();
    void showNavigationResult(const QJsonObject &route);
    void loadStaticMap(const QJsonArray &steps);

    QString m_originDesc;
    double m_originLat = 0;
    double m_originLng = 0;
    QString m_destName;
    double m_destLat = 0;
    double m_destLng = 0;
    QString m_baiduAk;
    QString m_region;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_setupPage = nullptr;
    QWidget *m_navPage = nullptr;

    QLabel *m_originLabel = nullptr;
    QLabel *m_destLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_mapLabel = nullptr;
    QRadioButton *m_drivingRadio = nullptr;
    QRadioButton *m_walkingRadio = nullptr;
    QPushButton *m_startBtn = nullptr;
    QListWidget *m_stepsList = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};

#endif // MAPNAVIGATIONDIALOG_H
