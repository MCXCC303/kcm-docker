# 变更记录

本文件记录 Kontainer 的对外可见变更（ARCH_V3_pre §2.14）。

格式约定：

- 版本号遵循 `MAJOR.MINOR.PATCH`。当前尚未发布正式版本，各期为开发里程碑。
- **权限模型变化**（例如从「依赖用户在 socket 所属组」改为「通过 polkit 提权」）、
  **新增破坏性操作**（remove / prune / 删除数据卷）、**提权范围扩大**这三类变更，
  必须在对应条目标注 `⚠️ 权限` 并单独成段说明影响，同时提升次版本号，
  以便用户与分发渠道（AUR / 各发行版打包）能清楚感知变化。

---

## 四期（开发中）

首批写操作 · 挂载与端口信息架构（[ARCH_V4.md](ARCH_V4.md)）。

> ⚠️ **权限**：四期起 Kontainer 不再是只读工具。写操作**按 socket 实际权限**工作，
> 不引入任何提权机制（无 KAuth、无 polkit policy、不修改 socket 权限、不调用 `docker` CLI）。
> 启动时探测 socket 可写性：不可写时**写入口整体不出现**，并说明原因；
> 运行中若引擎返回 403 / EACCES，本次会话降级为只读。
> **注意：使用系统级（root daemon）Docker 时，把用户加入 docker 组等价于给予 root 权限。**
> 本项目的目标部署形态仍是 rootless Docker（socket 由用户自己拥有），此时不涉及该风险。

新增能力：

- 容器操作：启动 / 停止 / 重启 / 删除（删除不删除数据卷，且强制二次确认，并说明后果）
- 镜像操作：拉取（分层进度 + 可取消，匿名拉取，不读取任何 registry 凭据）、删除
  （单标签只删该标签；多标签需显式选择「删除全部标签」，走 `force`）
- 写操作地基：全部 REST 路径集中到 `backend/docker_api_paths.h`；传输层支持 POST / DELETE、
  流式响应增量解析与取消；`DockerError` 增加 409（状态冲突）分类，错误分为
  「用户可自行解决 / 环境问题 / 意外」三类，UI 呈现不同
- 挂载分区重构：类型 / 读写模式 / 命名卷名 / 宿主路径存在性，并可在**系统文件管理器中打开宿主目录**
  （唯一的非 Docker 外部动作，限制在 `kio_host_path_service.*` 一个文件里）
- 端口映射改为**芯片 + 连线拓扑**：左列容器端口、右列宿主绑定，未发布的端口单独成组；
  连线只是装饰，信息全部由文字承载
- 破坏性操作统一走 `ConfirmDialog`（固定句式 + 必填的后果说明）；
  所有操作结果走唯一的 `OperationMessage` 通知通道

**权限**：见上方 ⚠️ 说明。次版本号从 0.3.0 提升到 0.4.0。

---

## 三期

UI 收口，**保持只读**（[ARCH_V3.md](ARCH_V3.md)）。

- 组件归一：状态徽标（`StatusChip`）、可复制字段（`CopyableText`）、空状态（`EmptyPlaceholder`）
  收敛为唯一实现；状态语义色集中到 `StatusPalette`
- 容器详情拆分为 概览 / 资源 / 网络 / 挂载 / 日志（占位）分区
- 镜像详情：层列表默认折叠前 5 层、多 tag 以 chip 呈现
- 数据可视化使用独立色板（亮/暗两套，过 WCAG AA），Storage 增加堆叠条
- 排版与响应：详情页限宽、数值右对齐、统计卡按窗口宽度重排
- 新增 i18n 裸字符串 lint 与 `CHANGELOG.md`

**权限**：无变化。三期不新增任何非 `GET` 请求，不引入提权机制（决策记录见 ARCH_V3.md §1.3）。

---

## 二期

可交互的只读 Dashboard（[ARCH_V2.md](ARCH_V2.md)）。

- 卡片导航：容器 / 镜像列表 → 详情页（页内 `StackView`，不依赖宿主 KCM 的 push/pop）
- 搜索 / 状态过滤 / 排序（条件保存在 proxy，后台刷新不重置）
- 统一状态体系：State（机器可读）/ Status（Docker 摘要）/ Health（独立语义）严格分离，
  颜色只作辅助，始终配文本与图标
- 刷新策略集中到 `refresh_policy.h`：5s 高频 + 30s storage + 手动刷新，
  Last Updated / Update failed / Data is stale，后台刷新不清空列表
- 容器资源监控：CPU / 内存 / 网络 / 块 IO，5s 采样、内存中保留 60 点，离开详情页立即停止
- Docker 磁盘占用（`GET /system/df`）
- 国际化：翻译域 `kcm_docker`，随附完整简体中文翻译
- 只读边界强化：Environment / Labels 默认折叠；剪贴板只提供标识类字段

**权限**：无变化，仍为只读。

---

## 一期

只读状态面板（[ARCH_V1.md](ARCH_V1.md)）。

- Docker Engine 只读访问：`/_ping`、`/version`、`/info`、`/containers/json`、`/images/json`
- 异步 `QLocalSocket` HTTP 客户端（无阻塞 I/O）、API 版本协商、请求去重
- Engine 概要、容器列表、镜像列表
- 分层：DTO → Domain → Model → UI，Backend 不依赖 UI，UI 不解析 Docker JSON

**权限**：只读；要求当前用户对 Docker socket 有读权限。
注意：**若使用系统级（root daemon）Docker 并把用户加入 docker 组，该组等价于 root 权限。**
本项目的目标部署形态是 rootless Docker（socket 由用户自己拥有），此时不涉及该风险。
