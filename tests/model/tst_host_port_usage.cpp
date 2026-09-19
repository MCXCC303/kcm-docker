/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_usage.h"
#include "model/port_binding_rules.h"

#include "support/mock_docker_backend.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * 宿主端口占用表（ARCH_next_ports.md §3，里程碑 M1）。
 *
 * 这是端口页、区间地图与创建表单冲突检测**共用**的一份实现，因此用例的重点是：
 *   - 只算跑着的容器（用户已确认：没运行的自然不占用）；
 *   - IPv4/IPv6 通配合并成一条（界面据此画双环）；
 *   - 地址重叠语义（通配与任何地址冲突；两个具体地址只有相同才冲突）；
 *   - 建议端口（`nextFreePort`）跳开已占用的端口与特权端口。
 */
class HostPortUsageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void bindingRulesAreSharedAndStrict();
    void parsesSinglePortsAndRanges();
    void collectsOnlyRunningContainers();
    void mergesDualStackWildcardBindings();
    void suggestsAPortOutsideTheUsedOnes();
};

void HostPortUsageTest::initTestCase()
{
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
}

/*! 共享的地址判定（原来有两份实现，这里守住语义）。 */
void HostPortUsageTest::bindingRulesAreSharedAndStrict()
{
    using namespace PortBindingRules;

    QVERIFY(isWildcardAddress(QString()));
    QVERIFY(isWildcardAddress(QStringLiteral("0.0.0.0")));
    QVERIFY(isWildcardAddress(QStringLiteral("::")));
    QVERIFY(isWildcardAddress(QStringLiteral("[::]")));
    QVERIFY(!isWildcardAddress(QStringLiteral("127.0.0.1")));

    // 通配与任何地址都冲突（含 IPv4 通配 vs IPv6 通配：Linux 默认互斥）
    QVERIFY(hostBindingsOverlap(QStringLiteral("0.0.0.0"), QStringLiteral("127.0.0.1")));
    QVERIFY(hostBindingsOverlap(QStringLiteral("0.0.0.0"), QStringLiteral("::")));
    QVERIFY(hostBindingsOverlap(QString(), QStringLiteral("192.168.1.5")));
    // 两个具体地址：只有相同才冲突
    QVERIFY(hostBindingsOverlap(QStringLiteral("127.0.0.1"), QStringLiteral("127.0.0.1")));
    QVERIFY2(!hostBindingsOverlap(QStringLiteral("127.0.0.1"), QStringLiteral("192.168.1.5")),
             "two different concrete addresses can share the same port");
}

/*! 声明的宿主端口可以是区间（实测 WinBoat 用 `47300-47309`）。 */
void HostPortUsageTest::parsesSinglePortsAndRanges()
{
    quint16 first = 0;
    quint16 last = 0;

    QVERIFY(PortBindingRules::parseHostPortSpec(QStringLiteral("8100"), &first, &last));
    QCOMPARE(first, quint16(8100));
    QCOMPARE(last, quint16(8100));

    QVERIFY(PortBindingRules::parseHostPortSpec(QStringLiteral("47300-47309"), &first, &last));
    QCOMPARE(first, quint16(47300));
    QCOMPARE(last, quint16(47309));

    // 坏输入一律拒绝：空、非数字、范围颠倒、越界
    QVERIFY(!PortBindingRules::parseHostPortSpec(QString(), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("abc"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("900-100"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("70000"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("0"), &first, &last));
}

/*! 没跑起来的容器不占端口（用户已确认的语义）。 */
void HostPortUsageTest::collectsOnlyRunningContainers()
{
    Container running;
    running.id = QStringLiteral("running");
    running.name = QStringLiteral("live");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("stopped");
    stopped.name = QStringLiteral("old");
    stopped.state = ContainerState::Exited;
    stopped.ports = {{QStringLiteral("0.0.0.0"), 80, 8200, QStringLiteral("tcp")}};

    Container unknown;
    unknown.id = QStringLiteral("unknown");
    unknown.name = QStringLiteral("mystery");
    unknown.state = ContainerState::Unknown;
    unknown.ports = {{QStringLiteral("0.0.0.0"), 80, 8300, QStringLiteral("tcp")}};

    Container exposed;
    exposed.id = QStringLiteral("exposed");
    exposed.name = QStringLiteral("expose-only");
    exposed.state = ContainerState::Running;
    exposed.ports = {{QString(), 443, 0, QStringLiteral("tcp")}}; // 只 EXPOSE、未发布

    const QList<Container> containers {running, stopped, unknown, exposed};
    const QList<HostPortEntry> entries = HostPortUsage::entriesFor(containers);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().hostPort, quint16(8100));
    QCOMPARE(entries.first().containerName, QStringLiteral("live"));
    QCOMPARE(entries.first().stateKey, QStringLiteral("inUse"));
    QCOMPARE(entries.first().displayAddress(), QStringLiteral("8100"));

    QVERIFY(HostPortUsage::holderFor(containers, QStringLiteral("0.0.0.0"), 8100) == QLatin1String("live"));
    QVERIFY2(HostPortUsage::holderFor(containers, QStringLiteral("0.0.0.0"), 8200).isEmpty(),
             "a stopped container must not block its port");
    QVERIFY(HostPortUsage::holderFor(containers, QStringLiteral("0.0.0.0"), 8300).isEmpty());
}

/*! IPv4 + IPv6 通配的同端口绑定合并成一条（界面据此画双环）。 */
void HostPortUsageTest::mergesDualStackWildcardBindings()
{
    Container container;
    container.id = QStringLiteral("dual");
    container.name = QStringLiteral("dual-stack");
    container.state = ContainerState::Running;
    container.ports = {
        {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
        {QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")},
        // 另一个具体地址：不参与合并
        {QStringLiteral("127.0.0.1"), 8888, 20204, QStringLiteral("tcp")},
    };

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({container});
    QCOMPARE(entries.size(), 2);

    const HostPortEntry dual = entries.first();
    QCOMPARE(dual.hostPort, quint16(20004));
    QVERIFY(dual.dualStack);
    QVERIFY(dual.ipv4 && dual.ipv6);
    QCOMPARE(dual.displayAddress(), QStringLiteral("20004"));

    const HostPortEntry specific = entries.at(1);
    QCOMPARE(specific.hostPort, quint16(20204));
    QVERIFY(!specific.dualStack);
    QCOMPARE(specific.displayAddress(), QStringLiteral("127.0.0.1:20204"));

    // 具体地址与通配同端口冲突；不同具体地址不冲突
    QVERIFY(!HostPortUsage::holderFor({container}, QStringLiteral("172.16.0.1"), 20004).isEmpty());
    QVERIFY2(HostPortUsage::holderFor({container}, QStringLiteral("172.16.0.1"), 20204).isEmpty(),
             "a wildcard binding must not be reported as the holder of a different concrete address");
}

/*! 建议端口：跳过已占用的端口，且不从特权端口开始。 */
void HostPortUsageTest::suggestsAPortOutsideTheUsedOnes()
{
    Container container;
    container.id = QStringLiteral("cid");
    container.name = QStringLiteral("web");
    container.state = ContainerState::Running;
    container.ports = {{QStringLiteral("0.0.0.0"), 80, 8000, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 81, 8001, QStringLiteral("tcp")}};

    QCOMPARE(HostPortUsage::nextFreePort({container}, 7999), 8000 + 2); // 8000/8001 都被占
    QCOMPARE(HostPortUsage::nextFreePort({container}, 0), 8002); // 起点从 8000 开始，跳开占用的
    QVERIFY2(HostPortUsage::nextFreePort({container}, 0) >= 1024, "never suggest a privileged port");
    QCOMPARE(HostPortUsage::nextFreePort({}, 9000), 9001); // 没有容器时就是下一个端口
}

QTEST_MAIN(HostPortUsageTest)

#include "tst_host_port_usage.moc"
