# charge-client（用户端）

组员 A 在此目录继续开发找桩、充电、我的等页面。

## 当前功能

- 登录、附近充电站、电站详情、预约/充电/结算
- **一键导航**：应用内 QWebEngine 交互地图 + 分步指引（不跳转浏览器）
- **公告**：顶部「公告」按钮查看系统公告
- 地理编码、收藏、订单历史、个人中心

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev qt6-webengine-dev \
  libqt6webenginecore6 libqt6webenginewidgets6 libqt6webenginecore6-bin

cd client
qmake6 charge-client.pro
make -j4
```

导航优先使用 **QWebEngineView + 百度 JS 地图** 展示可缩放、可拖动的路线；未安装 WebEngine 时回退为静态图 + 文字指引。

确认运行时组件：

```bash
ls /usr/lib/x86_64-linux-gnu/qt6/libexec/QtWebEngineProcess
```

## 运行

1. 启动 server（见 `../server/README.md`）
2. `./charge-client`

若曾编译过带 WebEngine 的版本并出现 `Could not find QtWebEngineProcess`，请先清理再按默认方式编译：

```bash
make clean
qmake6 charge-client.pro && make -j4
./charge-client
```

Wayland 提示可忽略。2026-03 起程序在 Wayland 下会自动使用 `xcb`（X11），输入法更稳定。

启动后终端会打印一行 `[IME] ... plugin=ibus`（或 `fcitx`）。若显示 `compose` 或警告，见下方输入法章节。

## Linux 虚拟机中文输入法（地址栏无法切拼音）

**现象：** 桌面、浏览器可以切拼音，但打开 `charge-client` 后不能；回到桌面切好再点回 client 仍无效。

**原因：** Qt 程序不会自动接系统输入法，必须设置 `QT_IM_MODULE=ibus` 或 `fcitx`，且须在启动进程前生效。  
（2026-03 起 `main.cpp` 会在启动时自动检测；仍建议用下面方式验证。）

### 第一步：确认你用的是 ibus 还是 fcitx

在终端执行：

```bash
pgrep -a ibus-daemon
pgrep -a fcitx5
echo "XMODIFIERS=$XMODIFIERS"
im-config -l
```

- 有 `ibus-daemon` → 用 **ibus**（Ubuntu 默认，与浏览器一致）
- 有 `fcitx5` → 用 **fcitx**

### 第二步：按框架安装 Qt6 插件

**先判断：Ubuntu 桌面默认是 ibus，只有 `pgrep fcitx5` 有输出时才走 fcitx 分支。**  
若 `apt` 报「无法定位软件包 fcitx5-frontend-qt6」，说明当前系统源里没有 fcitx 的 Qt6 插件（常见于 Ubuntu 20.04 或未启用 universe），**请直接用下面的 ibus 方案**，与浏览器一致。

**ibus（推荐，Ubuntu 默认）：**

```bash
sudo apt update
sudo apt install -y ibus ibus-pinyin ibus-gtk ibus-gtk3
im-config -n ibus
# 注销重新登录后，设置 → 键盘 → 输入源 中添加「中文(拼音)」
```

确认 Qt6 插件存在（应有一行输出）：

```bash
ls /usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so
```

若无此文件，再装 Qt6 本体（插件随 `libqt6gui6` / `qt6-base-dev` 或 `ibus` 提供，视版本而定）：

```bash
sudo apt install -y qt6-base-dev ibus
```

**fcitx5 已在运行但装不了 `fcitx5-frontend-qt6`（Ubuntu 20.04 常见）：**

fcitx5 支持 **ibus 兼容协议**，只需安装 ibus 的 Qt6 插件，程序会自动用 `QT_IM_MODULE=ibus` 接入 fcitx5：

```bash
sudo apt install -y ibus
ls /usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so
cd client && qmake6 charge-client.pro && make -j4 && ./charge-client
# 终端应显示: [IME] ... plugin=ibus
# 以及: fcitx5 已通过 ibus 兼容层接入
```

**fcitx5（Ubuntu 22.04+，有 fcitx5-frontend-qt6 时）：**

```bash
sudo apt update
# 仅 Ubuntu 22.04+ 通常有此包；20.04 请改用 ibus
sudo apt install -y fcitx5 fcitx5-chinese-addons fcitx5-frontend-qt6
im-config -n fcitx5
# 注销重新登录
```

若仍提示找不到 `fcitx5-frontend-qt6`，可搜索替代包名：

```bash
apt-cache search fcitx5 | grep -i qt6
lsb_release -a    # 查看 Ubuntu 版本
```

Ubuntu 20.04 及更早版本建议 **不要装 fcitx5**，改用 ibus + `im-config -n ibus`。

### 第三步：重新编译并启动

```bash
cd client
qmake6 charge-client.pro && make -j4
./run-client.sh
```

或手动指定（与桌面一致）：

```bash
# ibus 用户（最常见）
export QT_IM_MODULE=ibus
export XMODIFIERS=@im=ibus
./charge-client

# fcitx5 用户
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
./charge-client
```

### 使用技巧

1. 先用鼠标**点击地址输入框**获得焦点  
2. 再按 **Super+Space** 或 **Ctrl+Space** 切换输入法  
3. 若仍无效：设置 → 键盘 → 输入源，确认已添加「中文(拼音)」

### 临时替代

- 顶部 **「选择区域」** 下拉预设坐标  
- 在浏览器/记事本打好中文地址，**复制粘贴**到地址框

## 演示账号

- 13800138001（正常，有余额）
- 13800138006（冻结，登录应失败）

协议见 `../docs/protocal.md`。
