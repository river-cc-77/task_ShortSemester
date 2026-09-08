#include "announcementmanagedialog.h"
#include "announcementeditdialog.h"
#include "apiclient.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

AnnouncementManageDialog::AnnouncementManageDialog(ApiClient *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("公告管理"));
    resize(760, 480);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("标题"),
        QStringLiteral("状态"),
        QStringLiteral("发布时间"),
        QStringLiteral("操作"),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(4, 180);

    auto *addBtn = new QPushButton(QStringLiteral("新增公告"), this);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    addBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setCursor(Qt::PointingHandCursor);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(addBtn);
    topRow->addWidget(refreshBtn);
    topRow->addStretch();
    topRow->addWidget(closeBtn);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_table);

    connect(addBtn, &QPushButton::clicked, this, &AnnouncementManageDialog::onAdd);
    connect(refreshBtn, &QPushButton::clicked, this, &AnnouncementManageDialog::reloadList);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    reloadList();
}

void AnnouncementManageDialog::reloadList()
{
    m_table->setRowCount(0);
    const QJsonObject resp = m_api->call(QStringLiteral("announcement.admin.list"));
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             resp.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("message")).toString());
        return;
    }

    const QJsonArray items = resp.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        m_table->setItem(row, 0, new QTableWidgetItem(QString::number(obj.value("id").toInt())));
        m_table->setItem(row, 1, new QTableWidgetItem(obj.value("title").toString()));
        const bool active = obj.value("is_active").toInt() == 1;
        m_table->setItem(row, 2, new QTableWidgetItem(active ? QStringLiteral("启用")
                                                             : QStringLiteral("停用")));
        m_table->setItem(row, 3, new QTableWidgetItem(obj.value("created_at").toString()));

        auto *editBtn = new QPushButton(QStringLiteral("编辑"));
        auto *delBtn = new QPushButton(QStringLiteral("删除"));
        editBtn->setProperty("annId", obj.value("id").toInt());
        delBtn->setProperty("annId", obj.value("id").toInt());
        delBtn->setProperty("annTitle", obj.value("title").toString());

        auto *cell = new QWidget;
        auto *lay = new QHBoxLayout(cell);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->addWidget(editBtn);
        lay->addWidget(delBtn);
        m_table->setCellWidget(row, 4, cell);

        connect(editBtn, &QPushButton::clicked, this, [this, obj]() {
            AnnouncementEditDialog dlg(this);
            dlg.setEditMode(obj);
            if (dlg.exec() != QDialog::Accepted) {
                return;
            }
            const QJsonObject params = dlg.getParams();
            const QJsonObject resp = m_api->call(QStringLiteral("announcement.update"), params);
            if (!resp.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this, QStringLiteral("失败"),
                                     resp.value(QStringLiteral("error")).toObject()
                                         .value(QStringLiteral("message")).toString());
                return;
            }
            reloadList();
        });
        connect(delBtn, &QPushButton::clicked, this, [this, obj]() {
            const int annId = obj.value("id").toInt();
            const QString title = obj.value("title").toString();
            if (QMessageBox::question(this, QStringLiteral("确认删除"),
                                      QStringLiteral("确定删除公告「%1」？").arg(title))
                != QMessageBox::Yes) {
                return;
            }
            QJsonObject params;
            params[QStringLiteral("id")] = annId;
            const QJsonObject resp = m_api->call(QStringLiteral("announcement.delete"), params);
            if (!resp.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this, QStringLiteral("失败"),
                                     resp.value(QStringLiteral("error")).toObject()
                                         .value(QStringLiteral("message")).toString());
                return;
            }
            reloadList();
        });
    }
}

void AnnouncementManageDialog::onAdd()
{
    AnnouncementEditDialog dlg(this);
    dlg.setCreateMode();
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QJsonObject params = dlg.getParams();
    const QJsonObject resp = m_api->call(QStringLiteral("announcement.create"), params);
    if (!resp.value(QStringLiteral("ok")).toBool()) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             resp.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("message")).toString());
        return;
    }
    reloadList();
}

void AnnouncementManageDialog::onEdit()
{
}

void AnnouncementManageDialog::onDelete()
{
}
