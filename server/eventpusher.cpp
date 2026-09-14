#include "eventpusher.h"

#include "handlers/orderhandler.h"
#include "sessionregistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

EventPusher::EventPusher(QObject *parent)
    : QObject(parent)
{
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &EventPusher::onTick);
    timer->start(m_intervalMs);
}

void EventPusher::onTick()
{
    const QJsonArray orders = OrderHandler::activeChargingOrders();
    for (const QJsonValue &value : orders) {
        const QJsonObject order = value.toObject();
        const int userId = order.value(QStringLiteral("user_id")).toInt();
        const QJsonObject payload = OrderHandler::buildProgressPayload(order);
        if (payload.isEmpty()) {
            continue;
        }

        QJsonObject eventData;
        eventData[QStringLiteral("type")] = QStringLiteral("charge.progress");
        eventData[QStringLiteral("payload")] = payload;
        SessionRegistry::instance().pushToUser(userId, eventData);
    }
}
