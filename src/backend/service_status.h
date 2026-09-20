/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * The three systemd units we manage (ARCH_V5_V8 §B1).
 *
 * A **fixed whitelist**: the UI shows only these and privileged actions accept only these (never
 * arbitrary unit names). `docker.socket` provides socket activation, `docker.service` is the daemon
 * itself, `containerd.service` backs part of the low-level capabilities (some container/image views).
 */
QStringList managedServiceUnits();

/*! Snapshot of one unit's state. */
struct ServiceState {
    /*! Unit name (`docker.service`). */
    QString unit;
    /*! systemd's `ActiveState`: active / inactive / failed / activating / deactivating. */
    QString activeState;
    /*! `UnitFileState`: enabled / disabled / static / masked. */
    QString unitFileState;
    /*! `SubState`: running / dead / failed / listening … (shown raw, never translated). */
    QString subState;

    /*! Stable key (QML owns the wording): `running` / `stopped` / `failed` / `starting` / `unknown`. */
    QString stateKey() const;
    bool isActive() const
    {
        return activeState == QLatin1String("active");
    }
    bool isEnabled() const
    {
        return unitFileState == QLatin1String("enabled");
    }
    /*! Whether it was actually queried (empty when systemd or the unit is absent). */
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
 * Service status source.
 *
 * An interface so that: (1) tests can inject a stand-in (test machines may lack systemd or these
 * units), and (2) a non-systemd implementation can replace it later (e.g. in a container).
 */
class ServiceStatusBackend : public QObject
{
    Q_OBJECT

    /*! States of the three units (same order as `managedServiceUnits()`). */
    Q_PROPERTY(QVariantList services READ servicesVariant NOTIFY servicesChanged)

public:
    explicit ServiceStatusBackend(QObject *parent = nullptr);
    ~ServiceStatusBackend() override;

    /*! Query once (async): the result is announced via `servicesChanged`. */
    virtual void query() = 0;
    virtual QList<ServiceState> services() const = 0;
    QVariantList servicesVariant() const;

    /*! State key by unit name (`unknown` when not found). */
    Q_INVOKABLE QString stateKeyFor(const QString &unit) const;
    /*! Whether the unit is active. */
    Q_INVOKABLE bool isActive(const QString &unit) const;

Q_SIGNALS:
    void servicesChanged();
};

/*!
 * systemd implementation: **read-only** property queries over systemd's D-Bus interface.
 *
 * Read-only on purpose: no start/stop/enable/disable here — those go through the restricted
 * privileged helper (fixed units + fixed verbs).
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
