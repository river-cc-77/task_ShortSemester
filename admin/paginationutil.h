#ifndef PAGINATIONUTIL_H
#define PAGINATIONUTIL_H

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QtGlobal>
#include <QWidget>

struct ListPager {
    static constexpr int kPageSize = 20;

    QWidget *bar = nullptr;
    QLabel *label = nullptr;
    QPushButton *prev = nullptr;
    QPushButton *next = nullptr;
    int page = 0;
    int total = 0;

    void attach(QWidget *parent, QVBoxLayout *pageLayout, QWidget *tableWidget)
    {
        bar = new QWidget(parent);
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(0, 4, 0, 0);
        prev = new QPushButton(QStringLiteral("上一页"), bar);
        next = new QPushButton(QStringLiteral("下一页"), bar);
        label = new QLabel(bar);
        label->setAlignment(Qt::AlignCenter);
        prev->setMinimumWidth(88);
        next->setMinimumWidth(88);
        lay->addWidget(prev);
        lay->addWidget(label, 1);
        lay->addWidget(next);
        const int idx = pageLayout->indexOf(tableWidget);
        if (idx >= 0) {
            pageLayout->insertWidget(idx + 1, bar);
        } else {
            pageLayout->addWidget(bar);
        }
    }

    void setTotal(int count)
    {
        total = count;
        const int maxPage = total > 0 ? (total - 1) / kPageSize : 0;
        if (page > maxPage) {
            page = maxPage;
        }
        updateUi();
    }

    void updateUi()
    {
        if (!label) {
            return;
        }
        const int maxPage = total > 0 ? (total - 1) / kPageSize : 0;
        const int from = total == 0 ? 0 : page * kPageSize + 1;
        const int to = qMin(total, (page + 1) * kPageSize);
        label->setText(total == 0
                           ? QStringLiteral("暂无数据")
                           : QStringLiteral("第 %1 / %2 页，显示 %3–%4 / 共 %5 条")
                                 .arg(page + 1)
                                 .arg(maxPage + 1)
                                 .arg(from)
                                 .arg(to)
                                 .arg(total));
        if (prev) {
            prev->setEnabled(page > 0);
        }
        if (next) {
            next->setEnabled(page < maxPage);
        }
    }

    int startIndex() const
    {
        return page * kPageSize;
    }

    int endIndex() const
    {
        return qMin(total, startIndex() + kPageSize);
    }
};

#endif // PAGINATIONUTIL_H
