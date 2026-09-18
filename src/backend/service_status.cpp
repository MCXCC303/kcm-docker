/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/service_status.h"

#include "logging.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <QVariantMap>

namespace Kontainer
{

namespace
{
constexpr auto kSystemdService = "org.freedesktop.systemd1";
constexpr auto kSystemdPath = "/org/freedesktop/systemd1";
constexpr auto kSystemdManager = "org.freedesktop.systemd1.Manager";
constexpr auto kSystemdUnit = "org.freedesktop.systemd1.Unit";
constexpr auto kSystemdUnitPathPrefix = "/org/freedesktop/systemd1/unit/";
} // namespace

QStringList managedServiceUnits()
{
    return {QStringLiteral("docker.socket"), QStringLiteral("docker.service"), QStringLiteral("containerd.service")};
}

QString ServiceState::stateKey() const
{
    if (activeState.isEmpty()) {
        return QStringLiteral("unknown");
    }
    if (activeState == QLatin1String("active")) {
        return QStringLiteral("running");
    }
    if (activeState == QLatin1String("failed")) {
        return QStringLiteral("failed");
    }
    if (activeState == QLatin1String("activating") || activeState == QLatin1String("reloading")) {
        return QStringLiteral("starting");
    }
    if (activeState == QLatin1String("deactivating")) {
        return QStringLiteral("stopping");
    }
    return QStringLiteral("stopped"); // inactive
}

ServiceStatusBackend::ServiceStatusBackend(QObject *parent)
    : QObject(parent)
{
}

ServiceStatusBackend::~ServiceStatusBackend() = default;

QVariantList ServiceStatusBackend::servicesVariant() const
{
    QVariantList result;
    const QList<ServiceState> list = services();
    result.reserve(list.size());
    for (const ServiceState &state : list) {
        result.append(QVariantMap {
            {QStringLiteral("unit"), state.unit},
            {QStringLiteral("stateKey"), state.stateKey()},
            {QStringLiteral("activeState"), state.activeState},
            {QStringLiteral("unitFileState"), state.unitFileState},
            {QStringLiteral("subState"), state.subState},
            {QStringLiteral("active"), state.isActive()},
            {QStringLiteral("enabled"), state.isEnabled()},
            {QStringLiteral("known"), state.known()},
        });
    }
    return result;
}

QString ServiceStatusBackend::stateKeyFor(const QString &unit) const
{
    const QList<ServiceState> list = services();
    for (const ServiceState &state : list) {
        if (state.unit == unit) {
            return state.stateKey();
        }
    }
    return QStringLiteral("unknown");
}

bool ServiceStatusBackend::isActive(const QString &unit) const
{
    const QList<ServiceState> list = services();
    for (const ServiceState &state : list) {
        if (state.unit == unit) {
            return state.isActive();
        }
    }
    return false;
}

SystemdServiceStatus::SystemdServiceStatus(QObject *parent)
    : ServiceStatusBackend(parent)
{
}

void SystemdServiceStatus::query()
{
    QList<ServiceState> result;
    const QDBusConnection bus = QDBusConnection::systemBus();
    QDBusInterface manager(QString::fromLatin1(kSystemdService), QString::fromLatin1(kSystemdPath), QString::fromLatin1(kSystemdManager), bus);

    for (const QString &unit : managedServiceUnits()) {
        ServiceState state;
        state.unit = unit;

        if (!bus.isConnected() || !manager.isValid()) {
            // systemd 不在（容器里、非 systemd 发行版）：如实给 unknown，界面据此不显示误导性的状态
            result.append(state);
            continue;
        }

        // Unit 的 D-Bus 路径由 systemd 自己转义（`docker.service` → `docker_2eservice`：
        // 非字母数字的字符转成 `_` + 两位十六进制）。这里只处理我们白名单里的名字，
        // 因此按同一规则逐字符转义即可（不追求覆盖任意 unit 名）。
        QString escaped;
        for (const QChar &character : std::as_const(unit)) {
            if (character.isLetterOrNumber()) {
                escaped += character;
            } else {
                escaped += QStringLiteral("_%1").arg(int(character.unicode()), 2, 16, QLatin1Char('0'));
            }
        }
        const QString path = QString::fromLatin1(kSystemdUnitPathPrefix) + escaped;

        QDBusInterface unitInterface(QString::fromLatin1(kSystemdService), path, QString::fromLatin1(kSystemdUnit), bus);
        if (!unitInterface.isValid()) {
            result.append(state);
            continue;
        }
        state.activeState = unitInterface.property("ActiveState").toString();
        state.unitFileState = unitInterface.property("UnitFileState").toString();
        state.subState = unitInterface.property("SubState").toString();
        result.append(state);
    }

    if (result == m_services) {
        return; // 状态没变：不发信号（避免无意义的重绘）
    }
    m_services = result;
    Q_EMIT servicesChanged();
}

QList<ServiceState> SystemdServiceStatus::services() const
{
    return m_services;
}

} // namespace Kontainer
