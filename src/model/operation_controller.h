/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/docker_capabilities.h"
#include "domain/image_pull_progress.h"
#include "model/image_pull_model.h"

#include <QObject>
#include <QSet>
#include <QString>

namespace Kontainer
{

class CredentialStore;

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
    /*!
     * 忙碌集合的修订号（每次集合变化 +1）。
     *
     * 存在的唯一理由：QML 不会追踪 `Q_INVOKABLE` 调用，像
     * `operations.isContainerBusy(id)` 这样的绑定不会因为忙碌状态变化而重新求值。
     * 界面需要在同一个绑定里读一次这个可通知属性来建立依赖（见 ContainerCard.qml）。
     */
    Q_PROPERTY(int stateRevision READ stateRevision NOTIFY stateChanged)

    /* --- 结果通道（唯一的操作结果呈现来源） --- */
    Q_PROPERTY(QString resultKey READ resultKey NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    /*! 引擎原文（数据，不翻译）；用于「为什么失败」的次要行。 */
    Q_PROPERTY(QString resultDetailText READ resultDetailText NOTIFY resultChanged)
    /*! none / userActionable / environment / unexpected —— QML 据此选 InlineMessage 类型。 */
    Q_PROPERTY(QString resultCategoryKey READ resultCategoryKey NOTIFY resultChanged)
    /*! 可选引导动作：空 / refresh。 */
    Q_PROPERTY(QString resultActionKey READ resultActionKey NOTIFY resultChanged)

    /* --- 拉取列表（可并发、可在后台继续，ARCH_V4 §2.4） --- */
    Q_PROPERTY(Kontainer::ImagePullModel *pulls READ pulls CONSTANT)
    Q_PROPERTY(bool pulling READ pulling NOTIFY pullListChanged)
    Q_PROPERTY(int activePullCount READ activePullCount NOTIFY pullListChanged)

    /* --- 权限门 --- */
    Q_PROPERTY(bool writeAllowed READ writeAllowed NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessKey READ writeAccessKey NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessText READ writeAccessText NOTIFY writeAccessChanged)

public:
    explicit OperationController(DockerBackendInterface *backend, QObject *parent = nullptr);

    bool busy() const;
    int activeCount() const;
    int stateRevision() const;

    QString resultKey() const;
    QString resultText() const;
    QString resultDetailText() const;
    QString resultCategoryKey() const;
    QString resultActionKey() const;

    ImagePullModel *pulls() const
    {
        return m_pulls;
    }
    /*! 是否至少有一路拉取在进行中（用于工具栏指示与对话框文案）。 */
    bool pulling() const;
    int activePullCount() const;

    bool writeAllowed() const;
    QString writeAccessKey() const;
    QString writeAccessText() const;

    /*! 某个目标（`container:<id>` / `image:<ref>`）是否有操作在途。 */
    Q_INVOKABLE bool isTargetBusy(const QString &targetKey) const;
    /*!
     * 某个目标是否有操作在途。
     *
     * 界面调这两个便捷方法而不是自己拼 `container:` / `image:` 前缀：
     * target key 的拼法只有一处定义（OperationTarget），拼错会让忙碌态静默失效。
     */
    Q_INVOKABLE bool isContainerBusy(const QString &id) const;
    Q_INVOKABLE bool isImageBusy(const QString &reference) const;

    Q_INVOKABLE void startContainer(const QString &id);
    Q_INVOKABLE void stopContainer(const QString &id);
    Q_INVOKABLE void restartContainer(const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id);
    Q_INVOKABLE void pullImage(const QString &reference);

    /*!
     * 凭据来源（可为空 = 只做匿名拉取）。
     *
     * 控制器只依赖 `CredentialStore` 的读取接口：钱夹不可用时它返回空凭据，
     * 拉取照旧按匿名进行（而不是失败）——私有仓库会得到引擎的 401，用户看得见原因。
     */
    void setCredentialStore(CredentialStore *store);
    /*! 取消某一项拉取（列表里的「取消」按钮）。 */
    Q_INVOKABLE void cancelPull(const QString &reference);
    /*! 取消全部在途拉取。 */
    Q_INVOKABLE void cancelAllPulls();
    /*! 从列表里移除一条已结束的记录（失败的记录会一直留着，直到用户处理）。 */
    Q_INVOKABLE void dismissPull(const QString &reference);
    /*! 清空所有已结束的记录。 */
    Q_INVOKABLE void clearFinishedPulls();
    Q_INVOKABLE void removeImage(const QString &id, bool force);

    /*!
     * 创建网络（ARCH_V5_V8 §3.3）。
     *
     * 校验在 C++ 侧统一做（名称规则、子网/网关格式、与现有网络重名），失败时给出稳定的
     * 错误 key；界面把错误显示在对话框里。**只创建 bridge**：驱动由界面固定传入。
     */
    Q_INVOKABLE bool createNetwork(const QString &name,
                                   const QString &subnet = {},
                                   const QString &gateway = {},
                                   bool internal = false,
                                   bool attachable = false,
                                   const QVariantList &labels = {});
    /*! 删除网络（内置网络会被 daemon 拒绝，界面不提供入口）。 */
    Q_INVOKABLE void removeNetwork(const QString &id, const QString &name = {});

    /*! 关掉结果提示（用户已读）。 */
    Q_INVOKABLE void dismissResult();

    /*! 引用校验与归一化（QML 在提交前调用，非法输入不发往引擎）。 */
    Q_INVOKABLE bool isValidImageReference(const QString &reference) const;
    Q_INVOKABLE QString normalizedImageReference(const QString &reference) const;
    /*! 镜像引用对应的仓库地址（拉取前提示"这个仓库还没登录"用）。 */
    Q_INVOKABLE QString serverAddressForImage(const QString &reference) const;

    /*! 权限可能变化（例如刚被加入 socket 所属组），允许显式重算。 */
    Q_INVOKABLE void refreshWriteAccess();

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void pullListChanged();
    void writeAccessChanged();
    /*! 容器已删除：详情页据此返回列表。 */
    void containerRemoved(const QString &id);
    /*! 容器状态可能已变（启动 / 停止 / 重启成功）：详情页据此静默重读。 */
    void containerStateChanged(const QString &id);
    /*! 镜像已删除。 */
    void imageRemoved(const QString &id);
    /*! 网络集合变化（创建 / 删除成功）：网络页与容器详情据此重读。 */
    void networksChanged();

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
    /*! 按引用找到拉取记录；不存在返回 nullptr。 */
    ImagePullEntry *findPull(const QString &reference);
    /*! 重排并写入模型：进行中的在前，已结束的按结束顺序倒序（最近的在最上面）。 */
    void publishPulls();
    void onBackendMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);

    void setResult(Result result, const QString &text, const QString &detail = QString(), const DockerError &error = DockerError());
    void setWriteAccess(WriteAccess access, bool degraded);
    /*! 403 / EACCES：本次会话降级为只读，且不可逆。 */
    void degradeToReadOnly(const DockerError &error);

    void refreshAfter(Mutation mutation, const QString &targetKey);
    static QString successText(Mutation mutation, const QString &targetKey);
    static QString unchangedText(Mutation mutation);
    /*! 失败文案：通用分类文案 + 与操作相关的可操作提示。 */
    static QString failureText(Mutation mutation, const DockerError &error);

    DockerBackendInterface *m_backend = nullptr;
    CredentialStore *m_credentialStore = nullptr;

    QSet<QString> m_busyTargets;
    int m_stateRevision = 0;
    Result m_result = Result::None;
    QString m_resultText;
    QString m_resultDetailText;
    QString m_resultCategoryKey = QStringLiteral("none");
    QString m_resultActionKey;

    ImagePullModel *m_pulls = nullptr;
    /*! 界面顺序（进行中 + 已结束），模型每次按它重建。 */
    QList<ImagePullEntry> m_pullEntries;

    WriteAccess m_writeAccess = WriteAccess::Allowed;
    bool m_writeDegraded = false;
    WriteAccess m_degradedAccess = WriteAccess::SocketNotWritable;
};

} // namespace Kontainer
