#!/usr/bin/env bash
# 启动用户端，自动匹配系统输入法（ibus / fcitx5）
set -euo pipefail
cd "$(dirname "$0")"

if [[ -z "${QT_IM_MODULE:-}" ]]; then
  if pgrep -x fcitx5 >/dev/null 2>&1 || pgrep -x fcitx >/dev/null 2>&1; then
    export QT_IM_MODULE=fcitx
    export GTK_IM_MODULE=fcitx
    export XMODIFIERS=@im=fcitx
    echo "[run-client] 检测到 fcitx，已设置 QT_IM_MODULE=fcitx"
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
