# Kontainer

KDE Plasma 6 / System Settings 里的 **Docker 状态面板 / Dashboard**（KCM）。

> **只读**：整个项目不会修改任何 Docker 状态（没有 start/stop/restart/remove/pull/push/prune，
> 没有 POST/PUT/PATCH/DELETE）。这是设计约束，不是临时限制。

- 一期（[ARCH_V1.md](ARCH_V1.md)）：只读状态面板 —— Engine 概要、容器列表、镜像列表
- 二期（[ARCH_V2.md](ARCH_V2.md)）：可交互的只读 Dashboard —— 卡片导航、详情页、搜索/过滤/排序、
  统一状态体系、刷新/Last Updated/Stale、容器资源监控、Docker 磁盘占用
- 三期（[ARCH_V3.md](ARCH_V3.md)）：UI 收口 —— 组件归一（状态徽标 / 可复制字段 / 空状态）、
  容器详情分区、镜像详情收敛、数据可视化色板、排版与响应；**仍然只读**

---

## 能力一览

| 区域 | 内容 |
| --- | --- |
| Engine | 连接状态、Endpoint、Engine 版本、协商后的 API 版本（含 daemon 最低版本）、OS/架构、内核、cgroup、存储驱动 |
| Overview | 容器总数 / 运行中 / 已暂停 / 已停止、镜像数量（`/info` 概要不可用时显示 `—` 而不是 0） |
| Storage | Images / Containers / Volumes / Build cache / Total（来自结构化 API `GET /system/df`） |
| 容器列表 | 名称、短 ID、镜像、状态、健康、创建时间（相对）、端口概要；**整卡可点击进入详情**；搜索 / 状态过滤 / 排序 |
| 镜像列表 | 仓库标签、大小、创建时间、是否在用、是否悬空；搜索 / 过滤 / 排序；**整卡可点击进入详情** |
| Container Detail | Overview（状态/健康/镜像/ID/创建·启动·结束时间；容器名称与 ID 可复制）、Runtime（重启次数/退出码/OOM/PID/重启策略/平台）、Resources（CPU/内存/网络/块 IO + 短期趋势）、Network（网络/IPv4/IPv6/网关/MAC）、Ports、Mounts、Configuration（Entrypoint/Command/工作目录/用户/主机名 + Environment/Labels 折叠） |
| Image Detail | Repository / Tag / 完整引用 / ID（均可一键复制）、Digest、创建时间、大小、架构、OS、作者、Tags、Digests、Layers（层摘要 + 说明）、使用该镜像的容器、Environment（折叠） |
| 刷新 | 5 秒自动刷新（高频）+ 30 秒 storage（中频）+ 手动刷新；Last Updated / Update failed / Data is stale；后台刷新不清空列表、不重置搜索/过滤/排序 |
| 资源监控 | 每个详情页 5 秒采样一次，内存中保留 60 个采样点（约 5 分钟），离开页面立即停止采样并释放历史 |
| 状态体系 | State（机器可读）/ Status（Docker 摘要）/ Health（独立语义）严格分离；颜色只作辅助，始终配文本 + 图标 |
| 状态徽标 | 所有状态统一由 `StatusChip` 呈现（图标 + 颜色 + 文字三重编码）；语义 key → 主题色的映射只在 `StatusPalette` 里存在一份 |
| 可复制字段 | 容器名称/ID、镜像仓库/标签/完整引用/ID 统一使用 `CopyableText` / `CopyButton`；列表卡片也有复制入口 |
| 空状态 | 统一使用 `EmptyPlaceholder`（`Kirigami.PlaceholderMessage`）；「没有数据」「搜索无结果」「过滤无结果」文案互不相同，后两者提供清除条件的入口 |
| 容器详情分区 | 概览 / 资源 / 网络 / 挂载 / 日志（占位）五个分区，各自滚动；页面不再是一个超长滚动条 |
| 镜像详情收敛 | 层列表默认只显示前 5 层、可展开全部；多 tag 以 chip 呈现 |
| 存储可视化 | Overview 的存储区有横向堆叠条（镜像/容器/数据卷/构建缓存）+ 色块图例；不可用的类别显示 `—`，不伪装成 0 |
| 数据可视化配色 | 趋势线与存储条使用 `ChartPalette` 的专用取色（亮/暗各一套，均通过 WCAG AA 4.5:1 校验），不再借用状态语义色 |
| 排版与响应 | 详情页正文限宽 42 gridUnit 居中；资源数值右对齐；统计卡按窗口宽度 5/3/2 列重排；数值字号走 `Kirigami.Heading` |
| 国际化 | 全部用户可见文本走 KDE i18n（C++ 与 QML），含复数形式；已随附简体中文翻译；并有 lint 测试阻止裸字符串 |

---

## 只读安全边界

- 生产代码只发起 **GET**：`/_ping`、`/version`、`/info`、`/containers/json`、`/images/json`、
  `/system/df`、`/containers/{id}/json`、`/images/{id}/json`、`/containers/{id}/stats?stream=false`
- 没有 KAuth helper、不修改 socket 权限、不使用 `docker` CLI 读数据（§24）
- 日志只记录方法与路径、错误分类；**不输出 Docker JSON、environment、labels、mount 源路径、认证材料**
- Environment / Labels 默认只显示数量，用户显式展开才渲染取值（§40）
- 剪贴板只提供标识类字段的复制（容器名称/ID、镜像仓库/标签/完整引用/ID），不提供“复制整个 inspect JSON”（§41）
- UI 线程不做阻塞 I/O；全部请求基于 `QLocalSocket` + 事件循环
- **只读边界由测试守着**：`tst_source_conventions` 会在生产代码里出现
  `"POST"/"PUT"/"PATCH"/"DELETE"` 字面量，或引入 `QProcess` / `KAuth` 时直接失败——
  打开写操作必须是一次显式的设计变更，而不是某次顺手加上的请求

---

## 架构分层

```text
KDE System Settings / kcmshell6
        │
        ▼
KCM Layer              src/kcm/docker_kcm.*        生命周期、QML 加载、QML 类型注册
        │              src/ui/main.qml             页面导航（StackView）
        ▼
Presentation           src/ui/*.qml                纯展示；状态判断只用单一状态 key
        │              src/model/*                  Model / Proxy / Controller / Metrics / Scheduler
        ▼
Domain                 src/domain/*                 稳定语义（Container / ContainerDetail / ContainerStats /
        │                                            Image / ImageDetail / StorageUsage / EngineInfo）
        ▼
DTO                    src/dto/*                    Docker API schema 镜像 + 宽容解析 + → domain 映射
        │
        ▼
Backend                src/backend/*                只读 GET、Unix socket、HTTP、API 版本协商、请求去重
        │
        ▼
Docker Engine HTTP API (Unix Domain Socket)
```

规则：
- QML 不访问 Docker、不解析 JSON、不做统计计算；状态判断用字符串状态 key（`stateKey` / `engineStateKey` / …），
  避免同一个类里多个 `Q_ENUM` 同名成员导致 QML `Type.Loading` 解析错位
- `v1.xx` 只出现在 `backend/docker_api_version.cpp`
- domain 不反向依赖 dto（映射函数放在 dto 层）
- Backend 不知道任何页面/导航信息（§43）

### 页面导航

详情页使用 KCM **页面内** 的 `QQC2.StackView`（`src/ui/main.qml`）。

原因：`KQuickConfigModule::push()` 在本机 kcmshell6 中不生效（实测 `kcm.depth` 始终为 1，
不显示也不报错），使用页内 StackView 可同时兼容 kcmshell6 与 systemsettings，
且 Backend/Model 依旧完全不知道页面结构。

---

## 刷新策略（集中定义）

所有间隔与阈值只出现在 `src/refresh_policy.h`：

| 常量 | 值 | 用途 |
| --- | --- | --- |
| `kDefaultRefreshInterval` | 5s | Engine 概要 + 容器列表 + 镜像列表 |
| `kStorageRefreshInterval` | 30s | Docker disk usage（相对昂贵） |
| `kStatsSampleInterval` | 5s | 容器资源采样（仅详情页打开时） |
| `kDetailRefreshInterval` | 10s | 详情页静态信息低频复核 |
| `kMetricsHistorySamples` | 60 | 内存中的短期采样数（约 5 分钟） |
| `kStaleAfterFailedCycles` | 2 | 连续失败多少个刷新周期后标记 stale |

`RefreshScheduler` 负责定时器、刷新原因、Last Updated（成功时间）与失败周期计数；
`MetricsModel` 负责自己的采样生命周期（页面进入开始、离开立即停止并释放历史）。

---

## 构建与运行

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/kde/usr
cmake --build build
cmake --install build

source build/prefix.sh
kcmshell6 kcm_docker
```

依赖：CMake ≥ 3.20、Qt 6.5+（Core/Gui/Qml/Quick/Network/Test）、KF6（CoreAddons、Config、I18n、KCMUtils）、ECM。

- KCM 插件安装到 `${KDE_INSTALL_PLUGINDIR}/plasma/kcms/systemsettings/`
- QML 打包进插件 qrc：`:/kcm/kcm_docker/main.qml` 等
- 译文安装到 `${KDE_INSTALL_LOCALEDIR}/<lang>/LC_MESSAGES/kcm_docker.mo`

### 指定 Docker endpoint

按 `DOCKER_HOST` → `$XDG_RUNTIME_DIR/docker.sock` → `/run/docker.sock` → `/var/run/docker.sock` 解析；
只支持本机 Unix socket。

```bash
DOCKER_HOST=unix:///run/docker.sock kcmshell6 kcm_docker
```

---

## 国际化

采用 KDE 标准布局，翻译域 = KCM 插件 id `kcm_docker`：

```text
po/
├── kcm_docker.pot        # 模板（xgettext 提取）
└── zh_CN/
    └── kcm_docker.po     # 简体中文（160 条，全部已翻译）
```

`ki18n_install(po)` 在构建时编译并安装 `.mo`。验证：

```bash
LANGUAGE=zh_CN kcmshell6 kcm_docker     # 需要先 source build/prefix.sh
```

新增语言与重新翻译见 `po/README.md`（`xgettext` 提取 → `msgmerge` 合并 → `msgfmt --check`）；
`tst_i18n_consistency` 会校验「翻译域 == 插件 id == 元数据 TranslationDomain」以及
「zh_CN 没有未翻译条目」，防止界面静默退回英文。

> 说明：`KLocalizedString::setApplicationDomain()` 是进程级全局设置。同一个宿主进程里加载
> 多个 KCM 时，翻译域是「最后构造者生效」——这是 KDE 惯例（每个 KCM 用自己的域），
> 但如果将来把多个模块合并进同一个进程，需要重新评估。

---

## 测试

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

| 测试 | 覆盖 |
| --- | --- |
| `tst_http_response_parser` | Content-Length / chunked（含逐字节切分、chunk extension、trailer）/ 连接关闭 / 畸形响应 |
| `tst_dto_parsers` | 容器/镜像列表、version、info：正常、缺可选字段、未知字段、类型错误、空列表、非法 JSON、坏条目跳过 |
| `tst_error_mapping` | HTTP 401/403/404/4xx/5xx 与 socket 错误 → 错误分类；每种错误都有用户可见文本 |
| `tst_api_version` | 版本解析、协商（过旧/过新/区间内）、路径前缀集中管理 |
| `tst_container_model` | 状态与健康映射、role 稳定性、端口摘要、短 ID、空模型 |
| `tst_filter_models` | 搜索（name/ID/image、repository/tag/ID、大小写不敏感）、状态/使用情况过滤、条件组合、默认排序、**刷新不重置条件** |
| `tst_metrics` | CPU 公式、零增量、计数器回绕、缺内存上限、page cache 扣除、首采样无速率、环形缓冲上限、离开页面释放历史、宿主内存视为无限制 |
| `tst_detail_controllers` | 详情页生命周期（进入/离开）、列表构建、错误与重试、镜像与容器只读关联、停止采样 |
| `tst_format` / `tst_status_controller` | 时间与体积格式化；整页/分区状态机、错误隔离、Last Updated 与 stale、刷新间隔来自 RefreshPolicy |
| `tst_i18n_consistency` | 翻译域一致性、译文完整性、**裸字符串 lint**（界面里的 `text`/`title`/`Accessible.name`/`ToolTip.text` 等属性被赋字符串字面量即失败，并给出文件名与行号） |
| `tst_source_conventions` | 复制动作只有 `CopyButton` 一个实现；状态语义色只出现在 `StatusPalette`；**只读边界**（生产代码不得出现写请求动词 / `QProcess` / `KAuth`） |
| `tst_qml_load` | 逐个编译界面文件 + 真正实例化页面 + **触发卡片 activated 信号**验证导航接线 + 断言 Environment/Labels 默认折叠（§40）+ 状态徽标语义映射 + 复制按钮的空值禁用与剪贴板行为 + 三类空状态文案互不相同 + 容器详情五分区切换与「切分区不重新 inspect」+ 镜像层默认折叠前 5 层 + 捕获 QML 运行时错误（ReferenceError/TypeError）——这类错误在 kcmshell6 里只会显示错误页或静默失效 |
| `tst_refresh_churn` / `tst_kcm_widget_churn` | 刷新抖动压力测试：数据、窗口尺寸、分区、页面进出反复变化；后者用 **QQuickWidget**（与 kcmshell6 相同的宿主形态）承载 `main.qml`，并断言「同一结构下的数值刷新不得重建统计块与存储图例的条目」——针对真实会话里出现过的布局 polish 段错误 |
| `tst_qml_resource` | **从 qrc 加载界面**（与插件运行时完全一致的路径）：`main.qml` 能加载、源码目录里每个界面文件都在资源里且内容一致（期望值由扫描源码树得出，不维护第二份清单）、单例能从 qrc 解析。资源清单漏项这类问题不会被源码目录测试发现，只会让安装后的 KCM 打不开 |
| `tst_docker_backend_against_fake_engine` | 进程内假 Engine：协商、chunked、去重、inspect/stats/df 解析、`/info` 失败后计数作废、版本不匹配、stats 生命周期 |
| `tst_docker_backend_integration` | 真实 Docker 只读端到端（无 socket 时自动跳过） |

> 注意：这台机器上的 Qt 6.11 只执行「无参测试函数 + `QFETCH`」形式的数据驱动用例，
> 带参数的测试函数会被**静默跳过**，因此所有数据驱动测试都使用 `QFETCH` 写法。

`tests/support/mock_docker_backend.*` 与 `tests/support/qml_stub_kcm.*` 让 model / controller /
QML 测试都不依赖真实 Docker（§30/§46）。

### 不开 Docker 也能开发 UI

```bash
tests/tools/fake_docker_server.py /tmp/fake-docker.sock --empty   # 去掉 --empty 看有数据的样子
DOCKER_HOST=unix:///tmp/fake-docker.sock kcmshell6 kcm_docker
```

### 离屏截图（亮 / 暗两套，不干扰桌面）

```bash
cmake --build build --target kontainer_render_ui
tests/tools/render_ui.sh main             1200 900 light build/logs/main-light.png
tests/tools/render_ui.sh container-detail 1200 900 dark  build/logs/detail-dark.png
tests/tools/render_ui.sh image-detail     1200 900 light build/logs/image-light.png
```

用确定性 fixture（`MockDockerBackend`）渲染，因此同样的命令永远得到同样的图，
适合做「改版前后」对比与亮/暗主题复核。包装脚本会准备 KDE 配色方案
（复制 `/usr/share/color-schemes/BreezeLight.colors` / `BreezeDark.colors`
到私有 `XDG_CONFIG_HOME`）并设置 KDE 的 QQC2 样式——
这两点都必须在进程启动前完成，否则渲染结果不代表真实会话（详见
`tests/tools/render_ui.sh` 与 `tests/tools/render_ui.cpp` 里的说明）。

另：`tests/tools/vnc_grab.py` 是早期的 VNC 抓图工具；本机 Qt 的 `vnc` platform
插件在渲染期间会段错误 / 不响应更新请求，因此截图请使用上面的离屏渲染工具。

### 调试日志

```bash
QT_LOGGING_RULES="kontainer.*.debug=true" kcmshell6 kcm_docker
```

分类：`kontainer.backend` / `kontainer.api` / `kontainer.model` / `kontainer.kcm`。

---

## 目录结构

```text
CMakeLists.txt
ARCH_V1.md / ARCH_V2.md     一期 / 二期开发规约
ARCH_V3.md                  三期开发规约（UI 收口 · 保持只读）
ARCH_V3_pre.md              三期讨论草案，历史记录（不修改）
CHANGELOG.md                版本变更记录
README.md
src/
├── CMakeLists.txt          核心库 kontainer_core + KCM 插件 kcm_docker + QML 资源
├── refresh_policy.h        刷新间隔与阈值的唯一来源
├── i18n.* / logging.*      翻译域与日志分类
├── kcm/                    docker_kcm.{h,cpp}, kcm_docker.json
├── backend/                docker_client / endpoint / error / api_version / http / backend
├── dto/                    API schema 镜像与宽容解析（container/image/inspect/stats/storage）
├── domain/                 稳定语义：Container / ContainerDetail / ContainerStats / Image /
│                           ImageDetail / StorageUsage / EngineInfo
├── model/                  ContainerModel / ImageModel / Filter 代理 / 详情 Controller /
│                           MetricsModel / RefreshScheduler / StorageStatus / Format / Presentation
└── ui/                     main.qml, MainPage.qml, *Card.qml, *Detail.qml, *View.qml
    └── components/         StatusChip / CopyButton / CopyableText / EmptyPlaceholder /
                            CollapsibleSection / KeyValueList / StatTile / MiniTrend /
                            StorageBar + 单例 StatusPalette / ChartPalette（qmldir）
tests/                      19 个测试目标 + support（mock/stub/qml_item_utils.h）
└── tools/                  fake_docker_server.py（假 Engine）
                            render_ui.{cpp,sh}（离屏截图，亮/暗两套）
                            vnc_grab.py（早期 VNC 抓图，本机不可用，见上）
po/                         翻译（zh_CN 已完整）
```

---

## 与 ARCH_V2 / ARCH_V3_pre 的已知偏离

三期（ARCH_V3 §四）新增/确认的偏离：

| 项 | 规范 | 实现 | 说明 |
| --- | --- | --- | --- |
| 卡片基类 | ARCH_V3_pre §1.3 建议统一用 `Kirigami.AbstractCard` | **列表行用 `QQC2.ItemDelegate`，统计卡用自绘容器**（颜色/圆角/字号全部取自主题） | `AbstractCard` 会接管 `contentItem`：把它包进 `KirigamiLayouts.Padding`（`visible: contentItem !== null`）并用 `onXChanged/onYChanged` 强制覆盖内容坐标（Kirigami 6.30 `templates/AbstractCard.qml:110-140`）。二期正是在这里踩到「折叠区与标题重叠」的 bug。列表行还需要 hover/focus/Enter 与 `Accessible.role`，`ItemDelegate` 更合适 |
| `Repeater` model | 无规定 | 一律使用稳定的数值（`xxx.length`）+ 索引取值 | JS 数组属性每次刷新都会重新求值，直接当 model 会销毁重建全部条目；「布局算尺寸时条目被销毁」正是实际会话段错误的触发条件（详见 ARCH_V3 附录 A.1d） |
| 页面过渡 | ARCH_V3_pre §1.7 用 Kirigami 自带过渡 | 页内 `StackView` 显式使用空 `Transition {}`（`main.qml`） | KCM 是配置界面而非内容浏览界面，切换动效会带来「窗口在跳」的观感；四期若加入日志流等长驻页面可重新评估 |
| 数值精度 | ARCH_V3 §2.5 原计划新增定精度字节格式化变体 | **未新增**，沿用 `KFormat::formatByteSize` | 实测 `KFormat` 的默认精度就是 1 位小数（`formatByteSize(100) == "100 B"`），再加一层变体属于重复实现且偏离 KDE 惯例；三期的实际问题是**对齐**而不是小数位，已通过右对齐解决 |
| 端口「两行并一行」 | ARCH_V3_pre §1.9 | 现状本就每端口一行；本次收敛标签列宽与省略，避免折行 | 原截图所指待与讨论方确认 |

二期以来沿用的偏离：

| 项 | 规范 | 实现 | 说明 |
| --- | --- | --- | --- |
| 详情页导航 | §5 推荐页面导航 | 页内 `StackView` | `KQuickConfigModule::push()` 在 kcmshell6 下是 no-op（实测 depth 恒为 1），因此不依赖宿主子页面机制；仍是页面级导航而非 dialog |
| 翻译域 | §39 未指定域 | `kcm_docker`（= 插件 id） | 与系统 KCM 惯例一致（`share/locale/<lang>/LC_MESSAGES/kcm_*.mo`）；QML 与 C++ 共用 |
| 列表滚动 | §36/§37 关注大列表性能 | `ListView`（虚拟化 + 内部滚动），概览区固定在顶部 | 列表与概览区域分离，避免嵌套滚动 |
| Layer 大小 | §8 提到 Layer 信息 | 只显示层 digest + 明确说明 | Docker API 的 `RootFS.Layers` 不提供单层大小，不编造数据 |
| 测试 fixture | §45 建议 kontainer-test-* 容器 | 进程内假 Engine + 现有容器 | 不向用户 Docker 环境写入任何容器（DoD：测试不得以用户环境为唯一 fixture） |
| CI | §31 | 尚未配置 | 本机流程已验证；平台待定 |
| 详情页静态信息复核 | §13.2「低频 / 页面进入时」 | 30 秒（`kDetailRefreshInterval`） | 动态数据由 5 秒 stats 采样承担，静态 inspect 不需要频繁重取 |
| 容器排序 | §10 提到 Updated / Started | 只提供 Name / State / Created | 列表 API（`/containers/json`）不返回 StartAt，无法在不额外 inspect 的前提下排序 |
| 性能基准 | §36 列出 10/50/100 容器、500/1000 镜像 | 未建立基准 | 列表已用 `ListView` 虚拟化；基准测试待补（记录为技术债） |
| 模型更新粒度 | §13/§32 未强制 | 数据未变时不发信号；变化时仍是 `modelReset` | 已避免“每 5 秒无意义重置”，并在重置时恢复滚动位置；完全增量 `dataChanged` 留待后续 |
| 详情页列表刷新 | §13/§32 未强制 | 数据未变时**完全不动**模型（`DetailListModel::setEntries` 提前返回） | 原先详情页的端口/网络/挂载/关联容器等列表在每次 inspect 复核（30s）与容器列表刷新（5s）时都无条件 `modelReset`，QML 的 Repeater 因此周期性销毁重建 delegate；已由 `tst_detail_controllers` 与 `tst_kcm_widget_churn` 双向锁住 |

---

## 三期完成定义（DoD）自查

- 界面：`StatusChip` 是状态呈现的唯一实现 ✅、状态色 token 只在 `StatusPalette` ✅、
  `Repeater` 不再直接以 JS 数组为 model（数值刷新不重建条目）✅、
  `CopyableText`/`CopyButton` 覆盖全部标识字段且列表卡片有复制入口 ✅、
  `EmptyPlaceholder` 覆盖列表与详情空状态且四态文案互不相同 ✅、
  容器详情五分区（含日志占位）✅、Environment/Labels 默认折叠保持 ✅、
  镜像层默认前 5 层可展开 ✅、多 tag chip ✅、
  存储堆叠条 + 图例且缺项不伪装成 0 ✅、
  详情页限宽居中 + 资源数值右对齐 ✅、统计卡 `Heading` + 5/3/2 列重排（自绘容器，见偏离登记）✅、
  数据可视化色板与状态语义色分离且过 AA 对比度 ✅
- 工程：i18n lint 生效（人为插入裸字符串会被抓出并给出位置）✅、`CHANGELOG.md` 建立 ✅、
  zh_CN 译文完整（185 条、0 fuzzy、0 未翻译）✅、全部 17 个测试通过 ✅、
  新增界面文件全部进入 qrc 且由 `tst_qml_resource` 守着 ✅、
  只读声明仍然真实（并由 `tst_source_conventions` 守着）✅
- KDE：`kcmshell6 --smoke-test kcm_docker` 英文/中文均退出码 0 ✅、
  亮/暗主题离屏截图复核 ✅、键盘导航与焦点可见性未回归 ✅
- 说明：三期开工时曾出现「源码目录测试全绿、安装后的插件打不开」的回归
  （`StatusPalette.qml` / `ChartPalette.qml` 漏出资源清单），
  由新增的 `tst_qml_resource` 定位并修复；该测试即是为此类问题加的常驻防线。

---

## 二期完成定义（DoD）自查

- 功能：卡片可打开 Container/Image Detail ✅、搜索 ✅、状态过滤 ✅、排序 ✅、State/Status/Health 语义分离 ✅、
  5s 自动刷新 ✅、Last Updated ✅、Stale ✅、部分失败不影响其他模块 ✅、CPU/内存/网络/IO ✅、Storage ✅
- 只读：生产后端无 mutation API ✅、UI 无状态修改操作 ✅、测试不写用户 Docker ✅
- 工程：DTO/Domain/Presentation 分离 ✅、过滤排序在 model 层 ✅、Backend 不依赖 UI ✅、UI 不依赖 Docker JSON ✅、
  无阻塞 I/O ✅、请求去重 ✅、详情轮询有生命周期 ✅、metrics 不持久化 ✅
- KDE：Kirigami/Qt Quick ✅、KDE i18n（含 zh_CN）✅、Breeze Light/Dark（颜色全部取自 Kirigami.Theme）✅、
  键盘导航（列表与卡片可 Tab/Enter）✅、kcmshell6 实测可用 ✅

---

## 下一阶段（四期，不在三期范围）

按 ARCH_V3.md §7 的顺序：写操作地基（权限模型、变更传输层、统一反馈通道、确认对话框、
start/stop/restart → remove）→ 日志 → 卷与网络 → 创建/克隆 → exec → Compose → 诊断导出。

权限模型已经定好（ARCH_V3 §1.3）：**按 socket 实际权限工作，不引入提权机制**；
本机是 rootless Docker，OS 层面已放行写权限，引入 root helper 属于权限放大。
四期开工前只需把 README 顶部的「只读」声明改成对应的新表述并升次版本号。
