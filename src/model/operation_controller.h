/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/docker_capabilities.h"
#include "domain/image_pull_progress.h"

#include <QObject>
#include <QSet>
#include <QString>

namespace Kontainer
{

/*!
 * 写操作的编排与反馈（ARCH_V4 §2.2.4）。
 *
 * 职责：
 *  - 把界面意图（启动这个容器 / 拉这个镜像）转成 backend mutation
 *  - 保证同一个目标同时只有一个操作在途，并把忙碌状态暴露给界面
 *  - 把成功 / 失败 / 取消统一成一条结果通道（页面不许各自拼文案）
 *  - 维护拉取进度
 *  - 执行权限门：socket 不可写时不出现写入口；运行时撞上 403/EACCES 则本次会话降级为只读
 *
 * 不负责：HTTP、JSON、路径拼接（backend）、对话框与确认（QML）。
 */
class OperationController : public QObject
{
    Q_OBJECT

    /* --- 操作状态 --- */
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY stateChanged)

    /* --- 结果通道（唯一的操作结果呈现来源） --- */
    Q_PROPERTY(QString resultKey READ resultKey NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    /*! 引擎原文（数据，不翻译）；用于「为什么失败」的次要行。 */
    Q_PROPERTY(QString resultDetailText READ resultDetailText NOTIFY resultChanged)
    /*! none / userActionable / environment / unexpected —— QML 据此选 InlineMessage 类型。 */
    Q_PROPERTY(QString resultCategoryKey READ resultCategoryKey NOTIFY resultChanged)
    /*! 可选引导动作：空 / refresh。 */
    Q_PROPERTY(QString resultActionKey READ resultActionKey NOTIFY resultChanged)

    /* --- 拉取进度 --- */
    Q_PROPERTY(bool pulling READ pulling NOTIFY pullChanged)
    Q_PROPERTY(QString pullReference READ pullReference NOTIFY pullChanged)
    Q_PROPERTY(QString pullStatusText READ pullStatusText NOTIFY pullChanged)
    /*! 0.0~1.0；未知时为 -1（进度条走不确定态）。 */
    Q_PROPERTY(double pullProgress READ pullProgress NOTIFY pullChanged)
    Q_PROPERTY(bool pullProgressKnown READ pullProgressKnown NOTIFY pullChanged)
    Q_PROPERTY(int pullCompletedLayers READ pullCompletedLayers NOTIFY pullChanged)
    Q_PROPERTY(int pullTotalLayers READ pullTotalLayers NOTIFY pullChanged)

    /* --- 权限门 --- */
    Q_PROPERTY(bool writeAllowed READ writeAllowed NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessKey READ writeAccessKey NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessText READ writeAccessText NOTIFY writeAccessChanged)

public:
    explicit OperationController(DockerBackendInterface *backend, QObject *parent = nullptr);

    bool busy() const;
    int activeCount() const;

    QString resultKey() const;
    QString resultText() const;
    QString resultDetailText() const;
    QString resultCategoryKey() const;
    QString resultActionKey() const;

    bool pulling() const;
    QString pullReference() const;
    QString pullStatusText() const;
    double pullProgress() const;
    bool pullProgressKnown() const;
    int pullCompletedLayers() const;
    int pullTotalLayers() const;

    bool writeAllowed() const;
    QString writeAccessKey() const;
    QString writeAccessText() const;

    /*! 某个目标（`container:<id>` / `image:<ref>`）是否有操作在途。 */
    Q_INVOKABLE bool isTargetBusy(const QString &targetKey) const;

    Q_INVOKABLE void startContainer(const QString &id);
    Q_INVOKABLE void stopContainer(const QString &id);
    Q_INVOKABLE void restartContainer(const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id);
    Q_INVOKABLE void pullImage(const QString &reference);
    Q_INVOKABLE void cancelPull();
    Q_INVOKABLE void removeImage(const QString &id, bool force);

    /*! 关掉结果提示（用户已读）。 */
    Q_INVOKABLE void dismissResult();

    /*! 引用校验与归一化（QML 在提交前调用，非法输入不发往引擎）。 */
    Q_INVOKABLE bool isValidImageReference(const QString &reference) const;
    Q_INVOKABLE QString normalizedImageReference(const QString &reference) const;

    /*! 权限可能变化（例如刚被加入 socket 所属组），允许显式重算。 */
    Q_INVOKABLE void refreshWriteAccess();

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void pullChanged();
    void writeAccessChanged();
    /*! 容器已删除：详情页据此返回列表。 */
    void containerRemoved(const QString &id);
    /*! 容器状态可能已变（启动 / 停止 / 重启成功）：详情页据此静默重读。 */
    void containerStateChanged(const QString &id);
    /*! 镜像已删除。 */
    void imageRemoved(const QString &id);

private:
    enum class Result {
        None,
        Success,
        /*! 引擎返回 304：已经处于目标状态。 */
        Unchanged,
        Error,
        Cancelled,
    };

    using Mutation = DockerBackendInterface::Mutation;
    using MutationOutcome = DockerBackendInterface::MutationOutcome;

    /*! 统一的准入检查：权限、忙碌。返回 false 表示这次调用被拒绝（已给出结果文案）。 */
    bool admit(const QString &targetKey, const QString &what);
    /*! 当前生效的写权限（降级后以降级结果为准）。 */
    WriteAccess effectiveWriteAccess() const;
    void beginOperation(Mutation mutation, const QString &targetKey);
    void onMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);
    void onPullProgress(const ImagePullProgress &progress);
    void onBackendMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);

    void setResult(Result result, const QString &text, const QString &detail = QString(), const DockerError &error = DockerError());
    void setWriteAccess(WriteAccess access, bool degraded);
    /*! 403 / EACCES：本次会话降级为只读，且不可逆。 */
    void degradeToReadOnly(const DockerError &error);

    void refreshAfter(Mutation mutation, const QString &targetKey);
    static QString successText(Mutation mutation, const QString &targetKey);
    static QString unchangedText(Mutation mutation);

    DockerBackendInterface *m_backend = nullptr;

    QSet<QString> m_busyTargets;
    Result m_result = Result::None;
    QString m_resultText;
    QString m_resultDetailText;
    QString m_resultCategoryKey = QStringLiteral("none");
    QString m_resultActionKey;

    ImagePullProgress m_pullProgress;
    bool m_pulling = false;

    WriteAccess m_writeAccess = WriteAccess::Allowed;
    bool m_writeDegraded = false;
    WriteAccess m_degradedAccess = WriteAccess::SocketNotWritable;
};

} // namespace Kontainer
