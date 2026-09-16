# 变更记录

本文件记录 Kontainer 的对外可见变更（ARCH_V3_pre §2.14）。

格式约定：

- 版本号遵循 `MAJOR.MINOR.PATCH`。当前尚未发布正式版本，各期为开发里程碑。
- **权限模型变化**（例如从「依赖用户在 socket 所属组」改为「通过 polkit 提权」）、
  **新增破坏性操作**（remove / prune / 删除数据卷）、**提权范围扩大**这三类变更，
  必须在对应条目标注 `⚠️ 权限` 并单独成段说明影响，同时提升次版本号，
  以便用户与分发渠道（AUR / 各发行版打包）能清楚感知变化。

---

## 三期（开发中）

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
