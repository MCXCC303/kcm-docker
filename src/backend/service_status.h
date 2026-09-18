/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 我们管理的三个 systemd unit（ARCH_V5_V8 §B1）。
 *
 * **固定白名单**：界面只显示这三个，提权动作也只接受这三个（不接受任意 unit 名）。
 * `docker.socket` 提供 socket 激活，`docker.service` 是守护进程本体，
 * `containerd.service` 提供一部分底层能力（容器/镜像的部分查看）。
 */
QStringList managedServiceUnits();

/*! 一个 unit 的状态快照。 */
struct ServiceState {
    /*! unit 名（`docker.service`）。 */
    QString unit;
    /*! systemd 的 `ActiveState`：active / inactive / failed / activating / deactivating。 */
    QString activeState;
    /*! `UnitFileState`：enabled / disabled / static / masked。 */
    QString unitFileState;
    /*! `SubState`：running / dead / failed / listening …（按数据显示，不做翻译）。 */
    QString subState;

    /*! 稳定 key（界面文案由 QML 决定）：`running` / `stopped` / `failed` / `starting` / `unknown`。 */
    QString stateKey() const;
    bool isActive() const
    {
        return activeState == QLatin1String("active");
    }
    bool isEnabled() const
    {
        return unitFileState == QLatin1String("enabled");
    }
    /*! 是否真的查到了（systemd 不在或 unit 不存在时为空）。 */
    bool known() const
    {
        return !activeState.isEmpty();
    }

    friend bool operator==(const ServiceState &lhs, const ServiceState &rhs)
    {
        return lhs.unit == rhs.unit && lhs.activeState == rhs.activeState && lhs.unitFileState == rhs.unitFileState
            && lhs.subState == rhs.subState;
    }
};

/*!
 * 服务状态来源。
 *
 * 做成接口是为了：① 测试注入替身（不能假设测试机上有 systemd 或这三个 unit）；
 * ② 未来换成 systemd 之外的实现（例如容器里跑的时候）。
 */
class ServiceStatusBackend : public QObject
{
    Q_OBJECT

    /*! 三个 unit 的状态（顺序同 `managedServiceUnits()`）。 */
    Q_PROPERTY(QVariantList services READ servicesVariant NOTIFY servicesChanged)

public:
    explicit ServiceStatusBackend(QObject *parent = nullptr);
    ~ServiceStatusBackend() override;

    /*! 查询一次（异步）：结果经 `servicesChanged` 通知。 */
    virtual void query() = 0;
    virtual QList<ServiceState> services() const = 0;
    QVariantList servicesVariant() const;

    /*! 按 unit 名取状态 key（查不到给 `unknown`）。 */
    Q_INVOKABLE QString stateKeyFor(const QString &unit) const;
    /*! 该 unit 是否处于 active。 */
    Q_INVOKABLE bool isActive(const QString &unit) const;

Q_SIGNALS:
    void servicesChanged();
};

/*!
 * systemd 实现：通过 systemd 的 D-Bus 接口**只读**查询属性。
 *
 * 只读：这里不做任何 start/stop/enable/disable——那些动作走受限提权 helper（固定 unit + 固定动词）。
 */
class SystemdServiceStatus : public ServiceStatusBackend
{
    Q_OBJECT

public:
    explicit SystemdServiceStatus(QObject *parent = nullptr);

    void query() override;
    QList<ServiceState> services() const override;

private:
    QList<ServiceState> m_services;
};

} // namespace Kontainer
