/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * QML 侧共享的展示助手（ARCH_V2 §12/§38/§41）。
 *
 * 只回答“语义”问题（positive / neutral / negative、图标名），具体颜色仍由 QML 从
 * Kirigami.Theme 取（§12：颜色必须来自 KDE palette，不能硬编码 RGB）。
 * 这样状态颜色/图标规则只写一次，卡片、详情页、概览共用。
 */
class Presentation : public QObject
{
    Q_OBJECT

public:
    explicit Presentation(QObject *parent = nullptr);

    /*!
     * 由种子字符串取一个稳定的调色板下标（端口拓扑连线用）。
     *
     * 用途：让"同一个容器"的节点图连线颜色始终一致（ARCH_V4 §2.1.2 的观感约定）——
     * 种子一般是容器 id，刷新、重开页面、换主题都不会变。颜色本身不承载语义
     * （两端芯片的文字才是信息），因此只要求稳定、分布均匀、不要求可读性含义。
     *
     * 算法：FNV-1a（32 位）后取模，纯函数、跨平台一致，且不会随 Qt 版本变化。
     * `paletteSize <= 0` 或种子为空时返回 0。
     */
    Q_INVOKABLE int connectionColorIndex(const QString &seed, int paletteSize) const;

    /*! 状态 + 健康 → 语义色 key：positive / neutral / negative / disabled。 */
    Q_INVOKABLE QString stateSemanticKey(const QString &stateKey, const QString &healthKey) const;

    /*! 容器状态 → 图标名（icon theme name）。 */
    Q_INVOKABLE QString stateIconName(const QString &stateKey) const;

    /*! 健康状态 → 图标名；无健康检查时返回空。 */
    Q_INVOKABLE QString healthIconName(const QString &healthKey) const;

    /*!
     * 复制到剪贴板（ARCH_V2 §41）。
     * 只用于 Container ID / Image ID / IP / Port 这类标识；
     * 不提供“一键复制整个 inspect JSON”。
     */
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

    /* --- 键值对与列表编辑器的校验（ARCH_V5_V8 §1.6：只有一份实现） --- */

    /*!
     * 键名是否合法：`[A-Za-z_][A-Za-z0-9_]*`。
     * 环境变量、标签、build args 共用这一条规则。
     */
    Q_INVOKABLE bool isValidEnvKey(const QString &key) const;

    /*!
     * 解析 `.env` 形式的文本 → `[{ key, value }]`。
     *
     * 支持：注释（`#`）、空行、`export ` 前缀、单/双引号包裹的值、行内 `#` 注释。
     * 解析不了的行走"跳过"而不是报错（粘贴的内容经常是半结构化的人工文本）。
     */
    Q_INVOKABLE QVariantList parseEnvText(const QString &text) const;

    /*! 端口是否在合法范围内（1–65535）。 */
    Q_INVOKABLE bool isValidPort(int port) const;

    /*!
     * 宿主端口是否与已用绑定冲突。
     *
     * `usedBindings` 是既有绑定的字符串列表，形如 `0.0.0.0:8080`、`127.0.0.1:8080`。
     * 规则：同一 IP 的同一端口冲突；通配地址（`0.0.0.0` / `::` / 空）与任意具体 IP 的同一端口也冲突。
     * 端口冲突判定只此一处，创建表单与端口编辑器都调用它。
     */
    Q_INVOKABLE bool hostPortConflicts(const QString &hostIp, int hostPort, const QStringList &usedBindings) const;

    /*! 宿主 IP 是否是通配地址（`0.0.0.0` / `::` / 空字符串）。 */
    Q_INVOKABLE bool isWildcardHostIp(const QString &hostIp) const;

    /* --- 运行时配置（daemon.json）字段校验（ARCH_V5_V8 §2.3） --- */

    /*!
     * 镜像加速器地址的校验结果 key。
     *
     * 返回 key 而不是文案：规则在 C++（可测、只有一份），文案在 QML（可翻译）。
     * 空字符串 = 合法；`emptyHost` = 空输入；`invalid` = 形态不对。
     */
    Q_INVOKABLE QString registryMirrorErrorKey(const QString &value) const;

    /*! insecure-registries 的校验结果 key（只接受 `host[:port]`，不接受 scheme 与路径）。 */
    Q_INVOKABLE QString insecureRegistryErrorKey(const QString &value) const;
};

} // namespace Kontainer
