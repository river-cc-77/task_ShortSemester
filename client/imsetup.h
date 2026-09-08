#ifndef IMSETUP_H
#define IMSETUP_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace {

inline bool fileExists(const char *path)
{
    return QFileInfo::exists(QString::fromUtf8(path));
}

inline bool processCommMatches(const char *needle)
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList ids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : ids) {
        bool ok = false;
        id.toInt(&ok);
        if (!ok)
            continue;
        QFile comm(QStringLiteral("/proc/%1/comm").arg(id));
        if (!comm.open(QIODevice::ReadOnly))
            continue;
        const QByteArray name = comm.readAll().trimmed();
        if (name == needle)
            return true;
    }
    return false;
}

inline bool fcitxQtPluginInstalled()
{
    static const char *paths[] = {
        "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/"
        "libfcitx5platforminputcontextplugin.so",
        "/usr/lib/aarch64-linux-gnu/qt6/plugins/platforminputcontexts/"
        "libfcitx5platforminputcontextplugin.so",
        "/usr/lib/qt6/plugins/platforminputcontexts/libfcitx5platforminputcontextplugin.so",
        nullptr,
    };
    for (int i = 0; paths[i] != nullptr; ++i) {
        if (fileExists(paths[i]))
            return true;
    }
    return false;
}

inline bool ibusQtPluginInstalled()
{
    static const char *paths[] = {
        "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/"
        "libibusplatforminputcontextplugin.so",
        "/usr/lib/aarch64-linux-gnu/qt6/plugins/platforminputcontexts/"
        "libibusplatforminputcontextplugin.so",
        "/usr/lib/qt6/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so",
        nullptr,
    };
    for (int i = 0; paths[i] != nullptr; ++i) {
        if (fileExists(paths[i]))
            return true;
    }
    return false;
}

inline bool isWaylandSession()
{
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return true;
    return qgetenv("XDG_SESSION_TYPE").toLower() == "wayland";
}

inline void ensureEnv(const char *key, const char *value)
{
    if (qEnvironmentVariableIsEmpty(key))
        qputenv(key, value);
}

inline bool qtImModuleUsable(const QByteArray &mod)
{
    if (mod.isEmpty())
        return false;
    if (mod == "fcitx" || mod == "fcitx5")
        return fcitxQtPluginInstalled();
    if (mod == "ibus")
        return ibusQtPluginInstalled();
    return false;
}

inline void applyImModule(const QByteArray &module)
{
    qputenv("QT_IM_MODULE", module);
    if (module == "fcitx") {
        ensureEnv("XMODIFIERS", "@im=fcitx");
        ensureEnv("GTK_IM_MODULE", "fcitx");
    } else if (module == "ibus") {
        ensureEnv("XMODIFIERS", "@im=ibus");
        ensureEnv("GTK_IM_MODULE", "ibus");
    }
}

} // namespace

/**
 * 在创建 QApplication 之前调用。
 * Linux 下 Qt 不会自动继承桌面输入法；Wayland 会话还需强制 xcb 才稳定。
 */
inline void setupInputMethodEnv()
{
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("PNG_SKIP_sRGB_CHECK"))
        qputenv("PNG_SKIP_sRGB_CHECK", "1");

    if (isWaylandSession() && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    const QByteArray preset = qgetenv("QT_IM_MODULE");
    if (qtImModuleUsable(preset)) {
        applyImModule(preset == "fcitx5" ? QByteArray("fcitx") : preset);
        return;
    }

    const bool fcitxRunning = processCommMatches("fcitx5") || processCommMatches("fcitx");
    const bool ibusRunning = processCommMatches("ibus-daemon");

    if (fcitxRunning && fcitxQtPluginInstalled()) {
        applyImModule("fcitx");
        return;
    }
    // fcitx5 在跑但没有 fcitx5-frontend-qt6 时，Qt6 可走 ibus 兼容协议（Ubuntu 20.04 常见）
    if (fcitxRunning && ibusQtPluginInstalled()) {
        applyImModule("ibus");
        return;
    }
    if (ibusRunning && ibusQtPluginInstalled()) {
        applyImModule("ibus");
        return;
    }

    const QByteArray xmod = qgetenv("XMODIFIERS");
    if (xmod.contains("fcitx") && fcitxQtPluginInstalled()) {
        applyImModule("fcitx");
        return;
    }
    if (xmod.contains("ibus") && ibusQtPluginInstalled()) {
        applyImModule("ibus");
        return;
    }

    if (fcitxQtPluginInstalled()) {
        applyImModule("fcitx");
        return;
    }
    if (ibusQtPluginInstalled()) {
        applyImModule("ibus");
        return;
    }

    applyImModule("ibus");

    if (qEnvironmentVariableIsEmpty("QT_IM_MODULES"))
        qputenv("QT_IM_MODULES", "fcitx;ibus");
#endif
}

#if defined(Q_OS_LINUX)
#include <QDebug>

inline void logInputMethodStatus()
{
    const bool fcitxRunning = processCommMatches("fcitx5") || processCommMatches("fcitx");
    const bool ibusRunning = processCommMatches("ibus-daemon");
    const bool fcitxPlugin = fcitxQtPluginInstalled();
    const bool ibusPlugin = ibusQtPluginInstalled();
    const QByteArray qtIm = qgetenv("QT_IM_MODULE");

    qInfo().noquote() << "[IME]"
                      << "QT_IM_MODULE=" << qtIm
                      << "XMODIFIERS=" << qgetenv("XMODIFIERS")
                      << "QT_QPA_PLATFORM=" << qgetenv("QT_QPA_PLATFORM")
                      << "fcitx5=" << (fcitxRunning ? "yes" : "no")
                      << "ibus-daemon=" << (ibusRunning ? "yes" : "no");

    if (!fcitxPlugin && !ibusPlugin) {
        qWarning().noquote() << "[IME] 未找到 Qt6 输入法插件。"
                                  "ibus: sudo apt install ibus ibus-pinyin"
                                  " | fcitx5: sudo apt install fcitx5-frontend-qt6";
        return;
    }

    if (fcitxRunning && ibusPlugin && qtIm == "ibus") {
        qInfo().noquote() << "[IME] fcitx5 已通过 ibus 兼容层接入（无需 fcitx5-frontend-qt6）。";
    }

    qInfo().noquote() << "[IME] 请先点击输入框，再按 Ctrl+Space 或 Super+Space 切换拼音。";
}
#else
inline void logInputMethodStatus() {}
#endif

#endif // IMSETUP_H
