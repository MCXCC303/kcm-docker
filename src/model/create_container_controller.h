/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/container_create_request.h"
#include "model/mount_preset_store.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace Kontainer
{

class ContainerDetailController;
class OperationController;

/*!
 * 创建容器向导的状态与校验（ARCH_V5_V8 §4.3/§4.4）。
 *
 * 为什么把表单状态放在 C++ 而不是 QML：七期的校验矩阵里有一半要**对照后端数据**
 * （重名、端口冲突、镜像是否在本地），另一半是纯格式规则；两者放在一起才可能被
 * 单元测试完整覆盖。QML 只做两件事：把输入写进来、把 `summary()` 画出来。
 *
 * 步骤用**稳定 key**（`image` / `basics` / `ports` / `environment` / `mounts` /
 * `resources` / `summary`）而不是下标：界面上的步骤按钮、校验与测试都按 key 说话，
 * 以后插入或调整步骤顺序不会静默错位（六期在标签页索引上踩过这个坑）。
 */
class CreateContainerController : public QObject
{
    Q_OBJECT

    /* ---------------- 步骤 ---------------- */
    Q_PROPERTY(QString stepKey READ stepKey NOTIFY stepChanged)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    /*! 当前步骤能不能继续（不能时 `stepErrorKey()` 给出原因）。 */
    Q_PROPERTY(bool canAdvance READ canAdvance NOTIFY changed)
    /*! 当前步骤的问题 key（空 = 没问题）。 */
    Q_PROPERTY(QString stepErrorKey READ stepErrorKey NOTIFY changed)
    /*! 是否停在最后一步（确认总览）。 */
    Q_PROPERTY(bool onSummary READ onSummary NOTIFY stepChanged)

    /* ---------------- 表单字段 ---------------- */
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString image READ image WRITE setImage NOTIFY changed)
    Q_PROPERTY(QString commandText READ commandText WRITE setCommandText NOTIFY changed)
    Q_PROPERTY(QString entrypointText READ entrypointText WRITE setEntrypointText NOTIFY changed)
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY changed)
    Q_PROPERTY(QString user READ user WRITE setUser NOTIFY changed)
    Q_PROPERTY(QString hostname READ hostname WRITE setHostname NOTIFY changed)
    Q_PROPERTY(QString network READ network WRITE setNetwork NOTIFY changed)
    Q_PROPERTY(QString networkAliasesText READ networkAliasesText WRITE setNetworkAliasesText NOTIFY changed)
    Q_PROPERTY(QString restartPolicy READ restartPolicy WRITE setRestartPolicy NOTIFY changed)
    Q_PROPERTY(int restartMaxRetries READ restartMaxRetries WRITE setRestartMaxRetries NOTIFY changed)
    Q_PROPERTY(qint64 memoryLimitBytes READ memoryLimitBytes WRITE setMemoryLimitBytes NOTIFY changed)
    Q_PROPERTY(double cpus READ cpus WRITE setCpus NOTIFY changed)
    Q_PROPERTY(bool privileged READ privileged WRITE setPrivileged NOTIFY changed)
    Q_PROPERTY(bool startAfterCreate READ startAfterCreate WRITE setStartAfterCreate NOTIFY changed)
    /*! 镜像不在本地时是否允许继续（界面上的「先拉取」）。 */
    Q_PROPERTY(bool pullIfMissing READ pullIfMissing WRITE setPullIfMissing NOTIFY changed)

    /*! 端口 / 环境变量 / 标签 / 挂载：QML 的行编辑器写进来的纯数据。 */
    Q_PROPERTY(QVariantList portRows READ portRows WRITE setPortRows NOTIFY changed)
    Q_PROPERTY(QVariantList environmentRows READ environmentRows WRITE setEnvironmentRows NOTIFY changed)
    Q_PROPERTY(QVariantList labelRows READ labelRows WRITE setLabelRows NOTIFY changed)
    Q_PROPERTY(QVariantList mountRows READ mountRows WRITE setMountRows NOTIFY changed)

    /*! 挂载预设（摘要形式，界面据此铺"快速添加"列表）。 */
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    /*!
     * 选择列表：本地镜像与可用网络。
     *
     * 做成**属性**而不是 Q_INVOKABLE：函数调用既不会建立依赖，又会随着每次表单改动
     * 让 Repeater 重建全部条目（实测会崩）。它们只在后端数据变化时才通知。
     */
    Q_PROPERTY(QVariantList availableImages READ availableImages NOTIFY choiceListsChanged)
    Q_PROPERTY(QVariantList availableNetworks READ availableNetworks NOTIFY choiceListsChanged)

    /*! 确认总览：`[{label, value}]`，最后一步只读展示。 */
    Q_PROPERTY(QVariantList summary READ summary NOTIFY changed)

public:
    CreateContainerController(OperationController *operations,
                              MountPresetStore *presets,
                              DockerBackendInterface *backend,
                              ContainerDetailController *containerDetail = nullptr,
                              QObject *parent = nullptr);

    /*! 步骤 key 顺序（界面与测试共用；不要在 QML 里另抄一份）。 */
    static QStringList stepKeys();
    /*! 同上，但以属性形式暴露给 QML（静态方法在 QML 里拿不到）。 */
    Q_PROPERTY(QStringList stepKeys READ stepKeys CONSTANT)

    QString stepKey() const;
    int stepIndex() const;
    int stepCount() const;
    bool canAdvance() const;
    QString stepErrorKey() const;
    bool onSummary() const;

    QString name() const;
    QString image() const;
    QString commandText() const;
    QString entrypointText() const;
    QString workingDirectory() const;
    QString user() const;
    QString hostname() const;
    QString network() const;
    QString networkAliasesText() const;
    QString restartPolicy() const;
    int restartMaxRetries() const;
    qint64 memoryLimitBytes() const;
    double cpus() const;
    bool privileged() const;
    bool startAfterCreate() const;
    bool pullIfMissing() const;

    QVariantList portRows() const;
    QVariantList environmentRows() const;
    QVariantList labelRows() const;
    QVariantList mountRows() const;
    QVariantList presets() const;
    QVariantList summary() const;

    void setName(const QString &value);
    void setImage(const QString &value);
    void setCommandText(const QString &value);
    void setEntrypointText(const QString &value);
    void setWorkingDirectory(const QString &value);
    void setUser(const QString &value);
    void setHostname(const QString &value);
    void setNetwork(const QString &value);
    void setNetworkAliasesText(const QString &value);
    void setRestartPolicy(const QString &value);
    void setRestartMaxRetries(int value);
    void setMemoryLimitBytes(qint64 value);
    void setCpus(double value);
    void setPrivileged(bool value);
    void setStartAfterCreate(bool value);
    void setPullIfMissing(bool value);
    void setPortRows(const QVariantList &rows);
    void setEnvironmentRows(const QVariantList &rows);
    void setLabelRows(const QVariantList &rows);
    void setMountRows(const QVariantList &rows);

    /*! 从空白开始（可选预填镜像，供镜像卡片/详情进入时使用）。 */
    Q_INVOKABLE void reset(const QString &presetImage = {});
    /*! 克隆：用现有容器的**配置**预填（不复制运行时状态，§4.5）。 */
    Q_INVOKABLE bool prefillFromContainer(const QString &containerId);
    /*! 名称冲突时给一个可用的候选名（`web` → `web-copy`）。 */
    Q_INVOKABLE QString suggestedName() const;

    /*! 指定步骤的校验结果（诊断与测试用；不影响当前步骤）。 */
    Q_INVOKABLE QString stepErrorKeyForStep(const QString &key) const;

    /*! 上一步 / 下一步（下一步会先校验当前步骤）。 */
    Q_INVOKABLE bool nextStep();
    Q_INVOKABLE void previousStep();
    /*! 跳到某一步（步骤按钮用；只允许跳到已通过校验的那一步或它之前）。 */
    Q_INVOKABLE bool goToStep(const QString &key);

    QVariantList availableImages() const;
    QVariantList availableNetworks() const;

    /*!
     * 端口行的增删改（八期后的修正：行编辑放在 C++）。
     *
     * 为什么不让 QML 直接改列表：`Repeater` 的 delegate 在
     * `pragma ComponentBehavior: Unbound` 下**拿不到根对象的 id**，delegate 里
     * 写 `page.xxx` 会抛 `ReferenceError`，用户实测表现为"删不掉端口/加不了挂载"。
     * 把行的增删改收进控制器后，delegate 只需要一个非根 id 就能调用，规则也更好测。
     */
    Q_INVOKABLE void addPortRow(int containerPort = 80, int hostPort = 0, const QString &hostIp = {}, const QString &protocol = QStringLiteral("tcp"));
    Q_INVOKABLE void setPortRow(int row, const QString &field, const QVariant &value);
    Q_INVOKABLE void removePortRow(int row);
    Q_INVOKABLE void clearPortRows();

    Q_INVOKABLE void addMountRow(const QString &type = QStringLiteral("bind"),
                                 const QString &source = {},
                                 const QString &destination = {},
                                 bool readOnly = false);
    Q_INVOKABLE void setMountRow(int row, const QString &field, const QVariant &value);
    Q_INVOKABLE void removeMountRow(int row);

    /*! 从预设添加一条挂载（已存在则忽略）。 */
    Q_INVOKABLE bool addMountFromPreset(const QString &presetId);
    /*! 追加一条空挂载行（bind）。 */
    Q_INVOKABLE void addEmptyMount();
    Q_INVOKABLE void removeMountAt(int row);

    /*! 提交（最后一步）：构造请求交给 `OperationController::createContainer`。 */
    Q_INVOKABLE bool submit();

Q_SIGNALS:
    void changed();
    void stepChanged();
    void presetsChanged();
    /*! 镜像或网络列表变了（选择列表要重铺）。 */
    void choiceListsChanged();
    /*! 提交成功（界面据此跳到新容器详情页）。 */
    void submitted(const QString &containerId);

private:
    /*! 当前步骤的校验（返回稳定 key；空 = 通过）。 */
    QString validateCurrentStep() const;
    /*! 每一行端口/挂载的字段级校验（`stepErrorKey` 用的同一批规则）。 */
    QString validatePorts() const;
    QString validateMounts() const;
    QVariantMap requestMap() const;
    /*! 默认网络：列表里的第一个（列表是异步到的，因此这里每次现算）。 */
    QString defaultNetwork() const;
    QStringList splitLines(const QString &text) const;
    void touch();

    OperationController *m_operations = nullptr;
    MountPresetStore *m_presets = nullptr;
    DockerBackendInterface *m_backend = nullptr;
    /*! 容器详情控制器（可能为空）：克隆时用它拿命令/入口点/环境/标签等完整配置。 */
    ContainerDetailController *m_containerDetail = nullptr;

    int m_stepIndex = 0;

    QString m_name;
    QString m_image;
    QString m_commandText;
    QString m_entrypointText;
    QString m_workingDirectory;
    QString m_user;
    QString m_hostname;
    QString m_network;
    QString m_networkAliasesText;
    QString m_restartPolicy = QStringLiteral("no");
    int m_restartMaxRetries = 0;
    qint64 m_memoryLimitBytes = 0;
    double m_cpus = 0.0;
    bool m_privileged = false;
    bool m_startAfterCreate = true;
    bool m_pullIfMissing = false;

    QVariantList m_portRows;
    QVariantList m_environmentRows;
    QVariantList m_labelRows;
    QVariantList m_mountRows;

    /*! 提交后引擎返回的 id（`containerCreated` 信号里拿到）。 */
    QString m_createdContainerId;
};

} // namespace Kontainer
