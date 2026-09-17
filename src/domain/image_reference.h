/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <optional>

namespace Kontainer::ImageReference
{

/*!
 * 镜像引用的解析与校验（ARCH_V4 §2.4）。
 *
 * 纯函数、无 I/O、无 Docker 依赖，因此可以单测；backend 用它拼拉取请求的
 * `fromImage` / `tag`，QML 用它做提交前的输入校验（ARCH_V3_pre §2.8 的「校验前置」）。
 *
 * 支持的形式：
 *   alpine
 *   alpine:3.19
 *   library/alpine
 *   registry.example.com:5000/team/app:1.2.3
 *   alpine@sha256:0123...（digest 形式，此时不补 tag）
 */
struct Parts {
    /*! 显式给出的 registry（不含隐式的 docker.io）；没有则为空。 */
    QString registry;
    /*! 仓库路径（含 registry 之后的部分，例如 library/alpine）。 */
    QString repository;
    /*! 标签；digest 形式时为空。 */
    QString tag;
    /*! digest（sha256:...）；没有则为空。 */
    QString digest;

    /*! `fromImage` 参数：仓库（含 registry），digest 形式时带 @digest。 */
    QString fromImage() const;
};

/*! 解析失败返回 nullopt（空、含空白、非法字符、非法 tag/digest 都算失败）。 */
std::optional<Parts> parse(const QString &reference);

/*! 是否是可用引用（`alpine` 合法，缺 tag 时按 latest 处理）。 */
bool isValid(const QString &reference);

/*! 归一化：缺 tag 且无 digest 时补 `latest`；非法引用原样返回以便报错。 */
QString normalized(const QString &reference);

/*! 仅显示用的短形式：去掉 registry 前缀（`registry:5000/team/app:1` → `team/app:1`）。 */
QString shortForm(const QString &reference);

} // namespace Kontainer::ImageReference
