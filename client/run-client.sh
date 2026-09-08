#!/usr/bin/env bash
# 启动用户端，自动匹配系统输入法（ibus / fcitx5）
set -euo pipefail
cd "$(dirname "$0")"

export PNG_SKIP_sRGB_CHECK="${PNG_SKIP_sRGB_CHECK:-1}"

if [[ "${XDG_SESSION_TYPE:-}" == "wayland" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
fi

if [[ -z "${QT_IM_MODULE:-}" ]]; then
  fcitx_qt6="/usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/libfcitx5platforminputcontextplugin.so"
  ibus_qt6="/usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so"
  if pgrep -x fcitx5 >/dev/null 2>&1 || pgrep -x fcitx >/dev/null 2>&1; then
    if [[ -f "$fcitx_qt6" ]]; then
      export QT_IM_MODULE=fcitx
      export GTK_IM_MODULE=fcitx
      export XMODIFIERS=@im=fcitx
      echo "[run-client] fcitx5 + fcitx Qt6 插件"
    elif [[ -f "$ibus_qt6" ]]; then
      export QT_IM_MODULE=ibus
      export GTK_IM_MODULE=ibus
      export XMODIFIERS=@im=ibus
      echo "[run-client] fcitx5 运行中，使用 ibus 兼容层（无需 fcitx5-frontend-qt6）"
    else
      export QT_IM_MODULE=ibus
      export GTK_IM_MODULE=ibus
      export XMODIFIERS=@im=ibus
      echo "[run-client] fcitx5 运行中，但未找到 Qt6 插件，请先: sudo apt install ibus"
    fi
  elif pgrep -x ibus-daemon >/dev/null 2>&1; then
    export QT_IM_MODULE=ibus
    export GTK_IM_MODULE=ibus
    export XMODIFIERS=@im=ibus
    echo "[run-client] 检测到 ibus，已设置 QT_IM_MODULE=ibus"
  else
    export QT_IM_MODULE=ibus
    export GTK_IM_MODULE=ibus
    export XMODIFIERS=@im=ibus
    echo "[run-client] 未检测到 fcitx 进程，默认使用 ibus（Ubuntu 常见）"
  fi
fi

if [[ ! -x ./charge-client ]]; then
  echo "请先编译: qmake6 charge-client.pro && make -j4"
  exit 1
fi

exec ./charge-client "$@"
