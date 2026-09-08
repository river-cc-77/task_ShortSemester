#ifndef ANNOUNCEMENTMANAGEDIALOG_H
#define ANNOUNCEMENTMANAGEDIALOG_H

#include <QDialog>

class ApiClient;
class QTableWidget;

class AnnouncementManageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AnnouncementManageDialog(ApiClient *api, QWidget *parent = nullptr);

private slots:
    void reloadList();
    void onAdd();
    void onEdit();
    void onDelete();

private:
    ApiClient *m_api = nullptr;
    QTableWidget *m_table = nullptr;
};

#endif // ANNOUNCEMENTMANAGEDIALOG_H
