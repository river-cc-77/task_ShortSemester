#include "announcementeditdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

AnnouncementEditDialog::AnnouncementEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("公告编辑"));
    resize(480, 360);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(QStringLiteral("公告标题"));

    m_contentEdit = new QTextEdit(this);
    m_contentEdit->setPlaceholderText(QStringLiteral("公告正文"));

    m_activeCheck = new QCheckBox(QStringLiteral("对用户端展示（启用）"), this);
    m_activeCheck->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("标题"), m_titleEdit);
    form->addRow(QStringLiteral("内容"), m_contentEdit);
    form->addRow(QString(), m_activeCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void AnnouncementEditDialog::setCreateMode()
{
    m_editMode = false;
    m_id = 0;
    setWindowTitle(QStringLiteral("新增公告"));
    m_titleEdit->clear();
    m_contentEdit->clear();
    m_activeCheck->setChecked(true);
}

void AnnouncementEditDialog::setEditMode(const QJsonObject &announcement)
{
    m_editMode = true;
    m_id = announcement.value(QStringLiteral("id")).toInt();
    setWindowTitle(QStringLiteral("编辑公告"));
    m_titleEdit->setText(announcement.value(QStringLiteral("title")).toString());
    m_contentEdit->setPlainText(announcement.value(QStringLiteral("content")).toString());
    m_activeCheck->setChecked(announcement.value(QStringLiteral("is_active")).toInt() == 1);
}

QJsonObject AnnouncementEditDialog::getParams() const
{
    QJsonObject params;
    if (m_editMode) {
        params[QStringLiteral("id")] = m_id;
    }
    params[QStringLiteral("title")] = m_titleEdit->text().trimmed();
    params[QStringLiteral("content")] = m_contentEdit->toPlainText().trimmed();
    params[QStringLiteral("is_active")] = m_activeCheck->isChecked();
    return params;
}
