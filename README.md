# 智能家居中控面板（STM32MP157 / Qt Widgets）

基于 **Qt Widgets** 的触摸屏中控界面，同一份源码同时构建两个目标：

| 目标 | 环境 | 说明 |
|---|---|---|
| 桌面 | Qt 6.4 / x86_64 | 日常开发调试 |
| 开发板 | Qt 5.12.9 / ARM (STM32MP157, linuxfb) | 交叉编译后部署上板 |

界面固定 **1024×600**，为裸 framebuffer（linuxfb，无窗口管理器）环境设计。

---

## 1. 功能模块

| 模块 | 说明 |
|---|---|
| 主界面 | 4 个页面，左右滑动翻页；翻页时底部面板有滑入动画 |
| 设备控制 | 音乐 / 灯光 / 摄像头 / 蓝牙 / WiFi / 门锁 / 阳光 / 水 / 天气 / 月亮 / 安防 / 电视 |
| 安防监控 | 三态布防（离家 / 在家 / 撤防）、4 路自绘伪监控画面、报警闪烁与记录 |
| WiFi 设置 | 扫描热点（带信号强度/加密标记）、连接、断开、DHCP 获取并显示 IP |
| 串口 / TCP 调试助手 | 串口与 TCP 服务端/客户端双模；波特率/数据位/停止位/校验位菜单；端口热插拔枚举；HEX 显示、时间戳、字节计数 |
| 软键盘 | linuxfb 无输入法环境下的自绘软键盘，可拖动 |
| 小游戏 | 「愤怒的小鸟」：自研 2D 刚体物理、全 QPainter 绘制、实时合成音效 |

## 2. 技术要点

- **多线程**：`WifiTool` 通过 `moveToThread()` 跑在独立线程，GUI 只发队列信号；自定义类型用 `qRegisterMetaType` 注册；关闭窗口时用原子标志中止正在执行的 `iw` / `wpa_supplicant` 子进程，避免卡死。
- **外部命令调用**：`QProcess` + 绝对路径 + 超时 + `kill`，所有 WiFi 操作不阻塞界面。
- **串口**：`QSerialPort`，端口列表在菜单弹出时现场枚举（支持热插拔）；参数设置检查返回值。
- **TCP**：`QTcpServer` / `QTcpSocket`，服务端监听 `0.0.0.0`，客户端连对端；连接状态以 socket 实际状态为准，不看按钮文字。
- **自绘**：监控画面、游戏、软键盘全部 `QPainter` 绘制，无 GPU 依赖、无大图资源。
- **触摸手势**：`qApp->installEventFilter()` 全局拦截 + 坐标映射到 `stackedWidget`，同时兼容 Qt5 / Qt6 的鼠标与触摸 API。
- **linuxfb 适配**：手写窗口拖动、关闭子窗口后主动重绘主窗口、软键盘替代系统输入法。

## 3. 目录结构

```
my_mp157_smart_home_panel/
├── my_mp157_smart_home_panel.pro  qmake 工程（保留在根目录：交叉编译脚本按此查找）
├── res.qrc                        Qt 资源清单（保留在根目录：内部路径相对本文件）
├── src/                           所有 .cpp / .h
│   ├── main.cpp / mainwindow.*    主窗口：滑动翻页、面板动画、页面入口
│   ├── serialport.*               串口 / TCP 调试助手
│   ├── wifitool.*                 WiFi 工作线程（QProcess 调 iw/wpa_supplicant/udhcpc）
│   ├── wifidialog.*               WiFi 设置界面
│   ├── softkeyboardbinder.*       软键盘绑定器
│   ├── keyboardialog.*            自绘软键盘 + 拖动逻辑
│   ├── shielddialog.* / videowidget.*  安防监控
│   └── game.*                     小游戏（自研物理）
├── ui/                            Qt Designer 界面（绝对定位，1024×600）
├── res/                           图片资源（壁纸在 res/images/）
├── docs/软键盘使用说明.md          软键盘接入文档
└── README.md
```

> 目录调整后，如果 Qt Creator 里显示旧的文件列表，重新执行一次 qmake（构建 → 执行 qmake）即可。

## 4. 构建

### 4.1 桌面（Qt 6）

依赖：Qt 6 的 `base`、`svg`、`multimedia`、`serialport`、`network` 模块。

```bash
mkdir -p build-desktop && cd build-desktop
qmake6 ../my_mp157_smart_home_panel.pro
make -j4
./my_mp157_smart_home_panel
```

### 4.2 开发板（ARM 交叉编译）

使用工作区脚本 `~/QtDemos/tools/qt-build-arm.sh`（脚本里需要包含本工程分支）。
注意该工程用到 **serialport** 模块，交叉编译脚本里的头文件路径（INCS）和链接库（LIBS）两处都要加，只改一处会编不过。

```bash
~/QtDemos/tools/qt-build-arm.sh my_mp157_smart_home_panel
```

## 5. 开发板运行依赖

| 依赖 | 路径 / 说明 |
|---|---|
| 无线网卡 | `wlan0`（板载 RTL8723DS） |
| `iw` | `/usr/sbin/iw`，扫描与查询连接状态 |
| `wpa_supplicant` / `wpa_cli` | `/usr/sbin/`，关联热点、查询握手状态 |
| `udhcpc` | `/sbin/udhcpc`，关联成功后获取 IP |
| 配置脚本 | `/etc/udhcpc.d/50default` |

程序会自动探测这些工具的常见路径，探测不到时回退到出厂镜像的默认位置。

## 6. 验证状态

| 项 | 状态 |
|---|---|
| 桌面 Qt 6.4 编译 | ✅ 0 error / 0 warning（`game.cpp` 已清理） |
| 桌面启动 + 对话框冒烟测试 | ✅ 主窗口、串口助手、WiFi 设置均可正常构造与析构 |
| 资源完整性（qrc 含壁纸别名） | ✅ 运行期无资源缺失告警 |
| 串口环路（pty 端到端） | ✅ 此前已验证 31/31 |
| ARM 交叉编译 | ✅ `qt-build-arm.sh` 通过，产物在 `build-arm/` |
| 上板运行 | ✅ 已在 STM32MP157 开发板运行 |
| WiFi 连接链路端到端 | ⚠️ 尚未在真实热点上验证，仅完成代码链路 |

> WiFi 真连仍建议补测；上板运行已验证。

## 7. 踩坑记录

| 现象 | 根因 | 处理 |
|---|---|---|
| 全屏子窗口关闭后主界面定格 | linuxfb 没有窗口管理器/合成器，不会自动重绘 | 返回后 `show(); raise(); update(); repaint();` |
| 软键盘窗口整屏白 | linuxfb 不支持 `setMask()` | 改用自绘键盘 + 自己实现窗口拖动 |
| 串口一条数据被拆成两行 | 每批 `readyRead` 都 `append()` 成新段落 | 移到末尾 `insertPlainText()` 续写 |
| 收到二进制数据乱码 | 接收区永远按 UTF-8 显示 | 接入 Hex 显示开关（`appendData()` 统一处理） |
| TCP 断开后再连接崩溃 | `disconnected` 里 `deleteLater()` 了成员 socket | 移除删除逻辑，成员对象复用 |
| 关闭窗口时动画崩溃 | 动画还在跑，目标控件已析构 | `QPropertyAnimation(panel, "geometry", this)` 挂父对象 |
| 事件过滤器收不到子控件事件 | 只装了顶层 widget | `qApp->installEventFilter()` + `isAncestorOf()` 判断 |
| 板子 Qt5.12 编不过 | `errorOccurred` 是 Qt5.15+ 才有 | `#if QT_VERSION` 分支退回 `QOverload<...>::of(&QAbstractSocket::error)` |
| 触摸/鼠标 API 在 Qt5/Qt6 不同 | `globalPos()` vs `globalPosition()` | 统一封装成 `globalPosOf()` 等兼容函数 |

## 8. 已知限制

- TCP 服务端目前只支持**单个客户端**（第二个连接会顶掉第一个）。
- 所有 `.ui` 使用绝对定位，固定 1024×600；换分辨率需要重新布局。
- 监控画面与小游戏音效为**程序合成**，不是真实摄像头/音频输入。
- WiFi 连接失败时的错误提示较粗（密码错误 / 热点不可达未细分）。

## 9. 面试讲解要点

1. **为什么监控画面要自绘？** 板子没有 GPU，贴大图 + QSS 滤镜每帧重绘很慢；画矩形/线段几乎无开销，而且雪花、扫描线、画面抖动可控。
2. **WiFi 扫描为什么要放线程里？** 单次 `iw scan` 最长 25 秒，放 GUI 线程会冻结事件循环；用 `moveToThread` + 队列信号，并用原子标志在关窗时中止子进程。
3. **串口怎么保证不丢帧？** 接收缓冲区累积 + 按帧边界解析；界面层用末尾续写避免拆行，Hex/时间戳/字节计数统一走一个入口。
4. **linuxfb 上没有窗口管理器怎么拖动窗口？** 自己处理 `mousePress/Move/Release`，按全局坐标算偏移。
