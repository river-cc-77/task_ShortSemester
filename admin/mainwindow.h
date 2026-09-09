#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QJsonObject>
#include <QJsonArray>
#include <QMainWindow>
#include <QMap>
class QPushButton;
class QLabel;
class QLineEdit;
class QComboBox;
#include "qcustomplot.h"
#include "pileeditdialog.h"
#include "pilestatusdialog.h"
#include "pileadddialog.h"
#include "orderdetaildialog.h"
#include "stationeditdialog.h"
#include "stationdetaildialog.h"
#include "announcementeditdialog.h"
#include "paginationutil.h"
class ApiClient;
namespace Ui {
class MainWindow;
}
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ApiClient *api, const QJsonObject &admin, QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    // UI自动生成槽函数
    void on_btnRefresh_clicked();
    void on_btnBackHome_clicked();
    void on_btnShift_clicked();
    void on_btnPileQuery_clicked();
    void reloadUserList(const QString& keyword = "");
    void addUserRow(const QJsonObject& userObj);
    void onUserFreezeClick(int userId, bool wantFreeze);
    void reloadOverviewStat();
    void reloadPileList();
    void addPileRow(const QJsonObject &obj);
    void loadStationCombo();
    void onPileRestart(const QString &pileNo);
    void batchPileRestart();
    void batchDeletePile();
    QString getSingleSelectedPileNo() const;
    void goToSelectedPileStatus();
    void onAddPileClicked();

    void reloadOrderList();
    void addOrderRow(const QJsonObject &obj);
    void onOrderDetailClicked(const QJsonObject &order);
    void onOrderAdminSettle(const QString &orderNo);

    void reloadStationList();
    void addStationRow(const QJsonObject &obj);
    void onAddStationClicked();
    void onEditStationClicked(const QJsonObject &station);
    void onDeleteStationClicked(const QJsonObject &station);
    void onStationDetailClicked(const QJsonObject &station);
    void goToStationPiles(int stationId);

    void reloadOperationLogList();
    void addOperationLogRow(const QJsonObject &obj);

    void reloadAnnouncementList();
    void addAnnouncementRow(const QJsonObject &obj);
    void onAddAnnouncementClicked();
    void onEditAnnouncementClicked(const QJsonObject &announcement);
    void onDeleteAnnouncementClicked(const QJsonObject &announcement);

    // 电桩编辑弹窗槽函数
    void onEditPileBtnClicked(const QString& pileNo);

    void setupPagination();
    void renderUserPage();
    void renderPilePage();
    void renderOrderPage();
    void renderStationPage();
    void renderLogPage();
    void renderAnnouncementPage();
    void updatePileStatusOverview(const QJsonObject &pileStat, int pileTotal, double healthRate);

private:
    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    bool m_refreshBusy = false;
    QJsonArray m_stationItems;
    QJsonArray m_userItems;
    QJsonArray m_pileItems;
    QJsonArray m_orderItems;
    QJsonArray m_stationFilteredItems;
    QJsonArray m_logItems;
    QJsonArray m_announceItems;
    QMap<int, int> m_forecastMinIdle;
    ListPager m_userPager;
    ListPager m_pilePager;
    ListPager m_orderPager;
    ListPager m_stationPager;
    ListPager m_logPager;
    ListPager m_announcePager;
    QWidget *m_cardPileStatus = nullptr;
    QLabel *m_labPileStatusDetail = nullptr;
    QLabel *m_labPileHealth = nullptr;
    QLineEdit *m_editLogKeyword = nullptr;
    QComboBox *m_comboLogTargetType = nullptr;
    void resetAllBtnSelect();
    void refreshBtnStyle(QPushButton *btn);
    void drawRevenueChartFromJson(const QJsonArray& trendArr);
    // 根据桩号查找表格行号工具函数
    int getRowByPileNo(const QString& pileNo);
    // =========图表相关=========
    QCustomPlot* m_chart = nullptr;
    int m_currentDays = 7;
    QCPTextElement* m_chartTitle = nullptr;
};
#endif // MAINWINDOW_H
