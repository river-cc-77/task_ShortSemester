#ifndef ANNOUNCEMENTEDITDIALOG_H
#define ANNOUNCEMENTEDITDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QLineEdit;
class QTextEdit;
class QCheckBox;

class AnnouncementEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AnnouncementEditDialog(QWidget *parent = nullptr);

    void setCreateMode();
    void setEditMode(const QJsonObject &announcement);

    QJsonObject getParams() const;

private:
    QLineEdit *m_titleEdit = nullptr;
    QTextEdit *m_contentEdit = nullptr;
    QCheckBox *m_activeCheck = nullptr;
    int m_id = 0;
    bool m_editMode = false;
};

#endif // ANNOUNCEMENTEDITDIALOG_H
