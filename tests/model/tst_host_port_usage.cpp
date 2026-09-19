/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_filter_model.h"
#include "model/host_port_model.h"
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
    void declaredBindingsShowUpOnlyWhenTheyDidNotTakeEffect();
    void modelExposesRowsForThePage();
    void filterSearchesAndSeparatesTheTwoStates();
    void refreshKeepsTheModelIntact();
    void rangeClusteringGroupsNearbyPorts();
    void rangeClusteringCapsVeryLongRanges();
    void stoppedContainersKeepTheirDeclaredPortsAsReserved();
    void rangeUsedCountCountsPortsNotDeclarations();
    void reservedPortThatIsTakenByAnotherContainerIsMarked();
    void sortingByContainerReallyReordersRows();
    void rangeBuildingScalesWithManyPorts();
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

/*!
 * "声明 vs 实际发布"（ARCH_next_ports.md 决定 3）。
 *
 * 实测 `alpine-82dc`：运行中、`HostConfig.PortBindings` 有绑定、`NetworkSettings.Ports` 是空的
 * —— 端口页要能解释"为什么显示占用了却连不上"。已经真的发布了的声明不重复出现。
 */
void HostPortUsageTest::declaredBindingsShowUpOnlyWhenTheyDidNotTakeEffect()
{
    Container running;
    running.id = QStringLiteral("cid-1");
    running.name = QStringLiteral("alpine-82dc");
    running.state = ContainerState::Running;
    // 实际发布：只有 8100
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    QHash<QString, QList<DeclaredPortBinding>> declared;
    DeclaredPortBinding published; // 声明了 8100，而且真的发布了
    published.containerPort = 80;
    published.protocol = QStringLiteral("tcp");
    published.hostPort = 8100;
    published.hostPortEnd = 8100;
    DeclaredPortBinding missing; // 声明了 4880，但没发布（实际情形）
    missing.containerPort = 4880;
    missing.protocol = QStringLiteral("tcp");
    missing.hostPort = 4880;
    missing.hostPortEnd = 4880;
    DeclaredPortBinding range; // 声明的是区间
    range.containerPort = 3389;
    range.protocol = QStringLiteral("tcp");
    range.hostIp = QStringLiteral("127.0.0.1");
    range.hostPort = 47300;
    range.hostPortEnd = 47309;
    declared.insert(running.id, {published, missing, range});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running}, declared);
    QCOMPARE(entries.size(), 3); // 8100（发布）+ 4880（没生效）+ 47300-47309（区间）

    QCOMPARE(entries.at(0).hostPort, quint16(4880));
    QCOMPARE(entries.at(0).stateKey, QStringLiteral("declaredNotPublished"));
    QCOMPARE(entries.at(0).containerName, QStringLiteral("alpine-82dc"));

    QCOMPARE(entries.at(1).hostPort, quint16(8100));
    QVERIFY2(entries.at(1).stateKey == QLatin1String("inUse"),
             "a declared binding that really is published must stay a single inUse entry");
    QCOMPARE(entries.at(1).displayAddress(), QStringLiteral("8100"));

    const HostPortEntry ranged = entries.at(2);
    QCOMPARE(ranged.portText(), QStringLiteral("47300-47309"));
    QCOMPARE(ranged.displayAddress(), QStringLiteral("127.0.0.1:47300-47309"));
    QCOMPARE(ranged.stateKey, QStringLiteral("declaredNotPublished"));

    // 没给声明时，行为与单参数版本一致（M1/M2 不受影响）
    QCOMPARE(HostPortUsage::entriesFor({running}).size(), 1);
}


/*! 端口页模型：role 齐备（端口是第一视觉焦点，容器只是其中一列）。 */
void HostPortUsageTest::modelExposesRowsForThePage()
{
    Container dual;
    dual.id = QStringLiteral("cid-dual");
    dual.name = QStringLiteral("web-frontend");
    dual.image = QStringLiteral("registry.example.com/team/frontend:2.4.1");
    dual.state = ContainerState::Running;
    dual.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
                  {QStringLiteral("::"), 80, 8080, QStringLiteral("tcp")}};

    Container declaredOnly;
    declaredOnly.id = QStringLiteral("cid-declared");
    declaredOnly.name = QStringLiteral("alpine-82dc");
    declaredOnly.image = QStringLiteral("alpine:latest");
    declaredOnly.state = ContainerState::Running;
    declaredOnly.ports = {};

    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(declaredOnly.id,
                    {DeclaredPortBinding {4880, QStringLiteral("tcp"), QString(), 4880, 4880}});

    HostPortModel model;
    model.setEntries(HostPortUsage::entriesFor({dual, declaredOnly}, declared));
    QCOMPARE(model.count(), 2);

    const QModelIndex first = model.index(0, 0);
    QCOMPARE(first.data(HostPortModel::PortTextRole).toString(), QStringLiteral("4880"));
    QCOMPARE(first.data(HostPortModel::StateKeyRole).toString(), QStringLiteral("declaredNotPublished"));
    QCOMPARE(first.data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("alpine-82dc"));
    QVERIFY2(!first.data(HostPortModel::ActionableRole).toBool(),
             "a declared-only binding must not offer 'stop container'");

    const QModelIndex second = model.index(1, 0);
    QCOMPARE(second.data(HostPortModel::PortTextRole).toString(), QStringLiteral("8080"));
    QCOMPARE(second.data(HostPortModel::StateKeyRole).toString(), QStringLiteral("inUse"));
    QVERIFY2(second.data(HostPortModel::ActionableRole).toBool(), "an in-use port can stop its holder");
}

/*! 搜索（端口 / 容器 / 镜像 / 地址都算）与状态过滤。 */
void HostPortUsageTest::filterSearchesAndSeparatesTheTwoStates()
{
    Container running;
    running.id = QStringLiteral("cid-1");
    running.name = QStringLiteral("web-frontend");
    running.image = QStringLiteral("registry.example.com/team/frontend:2.4.1");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};

    Container declaredOnly;
    declaredOnly.id = QStringLiteral("cid-2");
    declaredOnly.name = QStringLiteral("alpine-82dc");
    declaredOnly.image = QStringLiteral("alpine:latest");
    declaredOnly.state = ContainerState::Running;
    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(declaredOnly.id, {DeclaredPortBinding {4880, QStringLiteral("tcp"), QString(), 4880, 4880}});

    HostPortModel model;
    model.setEntries(HostPortUsage::entriesFor({running, declaredOnly}, declared));

    HostPortFilterModel filter;
    filter.setSourceModel(&model);
    QCOMPARE(filter.count(), 2);

    // 搜容器名
    filter.setSearchText(QStringLiteral("frontend"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("web-frontend"));

    // 搜端口号
    filter.setSearchText(QStringLiteral("4880"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::StateKeyRole).toString(), QStringLiteral("declaredNotPublished"));

    // 搜镜像
    filter.setSearchText(QStringLiteral("registry.example.com"));
    QCOMPARE(filter.count(), 1);

    // 过滤：只看"运行中占用"
    filter.setSearchText(QString());
    filter.setStateFilter(QStringLiteral("inUse"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("declaredNotPublished"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 2);

    // 排序：默认端口升序
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 4880);
    filter.setSortKey(QStringLiteral("container"));
    QVERIFY(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString() < filter.index(1, 0).data(HostPortModel::ContainerNameRole).toString());
}

/*!
 * 刷新不得重置模型（本项目的老问题：整表重置会把滚动位置拉回顶部）。
 *
 * 键相同、只有值变化时只发 `dataChanged`，不发 `beginResetModel`。
 */
void HostPortUsageTest::refreshKeepsTheModelIntact()
{
    Container running;
    running.id = QStringLiteral("cid-1");
    running.name = QStringLiteral("web");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};

    HostPortModel model;
    model.setEntries(HostPortUsage::entriesFor({running}));
    QCOMPARE(model.count(), 1);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    running.image = QStringLiteral("alpine:3.21"); // 只有值变化，键不变
    model.setEntries(HostPortUsage::entriesFor({running}));
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(model.count(), 1);
    QVERIFY(dataSpy.count() >= 1);
}


/*!
 * 区间聚类（ARCH_next_ports.md §4.B，里程碑 M4）。
 *
 * 边界要稳：紧挨着的端口算一段，间隔超过 gap 就断开；每段还会向两侧留几个空闲端口，
 * 这样用户能直接看出"这一段附近哪里空着"。
 */
void HostPortUsageTest::rangeClusteringGroupsNearbyPorts()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("medai");
    container.state = ContainerState::Running;
    // 20001-20004 紧邻 → 一段；8000 与它们相距很远 → 另一段
    container.ports = {{QStringLiteral("0.0.0.0"), 8888, 20001, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20002, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20003, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 80, 8000, QStringLiteral("tcp")}};

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({container});
    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries, 5, 2, 64);
    QCOMPARE(ranges.size(), 2);

    // 第一段：8000 前后各留 2 个空闲端口
    QCOMPARE(ranges.at(0).first, quint16(7998));
    QCOMPARE(ranges.at(0).last, quint16(8002));
    QCOMPARE(ranges.at(0).tileCount, 5);
    QCOMPARE(ranges.at(0).hiddenCount, 0);
    QCOMPARE(ranges.at(0).usedCount, 1);

    // 第二段：20001-20004 合并成一段，两侧各留 2 个
    QCOMPARE(ranges.at(1).first, quint16(19999));
    QCOMPARE(ranges.at(1).last, quint16(20006));
    QCOMPARE(ranges.at(1).usedCount, 4);

    // 间隔 6（> gap=5）时断开
    Container spaced;
    spaced.id = QStringLiteral("cid-2");
    spaced.name = QStringLiteral("spaced");
    spaced.state = ContainerState::Running;
    spaced.ports = {{QStringLiteral("0.0.0.0"), 80, 9000, QStringLiteral("tcp")},
                    {QStringLiteral("0.0.0.0"), 81, 9007, QStringLiteral("tcp")}}; // 9000 与 9007 之间空 6 个
    QCOMPARE(HostPortUsage::clusterRanges(HostPortUsage::entriesFor({spaced}), 5, 0, 64).size(), 2);
    QCOMPARE(HostPortUsage::clusterRanges(HostPortUsage::entriesFor({spaced}), 6, 0, 64).size(), 1);

    // 每个方块的状态：空闲为空、占用是 inUse
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 20003), QStringLiteral("inUse"));
    QVERIFY(HostPortUsage::stateKeyForPort(entries, 20005).isEmpty());

    // 区间（47300-47309）按整段算被占
    Container ranged;
    ranged.id = QStringLiteral("cid-3");
    ranged.name = QStringLiteral("winboat");
    ranged.state = ContainerState::Running;
    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(ranged.id, {DeclaredPortBinding {3389, QStringLiteral("tcp"), QString(), 47300, 47309}});
    const QList<HostPortEntry> rangedEntries = HostPortUsage::entriesFor({ranged}, declared);
    QCOMPARE(rangedEntries.size(), 1);
    QCOMPARE(HostPortUsage::stateKeyForPort(rangedEntries, 47305), QStringLiteral("declaredNotPublished"));
    QVERIFY(HostPortUsage::stateKeyForPort(rangedEntries, 47310).isEmpty());
}

/*!
 * 很长的区间必须限流：1000-1100 这种段不能渲染 101 个方块。
 *
 * 超出上限的部分记进 `hiddenCount`，界面显示"还有 N 个"。
 */
void HostPortUsageTest::rangeClusteringCapsVeryLongRanges()
{
    Container container;
    container.id = QStringLiteral("cid-long");
    container.name = QStringLiteral("range-holder");
    container.state = ContainerState::Running;
    // 声明 1000-1100（101 个端口）——用一个区间式的声明最省事
    QHash<QString, QList<DeclaredPortBinding>> declared;
    DeclaredPortBinding binding;
    binding.containerPort = 80;
    binding.protocol = QStringLiteral("tcp");
    binding.hostPort = 1000;
    binding.hostPortEnd = 1100;
    declared.insert(container.id, {binding});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({container}, declared);
    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries, 5, 0, 20);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).first, quint16(1000));
    QCOMPARE(ranges.at(0).last, quint16(1100));
    QCOMPARE(ranges.at(0).tileCount, 20);
    QCOMPARE(ranges.at(0).hiddenCount, 101 - 20);
    QVERIFY2(ranges.at(0).tileCount <= 20, "a long range must be capped");
}


/*!
 * 未运行容器声明过的端口 = `reserved`（用户实测反馈要求能看到）。
 *
 * 语义（与"占用"区分开）：端口**现在是空的**，但那个容器一起来就会要回去；
 * 因此它不能参与创建表单的冲突判断（`holderFor` 只算运行中的容器，另有负例守着）。
 */
void HostPortUsageTest::stoppedContainersKeepTheirDeclaredPortsAsReserved()
{
    Container running;
    running.id = QStringLiteral("cid-run");
    running.name = QStringLiteral("live");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("cid-stop");
    stopped.name = QStringLiteral("alpine-82dc");
    stopped.image = QStringLiteral("alpine:latest");
    stopped.state = ContainerState::Exited;

    ContainerDetail detail;
    detail.id = stopped.id;
    detail.declaredPorts = {{80, QStringLiteral("tcp"), QString(), 8810, 8810},
                            {81, QStringLiteral("tcp"), QString(), 8810, 8810}};

    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(detail.id, detail.declaredPorts);

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running, stopped}, declared);
    /*
     * 3 条：8100（运行中占用）+ 8810 的**两条**声明。
     *
     * 后者就是真实容器的样子：`alpine-82dc` 把 8810 同时声明给了 80/81/82 三个容器端口
     * （正是"一个宿主端口映射到多个容器端口"那个必然会启动失败的写法）。端口页如实列出两条，
     * 用户一眼就能看出这个容器起了会炸——不要在这里"帮"他合并成一条。
     */
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).hostPort, quint16(8100));
    QCOMPARE(entries.at(0).stateKey, QStringLiteral("inUse"));
    QCOMPARE(entries.at(1).hostPort, quint16(8810));
    QCOMPARE(entries.at(1).stateKey, QStringLiteral("reserved"));
    QCOMPARE(entries.at(1).containerPort, quint16(80));
    QCOMPARE(entries.at(1).containerName, QStringLiteral("alpine-82dc"));
    QCOMPARE(entries.at(2).hostPort, quint16(8810));
    QCOMPARE(entries.at(2).containerPort, quint16(81));

    // 关键：reserved 不算"占用"——创建表单不能因为它拦住用户
    QVERIFY2(HostPortUsage::holderFor({running, stopped}, QStringLiteral("0.0.0.0"), 8810).isEmpty(),
             "a stopped container must not block a port for new containers");

    // 模型里 reserved 不给"停止容器"（它没在跑），但仍可跳转
    HostPortModel model;
    model.setEntries(entries);
    const QModelIndex reservedRow = model.index(1, 0);
    QVERIFY2(!reservedRow.data(HostPortModel::ActionableRole).toBool(), "reserved rows cannot be stopped");
    QCOMPARE(reservedRow.data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("alpine-82dc"));

    // 地图：未运行的声明也点亮（用保留色）
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 8810), QStringLiteral("reserved"));
}


/*!
 * 区间里的"被占用"数量必须按**端口个数**算（用户实测：WinBoat 的情况）。
 *
 * 它声明了 5 段、每段 10 个端口，落在同一个区间里，其中两段还重叠
 * （47268-47278 与 47270-47279）：原来按"声明条数"显示 5，而图上亮着几十个格子。
 */
void HostPortUsageTest::rangeUsedCountCountsPortsNotDeclarations()
{
    Container stopped;
    stopped.id = QStringLiteral("winboat-id");
    stopped.name = QStringLiteral("WinBoat");
    stopped.state = ContainerState::Exited;

    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(stopped.id,
                    {DeclaredPortBinding {3389, QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), 47268, 47278},
                     DeclaredPortBinding {3389, QStringLiteral("udp"), QStringLiteral("127.0.0.1"), 47270, 47279},
                     DeclaredPortBinding {7148, QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), 47280, 47289},
                     DeclaredPortBinding {7149, QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), 47290, 47299},
                     DeclaredPortBinding {8006, QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), 47300, 47309}});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({stopped}, declared);
    QCOMPARE(entries.size(), 5); // 五条声明（两条重叠）

    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries, 5, 0, 64);
    QCOMPARE(ranges.size(), 1);
    // 并集 = 47268..47309 → 42 个端口（不是 5，也不是 50：重叠只算一次）
    QCOMPARE(ranges.first().first, quint16(47268));
    QCOMPARE(ranges.first().last, quint16(47309));
    QCOMPARE(ranges.first().usedCount, 42);
}


/*!
 * 未运行容器的声明端口如果**已被别的容器占着**，要单独标出来（用户要求红色的"被占用"）。
 *
 * 语义差别很实在：`reserved` 只是"现在是空的，它一起来会要回去"；
 * `reservedTaken` 是"它一起来就会端口冲突、直接启动失败"。
 */
void HostPortUsageTest::reservedPortThatIsTakenByAnotherContainerIsMarked()
{
    Container running;
    running.id = QStringLiteral("run-id");
    running.name = QStringLiteral("noreva-medai");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 8888, 20002, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("stop-id");
    stopped.name = QStringLiteral("ml-medai");
    stopped.state = ContainerState::Exited;

    QHash<QString, QList<DeclaredPortBinding>> declared;
    // 同一台机器上：一个未运行的容器声明了**已经被别人占着**的 20002，另一个声明的是空闲的 20003
    declared.insert(stopped.id, {DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20002, 20002},
                                 DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20003, 20003}});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running, stopped}, declared);
    QCOMPARE(entries.size(), 3);

    // 同一端口上会有两条（占着的那个 + 声明它的那个），顺序是"端口 → 容器名"
    const auto stateOf = [&entries](const QString &containerName, quint16 port) {
        for (const HostPortEntry &entry : entries) {
            if (entry.containerName == containerName && entry.hostPort == port) {
                return entry.stateKey;
            }
        }
        return QString();
    };
    QCOMPARE(stateOf(QStringLiteral("noreva-medai"), 20002), QStringLiteral("inUse"));
    QCOMPARE(stateOf(QStringLiteral("ml-medai"), 20002), QStringLiteral("reservedTaken")); // 未运行 + 端口被占
    QCOMPARE(stateOf(QStringLiteral("ml-medai"), 20003), QStringLiteral("reserved")); // 未运行 + 端口空着

    // "未启动"这个过滤项要把两种都选进来
    HostPortModel model;
    model.setEntries(entries);
    HostPortFilterModel filter;
    filter.setSourceModel(&model);
    filter.setStateFilter(QStringLiteral("reserved"));
    QCOMPARE(filter.count(), 2);
    filter.setStateFilter(QStringLiteral("inUse"));
    QCOMPARE(filter.count(), 1);
}

/*!
 * 排序必须真的生效（用户实测"排序功能失效"）。
 *
 * 之前 `updateSorting()` 只调 `sort(0, AscendingOrder)`：列与方向都没变时
 * Qt 认为无事可做，换了排序键也不重排。这里用**两种顺序不一致**的数据守住它：
 * 端口最小的那个容器名反而排在后面。
 */
void HostPortUsageTest::sortingByContainerReallyReordersRows()
{
    Container alpha;
    alpha.id = QStringLiteral("alpha-id");
    alpha.name = QStringLiteral("alpha");
    alpha.state = ContainerState::Running;
    alpha.ports = {{QStringLiteral("0.0.0.0"), 80, 9000, QStringLiteral("tcp")}};

    Container zulu;
    zulu.id = QStringLiteral("zulu-id");
    zulu.name = QStringLiteral("zulu");
    zulu.state = ContainerState::Running;
    zulu.ports = {{QStringLiteral("0.0.0.0"), 80, 8000, QStringLiteral("tcp")}};

    HostPortModel model;
    model.setEntries(HostPortUsage::entriesFor({alpha, zulu}));
    HostPortFilterModel filter;
    filter.setSourceModel(&model);

    // 默认按端口：8000（zulu）在前
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 8000);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("zulu"));

    // 换成按容器名：alpha（端口 9000）必须排到前面——只调 sort() 是不够的
    filter.setSortKey(QStringLiteral("container"));
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(filter.index(1, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("zulu"));

    // 换回端口排序也要立即生效
    filter.setSortKey(QStringLiteral("port"));
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 8000);
}


/*!
 * 区间地图的数据构建必须是"能扛住真实规模"的（用户实测：切筛选会卡 1-2 秒）。
 *
 * 造 40 个容器 × 每个 40 个端口（含区间），断言：构建耗时可接受、方块数受上限约束。
 * 这不是精确的性能测试，而是防止有人再把去重改回 `QList::contains()` 这类平方级写法
 * （本用例在优化前需要几百毫秒，优化后是个位数毫秒）。
 */
void HostPortUsageTest::rangeBuildingScalesWithManyPorts()
{
    QList<Container> containers;
    QHash<QString, QList<DeclaredPortBinding>> declared;
    for (int i = 0; i < 40; ++i) {
        Container container;
        container.id = QStringLiteral("cid-%1").arg(i);
        container.name = QStringLiteral("container-%1").arg(i, 2, 10, QLatin1Char('0'));
        container.image = QStringLiteral("alpine:latest");
        container.state = i % 3 == 0 ? ContainerState::Exited : ContainerState::Running;
        for (int p = 0; p < 20; ++p) {
            // 每个容器一段 20 个端口的区间（展开后 40 个端口）
            const quint16 base = quint16(1000 + i * 1200 + p * 12); // 各容器互相隔开 → 形成很多段
            declared[container.id].append(DeclaredPortBinding {quint16(8000 + p), QStringLiteral("tcp"), QString(),
                                                                base, quint16(base + 9)});
        }
        containers.append(container);
    }

    QElapsedTimer timer;
    timer.start();
    const QList<HostPortEntry> entries = HostPortUsage::entriesFor(containers, declared);
    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(entries.size() >= 40 * 20);
    QVERIFY(!ranges.isEmpty());
    for (const HostPortRange &range : ranges) {
        QVERIFY2(range.tileCount <= 64, "each range must stay capped");
    }
    qInfo() << "range building took" << elapsed << "ms for" << entries.size() << "entries →" << ranges.size() << "ranges";
    // 实测：优化前（QList::contains 平方级去重）129 ms，优化后 16 ms；
    // 门限取 60 ms——比实测慢 4 倍仍然会失败，但不会因为机器慢而误报
    QVERIFY2(elapsed < 60, qPrintable(QStringLiteral("range building too slow: %1 ms").arg(elapsed)));
}


QTEST_MAIN(HostPortUsageTest)

#include "tst_host_port_usage.moc"
