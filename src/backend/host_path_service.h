/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*! 宿主路径的状态（ARCH_V4 §2.1.1）。 */
enum class HostPathState {
    /*! 没有宿主路径（tmpfs、匿名挂载）：界面不提供打开动作。 */
    NotApplicable,
    Missing,
    /*! 路径存在但不是目录：bind 挂载指向文件时会出现。 */
    NotADirectory,
    Directory,
};

/*! 打开目录的失败原因（backend 不产出 UI 文案，只给原因）。 */
enum class HostPathError {
    None,
    Missing,
    NotADirectory,
    LaunchFailed,
};

/*!
 * 宿主路径服务（ARCH_V4 §2.1.1）。
 *
 * 为什么单独抽一个接口：这是四期唯一一个「不经过 Docker 的外部动作」，
 * 抽出来之后
 *  - 探测与打开都能在测试里替换成 Fake（不需要真的弹出文件管理器）
 *  - 生产实现里对 KIO 的引用被限制在一个文件内（tst_source_conventions 断言）
 *
 * 安全约定：只 stat，不读文件内容、不递归目录；挂载源路径属潜在敏感信息，
 * 实现与调用方都不得把它写进日志（ARCH_V2 §40）。
 */
class HostPathService : public QObject
{
    Q_OBJECT

public:
    explicit HostPathService(QObject *parent = nullptr);
    ~HostPathService() override;

    /*! 探测路径状态：一次 stat，不做任何 I/O 之外的事情。 */
    virtual HostPathState probe(const QString &path) const = 0;
    /*!
     * 用系统文件管理器打开目录。
     *
     * 返回 false 表示请求在本地就被拒绝（没发出任何动作）；
     * 真正的打开结果异步到来，经 openFinished 通知。
     */
    virtual bool openDirectory(const QString &path) = 0;

Q_SIGNALS:
    /*! `detail` 是技术细节（供日志/调试），不是用户文案。 */
    void openFinished(Kontainer::HostPathError error, const QString &detail);
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::HostPathState)
Q_DECLARE_METATYPE(Kontainer::HostPathError)
