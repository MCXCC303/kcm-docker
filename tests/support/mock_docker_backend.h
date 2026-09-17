/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"

#include <QHash>
#include <QSet>

namespace Kontainer
{

/*!
 * 测试用的 backend（ARCH_V1 §30）。
 *
 * 让 model / controller 的测试不依赖真实 Docker daemon：
 *  - 测试设置“将要读到”的数据
 *  - refresh*() 只记录请求并进入 loading，由测试调用 completeRefresh() 决定何时完成
 *  - 可以为某个 dataset 注入失败，用来验证错误隔离
 */
class MockDockerBackend : public DockerBackendInterface
{
    Q_OBJECT

public:
    explicit MockDockerBackend(QObject *parent = nullptr);

    void setEngineInfo(const EngineInfo &info);
    void setContainers(const QList<Container> &containers);
    void setImages(const QList<Image> &images);
    void setStorageUsage(const StorageUsage &usage);
    void setContainerDetail(const ContainerDetail &detail);
    void setImageDetail(const ImageDetail &detail);
    void setContainerStats(const ContainerStats &stats);
    void setEndpointName(const QString &name);
    /*! 权限门（DockerCapabilities）会读这个 endpoint；测试用它构造可写 / 不可写场景。 */
    void setEndpoint(const DockerEndpoint &endpoint);

    /*! 让指定 dataset 的下一次刷新失败。 */
    void setNextFailure(Section section, const DockerError &error);
    void clearFailures();

    /* --- 写操作（ARCH_V4 §2.2.4） --- */
    struct MutationCall {
        Mutation mutation = Mutation::StartContainer;
        QString targetKey;
        /*! 删除镜像时的 force 标记。 */
        bool force = false;
    };
    /*! 已发出但还没结束的写操作。 */
    QList<MutationCall> mutationCalls() const
    {
        return m_mutationCalls;
    }
    int mutationCount(Mutation mutation) const;
    QString lastMutationTarget(Mutation mutation) const;
    /*! 被请求取消的引用（按引用取消，ARCH_V4 §2.4）。 */
    QStringList cancelledPulls() const
    {
        return m_cancelledPulls;
    }
    int cancelAllCount() const
    {
        return m_cancelAllCount;
    }
    /*! 结束全部在途写操作（默认成功）；可指定结果与错误。 */
    void completeMutations(MutationOutcome outcome = MutationOutcome::Succeeded, const DockerError &error = DockerError());
    /*! 只结束某一个目标的操作（用于「一路拉取结束、另一路继续」这类场景）。 */
    void completeMutation(const QString &targetKey, MutationOutcome outcome, const DockerError &error = DockerError());
    /*! 模拟引擎推送一条拉取进度。 */
    void emitPullProgress(const ImagePullProgress &progress);

    /*! 结束当前这一轮刷新：发出 *Updated / sectionFailed / loadingChanged。 */
    void completeRefresh();

    int refreshCount(Section section) const;
    /*! 仍在采样 stats 的容器 id（验证详情页生命周期，§27）。 */
    QSet<QString> samplingIds() const
    {
        return m_statsWanted;
    }

    // DockerBackendInterface
    void refreshEngine() override;
    void refreshContainers() override;
    void refreshImages() override;
    void refreshStorageUsage() override;
    void inspectContainer(const QString &id) override;
    void inspectImage(const QString &id) override;
    void requestContainerStats(const QString &id) override;
    void stopContainerStats(const QString &id) override;

    DockerEndpoint endpoint() const override;
    void startContainer(const QString &id) override;
    void stopContainer(const QString &id) override;
    void restartContainer(const QString &id) override;
    void removeContainer(const QString &id) override;
    void pullImage(const QString &reference, const Kontainer::RegistryCredential &credential = {}) override;
    void cancelImagePull(const QString &reference) override;
    void cancelAllImagePulls() override;
    void removeImage(const QString &id, bool force) override;
    void checkRegistryAuth(const QString &serverAddress, const Kontainer::RegistryCredential &credential) override;

    /* --- 认证校验的注入与观察（ARCH_V5_V8 §2.6） --- */

    /*! 下线一次校验的结果（默认成功）。 */
    void setAuthCheckResult(AuthCheckResult result, const QString &detail = {});
    /*!
     * 是否延迟回应校验（默认 false = 立即回应）。
     *
     * 只有需要观察"校验进行中"的用例才打开它，否则每个用例都要记得补一次
     * `completeAuthCheck()`，很容易写出假通过的测试。
     */
    void setAuthCheckDeferred(bool deferred)
    {
        m_authCheckDeferred = deferred;
    }
    /*! 把已排队的校验结果发出去（模拟引擎在那之后才回应）。 */
    void completeAuthCheck();
    QString lastAuthServerAddress() const
    {
        return m_lastAuthServerAddress;
    }
    RegistryCredential lastAuthCredential() const
    {
        return m_lastAuthCredential;
    }
    int authCheckCount() const
    {
        return m_authCheckCount;
    }
    /*! 最近一次拉取带上的凭据（断言"钱包里的凭据真的传到了拉取路径"）。 */
    RegistryCredential lastPullCredential() const
    {
        return m_lastPullCredential;
    }
    bool isLoading() const override;
    bool isRefreshingFastData() const override;
    QString endpointDisplayName() const override;
    EngineInfo engineInfo() const override;
    QList<Container> containers() const override;
    QList<Image> images() const override;
    StorageUsage storageUsage() const override;
    ContainerDetail containerDetail() const override;
    ImageDetail imageDetail() const override;
    ContainerStats containerStats() const override;
    bool isSamplingStats(const QString &id) const override;

private:
    void beginRefresh(Section section);

    EngineInfo m_engine;
    QList<Container> m_containers;
    QList<Image> m_images;
    StorageUsage m_storageUsage;
    ContainerDetail m_containerDetail;
    ImageDetail m_imageDetail;
    ContainerStats m_containerStats;
    QString m_endpointName = QStringLiteral("unix:///mock/docker.sock");
    QSet<QString> m_statsWanted;
    AuthCheckResult m_authCheckResult = AuthCheckResult::Succeeded;
    QString m_authCheckDetail;
    bool m_authCheckPending = false;
    bool m_authCheckDeferred = false;
    QString m_lastAuthServerAddress;
    RegistryCredential m_lastAuthCredential;
    int m_authCheckCount = 0;
    RegistryCredential m_lastPullCredential;

    bool m_loading = false;
    QHash<int, bool> m_pending;
    QHash<int, DockerError> m_failures;
    QHash<int, int> m_refreshCounts;

    DockerEndpoint m_endpoint;
    QList<MutationCall> m_mutationCalls;
    QStringList m_cancelledPulls;
    int m_cancelAllCount = 0;
};

} // namespace Kontainer
