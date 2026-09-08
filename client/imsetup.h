#ifndef IMSETUP_H
#define IMSETUP_H

#include <QByteArray>
#include <QFileInfo>

/**
 * 在创建 QApplication 之前调用。
 * Linux 下 Qt 不会自动继承桌面输入法，必须设置 QT_IM_MODULE。
 * 桌面/浏览器能用拼音、但 Qt 程序不能，几乎都是此项未配置或配错（ibus vs fcitx）。
 */
inline void setupInputMethodEnv()
{
#if defined(Q_OS_LINUX)
    if (!qEnvironmentVariableIsEmpty("QT_IM_MODULE")) {
        return;
    }

    const QByteArray xmod = qgetenv("XMODIFIERS");
    if (xmod.contains("fcitx")) {
        qputenv("QT_IM_MODULE", "fcitx");
        return;
    }
    if (xmod.contains("ibus")) {
        qputenv("QT_IM_MODULE", "ibus");
        return;
    }

    static const char *fcitxPlugins[] = {
        "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/"
        "libfcitx5platforminputcontextplugin.so",
        "/usr/lib/qt6/plugins/platforminputcontexts/libfcitx5platforminputcontextplugin.so",
        nullptr,
    };
    for (int i = 0; fcitxPlugins[i] != nullptr; ++i) {
        if (QFileInfo::exists(QString::fromUtf8(fcitxPlugins[i]))) {
            qputenv("QT_IM_MODULE", "fcitx");
            if (qEnvironmentVariableIsEmpty("XMODIFIERS")) {
                qputenv("XMODIFIERS", "@im=fcitx");
            }
            if (qEnvironmentVariableIsEmpty("GTK_IM_MODULE")) {
                qputenv("GTK_IM_MODULE", "fcitx");
            }
            return;
        }
    }

    // Ubuntu 等默认 ibus；与桌面/浏览器行为一致
    qputenv("QT_IM_MODULE", "ibus");
    if (qEnvironmentVariableIsEmpty("XMODIFIERS")) {
        qputenv("XMODIFIERS", "@im=ibus");
    }
    if (qEnvironmentVariableIsEmpty("GTK_IM_MODULE")) {
        qputenv("GTK_IM_MODULE", "ibus");
    }
#endif
}

#endif // IMSETUP_H
