/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/service_control.h"
#include "backend/service_status.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * 服务管理的提权边界（ARCH_V5_V8 §B1）。
 *
 * 这是**唯一新增的提权面**，因此用例的重点全在"拒绝"上：
 * 只有三个白名单 unit × 五个固定动词能通过，其它一律给稳定错误 key，
 * 而且会话侧与 helper 侧用的是同一个校验函数（纵深防御）。
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
    // 白名单之外一律不是（包括看起来很像的）
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
    // 3 × 5 = 15 组，一个不多一个不少
    QCOMPARE(managedServiceUnits().size() * managedServiceVerbs().size(), 15);
}

void ServiceControlTest::rejectsEverythingElse()
{
    // 空值
    QCOMPARE(serviceControlArgumentError(QString(), QStringLiteral("start")), QStringLiteral("unitRequired"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service"), QString()), QStringLiteral("verbRequired"));

    // 白名单之外的 unit：即使动词合法也拒绝（这是提权边界上最要紧的一条）
    QCOMPARE(serviceControlArgumentError(QStringLiteral("sshd.service"), QStringLiteral("start")),
             QStringLiteral("unitNotManaged"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service; reboot"), QStringLiteral("stop")),
             QStringLiteral("unitNotManaged"));
    QCOMPARE(serviceControlArgumentError(QStringLiteral("../etc/passwd"), QStringLiteral("start")),
             QStringLiteral("unitNotManaged"));

    // 不认识的动词：包括 systemctl 的其它子命令与大小写变体
    for (const QString &verb : {QStringLiteral("mask"), QStringLiteral("kill"), QStringLiteral("Start"),
                                QStringLiteral("daemon-reload"), QStringLiteral("start ")}) {
        QCOMPARE(serviceControlArgumentError(QStringLiteral("docker.service"), verb), QStringLiteral("verbNotManaged"));
    }
}

void ServiceControlTest::mapsVerbsToActionsAndSlots()
{
    // 动作名与槽名必须一一对应（.actions 的注释里写了规则，改名字要同步）
    QCOMPARE(serviceActionName(ServiceVerb::Start), QStringLiteral("org.kde.kcm.docker.service.start"));
    QCOMPARE(serviceActionName(ServiceVerb::Disable), QStringLiteral("org.kde.kcm.docker.service.disable"));
    QCOMPARE(serviceHelperSlot(ServiceVerb::Start), QStringLiteral("service_start"));
    QCOMPARE(serviceHelperSlot(ServiceVerb::Disable), QStringLiteral("service_disable"));

    // key ↔ 枚举是双向一致的
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
