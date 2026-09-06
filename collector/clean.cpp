#include "clean.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QtMath>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {

// 四舍五入保留两位小数
double round2(double v)
{
    return qRound(v * 100.0) / 100.0;
}

// 解析 "yyyy-MM-dd HH:mm:ss"；空串/非法返回 invalid
QDateTime parseTs(const QString &s)
{
    if (s.trimmed().isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(s.trimmed(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// 取字符串第 12 位起两位的"小时"（时钟格式 yyyy-MM-dd HH:mm:ss，index 11 起）
int hourOfTs(const QString &ts)
{
    if (ts.size() < 13) {
        return 0;
    }
    const int h = ts.mid(11, 2).toInt();
    return (h >= 0 && h <= 23) ? h : 0;
}

} // namespace

Cleaner::Cleaner(QSqlDatabase &db)
    : m_db(db)
{
}

QString Cleaner::regionFromAddress(const QString &address)
{
    if (address.isEmpty()) {
        return QStringLiteral("未知");
    }
    // "深圳市福田区福中三路…" -> "福田区"：取第一个'市'后、第一个'区'(含)之前的片段
    const int cityIdx = address.indexOf(QStringLiteral("市"));
    const int start = (cityIdx >= 0) ? cityIdx + 1 : 0;
    const int quIdx = address.indexOf(QStringLiteral("区"), start);
    if (quIdx < 0) {
        return QStringLiteral("未知");
    }
    return address.mid(start, quIdx - start + 1);
}

bool Cleaner::rebuildFactLedger(const QDate &from, const QDate &to)
{
    const QString fromStr = from.toString(Qt::ISODate);
    const QString toStr = to.toString(Qt::ISODate);

    // 幂等：先删窗口内旧行（按归属 stat_date），再全量重洗窗口订单
    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM ads_order_fact WHERE stat_date BETWEEN :from AND :to"));
    remove.bindValue(":from", fromStr);
    remove.bindValue(":to", toStr);
    if (!remove.exec()) {
        qCritical() << "ads_order_fact delete failed:" << remove.lastError().text();
        return false;
    }

    // 候选订单：LEFT JOIN 取 电价/功率/地址/引用 用于校验与补全。
    // 演示规模小，整表扫描即可；行归属日在窗口外的一律跳过（其旧 fact 行保持不变）。
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT o.id, o.order_no, o.user_id, o.station_id, o.pile_id, o.status, "
            "       o.start_at, o.end_at, o.created_at, o.kwh, o.amount, "
            "       u.id, s.id, s.price, p.id, p.power_kw, s.address "
            "FROM charge_order o "
            "LEFT JOIN user    u ON u.id = o.user_id "
            "LEFT JOIN station s ON s.id = o.station_id "
            "LEFT JOIN pile    p ON p.id = o.pile_id "
            "ORDER BY o.id ASC"))) {
        qCritical() << "fact candidate query failed:" << query.lastError().text();
        return false;
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO ads_order_fact "
        "(order_id, order_no, user_id, station_id, pile_id, status, "
        " stat_date, stat_hour, start_ts, end_ts, duration_min, "
        " kwh_orig, amount_orig, kwh_eff, amount_eff, est_source, "
        " region, excluded, exclude_code, warn_code, ts_flag, ts_missing, raw_created_at) "
        "VALUES (:order_id, :order_no, :user_id, :station_id, :pile_id, :status, "
        " :stat_date, :stat_hour, :start_ts, :end_ts, :duration_min, "
        " :kwh_orig, :amount_orig, :kwh_eff, :amount_eff, :est_source, "
        " :region, :excluded, :exclude_code, :warn_code, :ts_flag, :ts_missing, :raw_created_at)"));

    const QString now12 = QDateTime::currentDateTime().addSecs(-12 * 3600)
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QDateTime nowPlus10 = QDateTime::currentDateTime().addSecs(10 * 60);

    QSet<QString> seenOrderNo;                       // 精确去重：order_no 首次(min id)为规范
    QHash<QString, bool> nearDupKeySeen;             // 疑似重复：(user,pile,start分钟)

    while (query.next()) {
        const qlonglong orderId = query.value(0).toLongLong();
        const QString orderNo = query.value(1).toString();
        const qlonglong userId = query.value(2).toLongLong();
        const qlonglong stationId = query.value(3).toLongLong();
        const qlonglong pileId = query.value(4).toLongLong();
        const QString status = query.value(5).toString();
        const QString startAt = query.value(6).toString();
        const QString endAt = query.value(7).toString();
        const QString createdAt = query.value(8).toString();
        const double kwhRaw = query.value(9).toDouble();
        const double amountRaw = query.value(10).toDouble();
        const bool userMissing = query.value(11).isNull();
        const bool stationMissing = query.value(12).isNull();
        const double price = query.value(13).isNull() ? 0.0 : query.value(13).toDouble();
        const bool pileMissing = query.value(14).isNull();
        const double power = query.value(15).isNull() ? 0.0 : query.value(15).toDouble();
        const QString address = query.value(16).isNull() ? QString() : query.value(16).toString();

        const bool hasStart = !startAt.trimmed().isEmpty();
        const bool hasEnd = !endAt.trimmed().isEmpty();
        const QDateTime startDT = parseTs(startAt);
        const QDateTime endDT = parseTs(endAt);
        const QDateTime createdDT = parseTs(createdAt);

        // 归属时钟：start_at -> end_at -> created_at（回退置 ts_flag）
        QString clock;
        bool tsFlag = false;
        if (hasStart) {
            clock = startAt;
        } else if (hasEnd) {
            clock = endAt;
            tsFlag = true;
        } else {
            clock = createdAt;
            tsFlag = true;
        }
        const QString statDate = clock.left(10);
        const int statHour = hourOfTs(clock);

        // 只在归属日位于回填窗口内的订单参与本次重建（旧窗口行不动）。
        // 窗外订单虽不落 fact，也必须先登记去重线索：否则当重复对里"更早单在窗外、
        // 副本在窗内"时，窗内副本会被误当正常单保留（窗口边界去重失效）。
        if (statDate < fromStr || statDate > toStr) {
            seenOrderNo.insert(orderNo);
            // 疑似重复线索只登记时间有效（非倒挂）的窗外单，与窗内判定口径一致
            const bool winTimeInvalid = hasStart && hasEnd && startDT.isValid()
                                        && endDT.isValid() && endDT < startDT;
            if (hasStart && !winTimeInvalid) {
                nearDupKeySeen.insert(QString::number(userId) + QLatin1Char(':')
                                          + QString::number(pileId) + QLatin1Char(':')
                                          + startAt.left(16),
                                      true);
            }
            continue;
        }

        const bool isTerminal = (status == QStringLiteral("已完成")
                                 || status == QStringLiteral("待支付"));
        const bool tsMissing = isTerminal && !(hasStart && hasEnd);
        const bool timeInvalid = hasStart && hasEnd && startDT.isValid() && endDT.isValid()
                                 && endDT < startDT;
        double durationMin = 0.0;
        if (hasStart && hasEnd && startDT.isValid() && endDT.isValid() && endDT >= startDT) {
            durationMin = round2(startDT.msecsTo(endDT) / 60000.0);
        }

        const bool negative = (kwhRaw < 0.0 || amountRaw < 0.0);
        const bool fkMissing = userMissing || stationMissing || pileMissing;

        QDateTime clockDT = parseTs(clock);
        bool future = clockDT.isValid() && clockDT > nowPlus10;

        // 滞留单：预约超 12h 未启动 / 充电中超 12h 未结束（异常会话，占桩未释放）
        bool staleReserve = false;
        bool staleCharging = false;
        if (status == QStringLiteral("预约") && createdDT.isValid()) {
            staleReserve = (createdAt < now12);
        } else if (status == QStringLiteral("充电中")) {
            const QDateTime ref = (startDT.isValid()) ? startDT : createdDT;
            staleCharging = ref.isValid() && ref.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) < now12;
        }

        // 有效值（防御性去负）
        const double kwhPos = kwhRaw > 0.0 ? kwhRaw : 0.0;
        const double amountPos = amountRaw > 0.0 ? amountRaw : 0.0;
        double kwhEff = kwhPos;
        double amountEff = amountPos;
        QStringList estParts;

        // 字段补全（仅对终态单：已完成/待支付）。优先级：金额缺补电价 / 电量缺按金额反推 /
        // 双缺按时长×功率估（再由电价补金额）。
        if (isTerminal) {
            if (amountEff <= 0.0 && kwhEff > 0.0 && price > 0.0) {
                amountEff = round2(kwhEff * price);
                if (!estParts.contains(QStringLiteral("price"))) estParts << QStringLiteral("price");
            } else if (kwhEff <= 0.0 && amountEff > 0.0 && price > 0.0) {
                kwhEff = round2(amountEff / price);
                if (!estParts.contains(QStringLiteral("amount"))) estParts << QStringLiteral("amount");
            } else if (kwhEff <= 0.0 && amountEff <= 0.0 && durationMin > 0.0 && power > 0.0) {
                kwhEff = round2(power * durationMin / 60.0);
                if (!estParts.contains(QStringLiteral("power"))) estParts << QStringLiteral("power");
                if (price > 0.0) {
                    amountEff = round2(kwhEff * price);
                    if (!estParts.contains(QStringLiteral("price"))) estParts << QStringLiteral("price");
                }
            }
        }

        // 已完成单电量/金额皆 0 且无估算依据 -> 剔除
        const bool zeroUnest = (status == QStringLiteral("已完成") && kwhPos <= 0.0
                                && amountPos <= 0.0 && !(durationMin > 0.0 && power > 0.0));

        // 精确去重：同 order_no 已出现过（保留更早/更小 id）
        const bool dup = seenOrderNo.contains(orderNo);
        seenOrderNo.insert(orderNo);

        // 剔除原因优先级（单码）
        QString excludeCode;
        if (negative) {
            excludeCode = QStringLiteral("negative");
        } else if (fkMissing) {
            excludeCode = QStringLiteral("fk_missing");
        } else if (dup) {
            excludeCode = QStringLiteral("dup");
        } else if (timeInvalid) {
            excludeCode = QStringLiteral("time_invalid");
        } else if (future) {
            excludeCode = QStringLiteral("future_ts");
        } else if (zeroUnest) {
            excludeCode = QStringLiteral("zero_unest");
        } else if (staleReserve) {
            excludeCode = QStringLiteral("stale_reserve");
        } else if (staleCharging) {
            excludeCode = QStringLiteral("stale_charging");
        }
        const bool excluded = !excludeCode.isEmpty();

        // 仅标记仍入指标的告警
        QStringList warns;
        if (!excluded) {
            if (!estParts.isEmpty()) {
                warns << QStringLiteral("est");
            }
            // 价差可疑：原始电量与金额同时存在且与电价偏差>5%（估算来源的除外）
            if (status == QStringLiteral("已完成") && estParts.isEmpty() && amountEff > 0.0
                && kwhEff > 0.0 && price > 0.0
                && qAbs(amountEff - kwhEff * price) / amountEff > 0.05) {
                warns << QStringLiteral("price_suspect");
            }
            // 疑似重复：同 (user,pile,start 到分钟) 且非首单
            if (hasStart && !timeInvalid) {
                const QString key = QString::number(userId) + QLatin1Char(':')
                                    + QString::number(pileId) + QLatin1Char(':')
                                    + startAt.left(16);
                if (nearDupKeySeen.contains(key)) {
                    warns << QStringLiteral("near_dup");
                } else {
                    nearDupKeySeen.insert(key, true);
                }
            }
        }

        insert.bindValue(":order_id", orderId);
        insert.bindValue(":order_no", orderNo);
        insert.bindValue(":user_id", userId);
        insert.bindValue(":station_id", stationId);
        insert.bindValue(":pile_id", pileId);
        insert.bindValue(":status", status);
        insert.bindValue(":stat_date", statDate);
        insert.bindValue(":stat_hour", statHour);
        insert.bindValue(":start_ts", hasStart ? QVariant(startAt) : QVariant());
        insert.bindValue(":end_ts", hasEnd ? QVariant(endAt) : QVariant());
        insert.bindValue(":duration_min", durationMin);
        insert.bindValue(":kwh_orig", round2(kwhRaw));
        insert.bindValue(":amount_orig", round2(amountRaw));
        insert.bindValue(":kwh_eff", round2(kwhEff));
        insert.bindValue(":amount_eff", round2(amountEff));
        // 空列表 join() 得到的是 null QString，Qt 会绑成 SQL NULL；而这两列是
        // NOT NULL DEFAULT ''，必须显式绑空串而非 NULL。
        insert.bindValue(":est_source",
                         estParts.isEmpty() ? QStringLiteral("") : estParts.join(QLatin1Char(',')));
        insert.bindValue(":region", regionFromAddress(address));
        insert.bindValue(":excluded", excluded ? 1 : 0);
        insert.bindValue(":exclude_code", excludeCode.isEmpty() ? QVariant() : QVariant(excludeCode));
        insert.bindValue(":warn_code",
                         warns.isEmpty() ? QStringLiteral("") : warns.join(QLatin1Char(',')));
        insert.bindValue(":ts_flag", tsFlag ? 1 : 0);
        insert.bindValue(":ts_missing", tsMissing ? 1 : 0);
        insert.bindValue(":raw_created_at", createdAt);

        if (!insert.exec()) {
            qCritical() << "ads_order_fact insert failed for" << orderNo << ":" << insert.lastError().text();
            return false;
        }
    }

    qInfo() << "Cleaner: rebuilt order fact ledger" << fromStr << "~" << toStr;
    return true;
}

bool Cleaner::rebuildIssueLedger()
{
    QSqlQuery remove(m_db);
    if (!remove.exec(QStringLiteral("DELETE FROM ads_order_issue"))) {
        qCritical() << "ads_order_issue delete failed:" << remove.lastError().text();
        return false;
    }

    QSqlQuery insert(m_db);
    const QString sql = QStringLiteral(
        "INSERT INTO ads_order_issue (order_no, issue_code, issue_level, detail) "
        // 剔除原因
        "SELECT order_no, exclude_code, 'exclude', "
        "  CASE exclude_code "
        "    WHEN 'dup'            THEN 'order_no 重复，保留更早订单' "
        "    WHEN 'fk_missing'     THEN 'user/station/pile 引用缺失' "
        "    WHEN 'negative'       THEN '存在负值电量/金额' "
        "    WHEN 'time_invalid'   THEN 'end_at 早于 start_at（时间倒挂）' "
        "    WHEN 'future_ts'      THEN '充电时间在未来' "
        "    WHEN 'zero_unest'     THEN '已完成单 电量/金额皆 0 且缺估算依据' "
        "    WHEN 'stale_reserve'  THEN '预约超 12h 未启动（僵尸预约）' "
        "    WHEN 'stale_charging' THEN '充电中超 12h 未结束（异常会话）' "
        "    ELSE exclude_code END "
        "FROM ads_order_fact WHERE exclude_code IS NOT NULL AND exclude_code <> '' "
        "UNION ALL "
        // 估算补全
        "SELECT order_no, 'est', 'warn', "
        "  '电量/金额按(' || est_source || ')估算补全: orig kwh=' || printf('%.2f', kwh_orig) "
        "    || ', amount=' || printf('%.2f', amount_orig) "
        "    || ' -> eff kwh=' || printf('%.2f', kwh_eff) "
        "    || ', amount=' || printf('%.2f', amount_eff) "
        "FROM ads_order_fact WHERE est_source <> '' "
        "UNION ALL "
        // 疑似重复
        "SELECT order_no, 'near_dup', 'warn', "
        "  '疑似与同用户/同桩/同分钟开始时间的另一笔订单重复' "
        "FROM ads_order_fact WHERE warn_code LIKE '%near_dup%' "
        "UNION ALL "
        // 价差可疑（需联电站取电价）
        "SELECT f.order_no, 'price_suspect', 'warn', "
        "  printf('金额 %.2f 与 电量×电价 %.2f 偏差>5%%', f.amount_eff, f.kwh_eff * s.price) "
        "FROM ads_order_fact f JOIN station s ON s.id = f.station_id "
        "WHERE f.warn_code LIKE '%price_suspect%'");
    if (!insert.exec(sql)) {
        qCritical() << "ads_order_issue rebuild failed:" << insert.lastError().text();
        return false;
    }
    return true;
}
