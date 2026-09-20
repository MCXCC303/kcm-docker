/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/service_control.h"
#include "backend/service_status.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * Privilege boundary of service management (ARCH_V5_V8 §B1).
 *
 * This is the **only new privilege surface**, so the tests focus on rejection:
 * only 3 whitelisted units × 5 fixed verbs pass, everything else gets a stable
 * error key, and session and helper share one validation function (defense in depth).
 */
class ServiceControlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void whitelistCoversExactlyThreeUnits();
    void acceptsOnlyTheManagedCombinations();
    void rejectsEverythingElse();
    void mapsVerbsToActionsAndSlots();
};

void ServiceControlTest::whitelistCoversExactlyThreeUnits()
{
    const QStringList units = managedServiceUnits();
    QCOMPARE(units, QStringList({QStringLiteral("docker.socket"),
                                 QStringLiteral("docker.service"),
                                 QStringLiteral("containerd.service")}));
    for (const QString &unit : units) {
        QVERIFY(isManagedServiceUnit(unit));
    }
    // Nothing else is managed, including look-alike names
    QVERIFY(!isManagedServiceUnit(QStringLiteral("sshd.service")));
    QVERIFY(!isManagedServiceUnit(QStringLiteral("docker")));
    QVERIFY(!isManagedServiceUnit(QStringLiteral("docker.service ")));
    QVERIFY(!isManagedServiceUnit(QString()));
}

void ServiceControlTest::acceptsOnlyTheManagedCombinations()
{
    for (const QString &unit : managedServiceUnits()) {
        for (const QString &verb : managedServiceVerbs()) {
            QVERIFY2(serviceControlArgumentError(unit, verb).isEmpty(),
                     qPrintable(QStringLiteral("%1 + %2 must be accepted").arg(unit, verb)));
        }
    }
    // Exactly 3 × 5 = 15 combinations
    QCOMPARE(managedServiceUnits().size() * managedServiceVerbs().size(), 15);
}

void ServiceControlTest::rejectsEverythingElse()
{
    // Empty values
    QCOMPARE(serviceControlArgumentError(QString(), QStringLiteral("start")), QStringLiteral("unitRequired"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service"), QString()), QStringLiteral("verbRequired"));

    // Unit outside the whitelist: rejected even with a valid verb (the critical privilege-boundary rule)
    QCOMPARE(serviceControlArgumentError(QStringLiteral("sshd.service"), QStringLiteral("start")),
             QStringLiteral("unitNotManaged"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service; reboot"), QStringLiteral("stop")),
             QStringLiteral("unitNotManaged"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("../etc/passwd"), QStringLiteral("start")),
             QStringLiteral("unitNotManaged"));

    // Unknown verbs: other systemctl subcommands and case variants
    for (const QString &verb : {QStringLiteral("mask"), QStringLiteral("kill"), QStringLiteral("Start"),
                                QStringLiteral("daemon-reload"), QStringLiteral("start ")}) {
        QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service"), verb), QStringLiteral("verbNotManaged"));
    }
}

void ServiceControlTest::mapsVerbsToActionsAndSlots()
{
    // Action and slot names must match one-to-one (rules live in .actions comments; rename both together)
    QCOMPARE(serviceActionName(ServiceVerb::Start), QStringLiteral("org.kde.kcm.docker.service.start"));
    QCOMPARE(serviceActionName(ServiceVerb::Disable), QStringLiteral("org.kde.kcm.docker.service.disable"));
    QCOMPARE(serviceHelperSlot(ServiceVerb::Start), QStringLiteral("service_start"));
    QCOMPARE(serviceHelperSlot(ServiceVerb::Disable), QStringLiteral("service_disable"));

    // key ↔ enum must round-trip
    for (const QString &key : managedServiceVerbs()) {
        ServiceVerb verb = ServiceVerb::Start;
        QVERIFY(serviceVerbFromKey(key, &verb));
        QCOMPARE(serviceVerbKey(verb), key);
    }
    ServiceVerb unused = ServiceVerb::Start;
    QVERIFY(!serviceVerbFromKey(QStringLiteral("mask"), &unused));
}

QTEST_MAIN(ServiceControlTest)

#include "tst_service_control.moc"
