#!/usr/bin/env bash
# 管理端启动脚本（slave2 连远程大屏 node100 时使用）
#
# 用法:
#   ./run-admin.sh                          # 默认大屏 http://192.168.176.100:5000/
#   DASHBOARD_URL=http://127.0.0.1:5000/ ./run-admin.sh   # 本机也跑了 Flask 时
#   SERVER_HOST=192.168.x.x ./run-admin.sh  # server 不在本机时

set -euo pipefail
cd "$(dirname "$0")"

export DASHBOARD_URL="${DASHBOARD_URL:-http://192.168.176.100:5000/}"

if [[ ! -x ./charge-admin ]]; then
  echo "未找到 charge-admin，正在编译..."
  qmake6 charge-admin.pro && make -j4
fi

echo "DASHBOARD_URL=$DASHBOARD_URL"
exec ./charge-admin
