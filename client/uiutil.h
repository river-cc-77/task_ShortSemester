#ifndef CLIENT_UIUTIL_H
#define CLIENT_UIUTIL_H

#include <QDialog>
#include <QEvent>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QTimer>
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

// 监听宿主窗口尺寸/位置变化，让子窗口实时跟随重排：
// 尺寸按基准（designHeight）随宿主当前几何重新计算，并保持在宿主客户区内居中。
class ChildDialogGeometryFollower : public QObject
{
public:
    ChildDialogGeometryFollower(QDialog *dlg, QWidget *host, int designHeight)
        : QObject(dlg)
        , m_dlg(dlg)
        , m_host(host)
        , m_designHeight(designHeight)
    {
        if (m_host) {
            m_host->installEventFilter(this);
        }
    }

    ~ChildDialogGeometryFollower() override
    {
        if (m_host) {
            m_host->removeEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_host && m_dlg) {
            const QEvent::Type t = event->type();
            if ((t == QEvent::Resize || t == QEvent::Move) && !m_queued) {
                m_queued = true;
                // 等宿主本次几何事件处理完再重排，避免在 resize/move 处理中嵌套改动
                QTimer::singleShot(0, this, [this]() {
                    m_queued = false;
                    if (m_dlg && m_host) {
                        refit();
                    }
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void refit()
    {
        const QSize sz = childDialogSize(m_host, m_designHeight);
        m_dlg->resize(sz);
        const int x = qMax(0, (m_host->width() - sz.width()) / 2);
        const int y = qMax(0, (m_host->height() - sz.height()) / 2);
        m_dlg->move(m_host->mapToGlobal(QPoint(x, y)));
    }

    QDialog *m_dlg = nullptr;
    QWidget *m_host = nullptr;
    int m_designHeight = 0;
    bool m_queued = false;
};

// 子弹窗统一入口：初始几何随父窗口当前尺寸缩放；父窗口后续 resize/move 时自动跟随重排。
// 用法：QDialog dlg(parent); ...; fitChildDialog(&dlg, parent, designHeight);
inline void fitChildDialog(QDialog *dlg, QWidget *host, int designHeight)
{
    if (!dlg || !host) {
        return;
    }
    dlg->resize(childDialogSize(host, designHeight));
    // 同一弹窗只注册一次跟随器（跟随器以弹窗为父对象，随弹窗析构自动卸载）
    if (!dlg->findChild<ChildDialogGeometryFollower *>()) {
        new ChildDialogGeometryFollower(dlg, host, designHeight);
    }
}

#endif // CLIENT_UIUTIL_H
