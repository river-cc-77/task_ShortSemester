#ifndef EVENTPUSHER_H
#define EVENTPUSHER_H

#include <QObject>

/** 定时向在线用户推送 charge.progress 等 event.push 消息。 */
class EventPusher : public QObject
{
    Q_OBJECT

public:
    explicit EventPusher(QObject *parent = nullptr);

private slots:
    void onTick();

private:
    int m_intervalMs = 3000;
};

#endif // EVENTPUSHER_H
