# 调试 Kontainer（在系统设置里用起来时怎么看它在干什么）

本文件回答一个具体问题：**这个 KCM 被"系统设置"（System Settings）加载之后，怎么观察、怎么定位问题。**
核心思路是先分清"宿主是谁"，再选对应的观察手段。

## 0. 三种运行形态，别混着谈

| 形态 | 命令 | 宿主进程 | 什么时候用 |
|---|---|---|---|
| 独立窗口 | `kcmshell6 kcm_docker` | `kcmshell6` | 最快的迭代；窗口小、日志直接可看 |
| **系统设置内** | `systemsettings kcm_docker` | `systemsettings` | **真实用户路径**：KCM 被塞进一个 `QQuickWidget` |
| 冒烟（非交互） | `kcmshell6 --smoke-test kcm_docker` | `kcmshell6` | CI / 提交前：只验证"能加载、不报错" |

关键差别：**系统设置里 KCM 是"嵌进别人的 QML 场景"的**（`QQuickWidget` + `StackView`），
因此焦点、尺寸协商、快捷键、以及"谁先销毁"都与独立窗口不同。
本项目历史上最难查的一次崩溃（`libKF6KIOWidgets` 的 `QTreeView::drawRow`）**只在系统设置那种宿主里**出现。
判断"是不是宿主相关问题"的最快办法：同一操作在 `kcmshell6` 里做一遍——
两边都崩 → 我们的代码；只有系统设置里崩 → 宿主/环境。

## 1. 一键启动（推荐）

```sh
tests/tools/debug_kcm.sh                     # systemsettings kcm_docker
tests/tools/debug_kcm.sh standalone          # kcmshell6 kcm_docker
tests/tools/debug_kcm.sh standalone --isolated   # 用临时 HOME（不碰真实 ~/.config/kontainerrc）
tests/tools/debug_kcm.sh settings --gdb      # 在 gdb 里跑
tests/tools/debug_kcm.sh --coredumps         # 看最近的崩溃
tests/tools/debug_kcm.sh --verbose-qml       # 打开 QML 绑定/连接日志
tests/tools/debug_kcm.sh --fatal-warnings    # 第一条 Qt 警告直接中止（定位"最早出错点"）
```

**从终端启动这一点很重要**：`systemsettings` 是普通用户进程，它（以及它加载的插件）的
`stderr` 就打在启动它的终端里。从菜单点开的系统设置看不到这些输出。

## 2. 不需要安装就能调试（省掉每轮 `cmake --install`）

插件是标准的 KCM 插件，构建产物落在 `build/bin/plasma/kcms/systemsettings/kcm_docker.so`，
而 **`QT_PLUGIN_PATH` 指向 `build/bin` 就够了**（元数据 `kcm_docker.json` 编译进了 `.so`，
不需要额外的 desktop 文件）。已验证：

```sh
QT_PLUGIN_PATH=$PWD/build/bin systemsettings --list | grep docker
# kcm_docker  - Docker Engine 状态面板，可管理容器与镜像
```

所以日常循环是：`cmake --build build` → `tests/tools/debug_kcm.sh`。
QML 走 **qrc**（编进 `.so`），因此**改了 QML 必须重新构建插件**（重新 `cmake --install` 不必要）。

## 3. 看日志

我们的日志类别（`src/logging.cpp`，前缀 `kontainer.`）：

| 类别 | 内容 |
|---|---|
| `kontainer.api` | 每个 HTTP 请求一行：`GET /v1.56/containers/json`（**只记方法与路径**，不记 header/payload/query——ARCH_V1 §27 的隐私约束） |
| `kontainer.backend` | 连接、协商到的 API 版本、请求合并、放弃在途请求 |
| `kontainer.model` | 模型/控制器层的状态与错误 key |
| `kontainer.kcm` | KCM 装配（提权客户端注入、翻译域等） |

```sh
# 全部打开（默认 debug 就是开的，这条只是防止会话配置把它关掉）
QT_LOGGING_RULES="kontainer.*=true" systemsettings kcm_docker

# QML 层面的"静默"问题：绑定被写坏、连接重复
QT_LOGGING_RULES="qt.qml.binding.removal.info=true;qt.qml.connections=true" systemsettings kcm_docker

# 只想看网络往返
QT_LOGGING_RULES="kontainer.api=true" systemsettings kcm_docker
```

拿真实引擎核对请求序列时，把日志与 `docker events`（另一个终端）对着看最直观。

## 4. QML 出问题怎么办

QML 的报错（绑定失效、属性不存在、delegate 里的 `ReferenceError`）会打到 `stderr`，
所以"从终端启动"同样能看见。更细的手段：

- `QT_FATAL_WARNINGS=1`：把第一条警告变成中止——想抓"到底哪一步先坏"时最有效。
- `QSG_VISUALIZE=overdraw` / `batches` / `clip`：把场景图叠加出来看（渲染/裁剪问题）。
- `QT_QUICK_CONTROLS_STYLE=...`、`QT_SCALE_FACTOR=2`：验证不同样式/缩放下是否错位。

> 说明：**QML 调试器（`-qmljsdebugger`）一般用不上**——它要求宿主程序在编译时启用 QML 调试，
> 而发行版打包的 `systemsettings` 不会开。要单步调 QML，请用仓库里的宿主：
> `tests/tools/render_ui.sh`（把 `MainPage.qml` 装进一个受控的 `QQuickWidget`，
> 带 `KONTAINER_RENDER_*` 开关，可复现任意界面状态），或 `tst_kcm_widget_churn`
> （它用的**就是**系统设置那种 `QQuickWidget` 宿主方式，是"宿主相关问题"的最近复现环境）。

## 5. 崩溃取证

```sh
coredumpctl list | tail                     # 最近的崩溃
coredumpctl gdb <PID>                       # 载入 core（bt / info sharedlibrary）
```

判断"是不是我们的锅"的一条硬指标：`(gdb) info sharedlibrary` 里有没有 `kcm_docker.so`
或 `~/kde/usr` 下的库，以及 **`bt` 里我们的帧在不在崩溃点附近**。
本项目上次那次 `libKF6KIOWidgets` 崩溃就是靠"两次 core 的偏移与调用链完全相同、
且其中一个进程是系统守护进程"确认是上游问题的。

## 6. 隔离与安全

- **不要拿真实容器做实验**：`--isolated` 会把 `HOME` 指到临时目录，
  挂载预设与命令历史就不会写到真实的 `~/.config/kontainerrc`。
- 提权动作（引擎页的"服务"卡片）会真的调 systemd，调试时先看清楚再点；
  polkit 的授权窗口是按动作记忆的，取消是正常结果（不会留下半成品状态）。
- 后端默认只读地读引擎信息；写操作都集中在 `OperationController` 一个入口（ARCH §2.3）。

## 7. 提交前的最小验证

```sh
cmake --build build && (cd build && ctest)          # 全部用例
kcmshell6 --smoke-test kcm_docker                   # 加载冒烟（en/zh 各一次）
tests/tools/render_ui.sh main 1150 700 light /tmp/x.png  # 界面渲染复核
```
