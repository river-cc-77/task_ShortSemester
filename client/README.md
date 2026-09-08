# charge-client（用户端）

组员 A 在此目录继续开发找桩、充电、我的等页面。

## 当前功能

- 登录、附近充电站、电站详情、预约/充电/结算
- **一键导航**：站点详情内打开百度地图驾车路线（QWebEngineView）
- **公告**：顶部「公告」按钮查看系统公告
- 地理编码、收藏、订单历史、个人中心

## 构建（Ubuntu 22.04）

```bash
sudo apt install -y qt6-base-dev qt6-webengine-dev

cd client
qmake6 charge-client.pro
make -j4
```

未安装 `qt6-webengine-dev` 时仍可编译（需去掉 `.pro` 中 `webenginewidgets`），导航会回退为系统浏览器打开。

## 运行

1. 启动 server（见 `../server/README.md`）
2. `./charge-client` 或 `./run-client.sh`（推荐，已配置中文输入法环境变量）

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

**ibus（多数 Ubuntu VM）：**

```bash
sudo apt install -y ibus ibus-pinyin ibus-gtk ibus-gtk3
im-config -n ibus
# 注销重新登录
```

**fcitx5：**

```bash
sudo apt install -y fcitx5 fcitx5-chinese-addons fcitx5-frontend-qt6
im-config -n fcitx5
# 注销重新登录
```

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
