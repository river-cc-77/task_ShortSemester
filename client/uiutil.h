#ifndef CLIENT_UIUTIL_H
#define CLIENT_UIUTIL_H

#include <QDialog>
#include <QPoint>
#include <QSize>
#include <QWidget>

// 子弹窗随父窗口（主窗或上级弹窗）按宽、高分别放大，以 390×844 手机竖屏为基准。
inline QSize childDialogSize(const QWidget *parent, int designHeight)
{
    if (!parent) {
        return QSize(372, designHeight);
    }
    constexpr int kDesignWidth = 372;
    constexpr int kRefWinW = 390;
    constexpr int kRefWinH = 844;
    int w = qRound(kDesignWidth * (parent->width() / qreal(kRefWinW)));
    int h = qRound(designHeight * (parent->height() / qreal(kRefWinH)));
    w = qBound(280, w, parent->width() - 20);
    h = qBound(180, h, parent->height() - 40);
    return QSize(w, h);
}

// 将弹窗限制在父窗口可视区域内并居中（模拟手机 App 内页）
inline void fitDialogInParent(QDialog *dlg, const QWidget *parent, int designHeight)
{
    if (!dlg || !parent) {
        return;
    }
    const QSize sz = childDialogSize(parent, designHeight);
    dlg->resize(sz);
    const int x = qMax(0, (parent->width() - sz.width()) / 2);
    const int y = qMax(0, (parent->height() - sz.height()) / 2);
    dlg->move(parent->mapToGlobal(QPoint(x, y)));
}

#endif // CLIENT_UIUTIL_H
