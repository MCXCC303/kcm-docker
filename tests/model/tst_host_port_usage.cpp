/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Host port usage table (ARCH_next_ports.md §3, milestone M1).
 *
 * One implementation shared by the ports page, the range map and the create form's conflict
 * detection, so the tests focus on:
 *   - only running containers count (user-confirmed: a stopped one holds nothing);
 *   - IPv4/IPv6 wildcard bindings merge into one entry (the UI draws a double ring from it);
 *   - overlap semantics (a wildcard conflicts with anything; two concrete addresses only if equal);
 *   - the suggested port (`nextFreePort`) skips used and privileged ports.
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
    void mapTileStatePrefersRunningUnlessFiltered();
};

void HostPortUsageTest::initTestCase()
{
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
}

/*! Shared address logic (there used to be two copies; this pins the semantics). */
void HostPortUsageTest::bindingRulesAreSharedAndStrict()
{
    using namespace PortBindingRules;

    QVERIFY(isWildcardAddress(QString()));
    QVERIFY(isWildcardAddress(QStringLiteral("0.0.0.0")));
    QVERIFY(isWildcardAddress(QStringLiteral("::")));
    QVERIFY(isWildcardAddress(QStringLiteral("[::]")));
    QVERIFY(!isWildcardAddress(QStringLiteral("127.0.0.1")));

    // A wildcard conflicts with every address (IPv4 vs IPv6 wildcards are mutually exclusive on Linux)
    QVERIFY(hostBindingsOverlap(QStringLiteral("0.0.0.0"), QStringLiteral("127.0.0.1")));
    QVERIFY(hostBindingsOverlap(QStringLiteral("0.0.0.0"), QStringLiteral("::")));
    QVERIFY(hostBindingsOverlap(QString(), QStringLiteral("192.168.1.5")));
    // Two concrete addresses: only identical ones conflict
    QVERIFY(hostBindingsOverlap(QStringLiteral("127.0.0.1"), QStringLiteral("127.0.0.1")));
    QVERIFY2(!hostBindingsOverlap(QStringLiteral("127.0.0.1"), QStringLiteral("192.168.1.5")),
             "two different concrete addresses can share the same port");
}

/*! A declared host port may be a range (WinBoat really uses `47300-47309`). */
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

    // Bad input is always rejected: empty, non-numeric, inverted range, out of range
    QVERIFY(!PortBindingRules::parseHostPortSpec(QString(), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("abc"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("900-100"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("70000"), &first, &last));
    QVERIFY(!PortBindingRules::parseHostPortSpec(QStringLiteral("0"), &first, &last));
}

/*! A container that is not running holds no port (user-confirmed semantics). */
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
    exposed.ports = {{QString(), 443, 0, QStringLiteral("tcp")}}; // EXPOSE only, not published

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

/*! IPv4 + IPv6 wildcard bindings on the same port merge into one entry (the UI draws two rings). */
void HostPortUsageTest::mergesDualStackWildcardBindings()
{
    Container container;
    container.id = QStringLiteral("dual");
    container.name = QStringLiteral("dual-stack");
    container.state = ContainerState::Running;
    container.ports = {
        {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
        {QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")},
        // another concrete address: never merged
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

    // A concrete address conflicts with a wildcard on the same port; two concrete addresses do not
    QVERIFY(!HostPortUsage::holderFor({container}, QStringLiteral("172.16.0.1"), 20004).isEmpty());
    QVERIFY2(HostPortUsage::holderFor({container}, QStringLiteral("172.16.0.1"), 20204).isEmpty(),
             "a wildcard binding must not be reported as the holder of a different concrete address");
}

/*! Suggested port: skip the used ones and never start below the privileged range. */
void HostPortUsageTest::suggestsAPortOutsideTheUsedOnes()
{
    Container container;
    container.id = QStringLiteral("cid");
    container.name = QStringLiteral("web");
    container.state = ContainerState::Running;
    container.ports = {{QStringLiteral("0.0.0.0"), 80, 8000, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 81, 8001, QStringLiteral("tcp")}};

    QCOMPARE(HostPortUsage::nextFreePort({container}, 7999), 8000 + 2); // 8000/8001 are both taken
    QCOMPARE(HostPortUsage::nextFreePort({container}, 0), 8002); // start from 8000 and skip the taken ones
    QVERIFY2(HostPortUsage::nextFreePort({container}, 0) >= 1024, "never suggest a privileged port");
    QCOMPARE(HostPortUsage::nextFreePort({}, 9000), 9001); // with no containers it is simply the next port
}

/*!
 * "Declared vs actually published" (ARCH_next_ports.md decision 3).
 *
 * Real case `alpine-82dc`: running, `HostConfig.PortBindings` has bindings, `NetworkSettings.Ports`
 * is empty -- the ports page must explain "why does it look taken but nothing connects". A
 * declaration that really was published is not listed twice.
 */
void HostPortUsageTest::declaredBindingsShowUpOnlyWhenTheyDidNotTakeEffect()
{
    Container running;
    running.id = QStringLiteral("cid-1");
    running.name = QStringLiteral("alpine-82dc");
    running.state = ContainerState::Running;
    // actually published: only 8100
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    QHash<QString, QList<DeclaredPortBinding>> declared;
    DeclaredPortBinding published; // declared 8100 and really published
    published.containerPort = 80;
    published.protocol = QStringLiteral("tcp");
    published.hostPort = 8100;
    published.hostPortEnd = 8100;
    DeclaredPortBinding missing; // declared 4880 but never published (the real case)
    missing.containerPort = 4880;
    missing.protocol = QStringLiteral("tcp");
    missing.hostPort = 4880;
    missing.hostPortEnd = 4880;
    DeclaredPortBinding range; // a range was declared
    range.containerPort = 3389;
    range.protocol = QStringLiteral("tcp");
    range.hostIp = QStringLiteral("127.0.0.1");
    range.hostPort = 47300;
    range.hostPortEnd = 47309;
    declared.insert(running.id, {published, missing, range});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running}, declared);
    QCOMPARE(entries.size(), 3); // 8100 (published) + 4880 (no effect) + 47300-47309 (range)

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

    // Without declarations the behavior matches the single-argument version (M1/M2 unaffected)
    QCOMPARE(HostPortUsage::entriesFor({running}).size(), 1);
}


/*! Ports page model: every role present (the port is the visual focus, the container just one column). */
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

/*! Search (port / container / image / address) and state filtering. */
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

    // search by container name
    filter.setSearchText(QStringLiteral("frontend"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("web-frontend"));

    // search by port number
    filter.setSearchText(QStringLiteral("4880"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::StateKeyRole).toString(), QStringLiteral("declaredNotPublished"));

    // search by image
    filter.setSearchText(QStringLiteral("registry.example.com"));
    QCOMPARE(filter.count(), 1);

    // filter: only "held by a running container"
    filter.setSearchText(QString());
    filter.setStateFilter(QStringLiteral("inUse"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("declaredNotPublished"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 2);

    // Sorting: default is port ascending
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 4880);
    filter.setSortKey(QStringLiteral("container"));
    QVERIFY(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString() < filter.index(1, 0).data(HostPortModel::ContainerNameRole).toString());
}

/*!
 * A refresh must not reset the model (this project's old problem: a full reset scrolls back to top).
 *
 * With unchanged keys and only changed values, emit `dataChanged`, never `beginResetModel`.
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

    running.image = QStringLiteral("alpine:3.21"); // only a value changes, the key does not
    model.setEntries(HostPortUsage::entriesFor({running}));
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(model.count(), 1);
    QVERIFY(dataSpy.count() >= 1);
}


/*!
 * Range clustering (ARCH_next_ports.md §4.B, milestone M4).
 *
 * Boundaries must be stable: adjacent ports form one range, a gap larger than `gap` splits it, and
 * each range keeps a few free ports on both sides so the user sees where room is left nearby.
 */
void HostPortUsageTest::rangeClusteringGroupsNearbyPorts()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("medai");
    container.state = ContainerState::Running;
    // 20001-20004 are adjacent -> one range; 8000 is far away -> another
    container.ports = {{QStringLiteral("0.0.0.0"), 8888, 20001, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20002, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20003, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
                       {QStringLiteral("0.0.0.0"), 80, 8000, QStringLiteral("tcp")}};

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({container});
    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries, 5, 2, 64);
    QCOMPARE(ranges.size(), 2);

    // First range: 2 free ports on each side of 8000
    QCOMPARE(ranges.at(0).first, quint16(7998));
    QCOMPARE(ranges.at(0).last, quint16(8002));
    QCOMPARE(ranges.at(0).tileCount, 5);
    QCOMPARE(ranges.at(0).hiddenCount, 0);
    QCOMPARE(ranges.at(0).usedCount, 1);

    // Second range: 20001-20004 merge, 2 free on each side
    QCOMPARE(ranges.at(1).first, quint16(19999));
    QCOMPARE(ranges.at(1).last, quint16(20006));
    QCOMPARE(ranges.at(1).usedCount, 4);

    // A gap of 6 (> gap=5) splits
    Container spaced;
    spaced.id = QStringLiteral("cid-2");
    spaced.name = QStringLiteral("spaced");
    spaced.state = ContainerState::Running;
    spaced.ports = {{QStringLiteral("0.0.0.0"), 80, 9000, QStringLiteral("tcp")},
                    {QStringLiteral("0.0.0.0"), 81, 9007, QStringLiteral("tcp")}}; // gap of 6 (> 5)
    QCOMPARE(HostPortUsage::clusterRanges(HostPortUsage::entriesFor({spaced}), 5, 0, 64).size(), 2);
    QCOMPARE(HostPortUsage::clusterRanges(HostPortUsage::entriesFor({spaced}), 6, 0, 64).size(), 1);

    // Tile state: free is empty, taken is inUse
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 20003), QStringLiteral("inUse"));
    QVERIFY(HostPortUsage::stateKeyForPort(entries, 20005).isEmpty());

    // A range (47300-47309) counts as taken across its whole span
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
 * Very long ranges must be throttled: 1000-1100 must not render 101 tiles.
 *
 * Everything beyond the cap is counted into `hiddenCount`, which the UI shows as "N more".
 */
void HostPortUsageTest::rangeClusteringCapsVeryLongRanges()
{
    Container container;
    container.id = QStringLiteral("cid-long");
    container.name = QStringLiteral("range-holder");
    container.state = ContainerState::Running;
    // Declare 1000-1100 (101 ports) -- a range-style declaration is the easiest way
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
 * Ports declared by a stopped container = `reserved` (user feedback asked to see them).
 *
 * Semantics, distinct from "in use": the port is FREE right now, but that container will take it
 * back once started; so it must not take part in the create form's conflict check (`holderFor`
 * counts running containers only, and a negative case guards that).
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
     * 3 entries: 8100 (in use) + BOTH declarations of 8810.
     *
     * The latter is what the real container looks like: `alpine-82dc` declares 8810 for the three
     * container ports 80/81/82 (exactly the "one host port mapped to several container ports" form
     * that is bound to fail at start). The ports page lists both entries so the user sees at a
     * glance that starting it will blow up -- do not "helpfully" merge them here.
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

    // Key point: reserved does not count as "in use" -- the create form must not block on it
    QVERIFY2(HostPortUsage::holderFor({running, stopped}, QStringLiteral("0.0.0.0"), 8810).isEmpty(),
             "a stopped container must not block a port for new containers");

    // The model offers no "stop container" for reserved (nothing runs), but the row stays navigable
    HostPortModel model;
    model.setEntries(entries);
    const QModelIndex reservedRow = model.index(1, 0);
    QVERIFY2(!reservedRow.data(HostPortModel::ActionableRole).toBool(), "reserved rows cannot be stopped");
    QCOMPARE(reservedRow.data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("alpine-82dc"));

    // Map: declarations from stopped containers light up too (in the reserved color)
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 8810), QStringLiteral("reserved"));
}


/*!
 * A range's "used" count must count PORTS, not declarations (user-reported: the WinBoat case).
 *
 * It declares 5 ranges of 10 ports each inside one range, two of them overlapping
 * (47268-47278 and 47270-47279): counting declarations showed 5 while dozens of tiles were lit.
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
    QCOMPARE(entries.size(), 5); // five declarations (two of them overlapping)

    const QList<HostPortRange> ranges = HostPortUsage::clusterRanges(entries, 5, 0, 64);
    QCOMPARE(ranges.size(), 1);
    // Union = 47268..47309 -> 42 ports (neither 5 nor 50: overlaps count once)
    QCOMPARE(ranges.first().first, quint16(47268));
    QCOMPARE(ranges.first().last, quint16(47309));
    QCOMPARE(ranges.first().usedCount, 42);
}


/*!
 * A declared port of a stopped container that is ALREADY held by another container is marked
 * separately (the user asked for a red "taken" state).
 *
 * The difference is real: `reserved` means "free now, taken back as soon as it starts";
 * `reservedTaken` means "starting it will conflict and fail outright".
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
    // On one host: a stopped container declares the already held 20002 and the free 20003
    declared.insert(stopped.id, {DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20002, 20002},
                                 DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20003, 20003}});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running, stopped}, declared);
    QCOMPARE(entries.size(), 3);

    // Two entries share a port (the holder and the declarer); the order is port -> container name
    const auto stateOf = [&entries](const QString &containerName, quint16 port) {
        for (const HostPortEntry &entry : entries) {
            if (entry.containerName == containerName && entry.hostPort == port) {
                return entry.stateKey;
            }
        }
        return QString();
    };
    QCOMPARE(stateOf(QStringLiteral("noreva-medai"), 20002), QStringLiteral("inUse"));
    QCOMPARE(stateOf(QStringLiteral("ml-medai"), 20002), QStringLiteral("reservedTaken")); // stopped + held
    QCOMPARE(stateOf(QStringLiteral("ml-medai"), 20003), QStringLiteral("reserved")); // stopped + free

    // The "not started" filter must select both of them
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
 * Sorting must really take effect (user-reported "sorting is broken").
 *
 * `updateSorting()` used to call only `sort(0, AscendingOrder)`: with the same column and order Qt
 * considers there is nothing to do, so changing the sort key did not reorder. The guard here uses
 * data whose two orders differ: the container with the lowest port sorts last by name.
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

    // Default is by port: 8000 (zulu) first
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 8000);
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("zulu"));

    // Switch to container name: alpha (port 9000) must come first -- calling sort() alone is not enough
    filter.setSortKey(QStringLiteral("container"));
    QCOMPARE(filter.index(0, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(filter.index(1, 0).data(HostPortModel::ContainerNameRole).toString(), QStringLiteral("zulu"));

    // Switching back to port order must take effect immediately too
    filter.setSortKey(QStringLiteral("port"));
    QCOMPARE(filter.index(0, 0).data(HostPortModel::HostPortRole).toInt(), 8000);
}


/*!
 * Range map construction must cope with real scale (user-reported: switching a filter stalled 1-2 s).
 *
 * 40 containers x 40 ports each (ranges included): construction stays acceptable and the tile count
 * stays capped. This is not a precise benchmark, it only keeps deduplication from turning back into
 * a quadratic `QList::contains()` (this case needed hundreds of ms before the fix, single-digit ms
 * after).
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
            // One 20-port range per container (40 ports after expansion)
            const quint16 base = quint16(1000 + i * 1200 + p * 12); // containers spaced apart -> many ranges
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
    // Measured: 129 ms before the fix (quadratic QList::contains dedup), 16 ms after;
    // 60 ms still fails at 4x the measurement but does not misfire on a slow machine
    QVERIFY2(elapsed < 60, qPrintable(QStringLiteral("range building too slow: %1 ms").arg(elapsed)));
}


/*!
 * Tile state resolution for the map (two real scenarios reported by the user).
 *
 * On the actual machine:
 *   - 20003: `alpine-9239` runs but its mapping never took effect (not published), while `ml-medai`
 *     runs and really holds it (in use)
 *   - 20004: `dl-medai` is stopped but declared it (not started / taken), and that port is held by a
 *     running container
 * Requirement: in "all ports" a running holder wins; taken/not-published states appear only when
 * their filter is selected.
 */
void HostPortUsageTest::mapTileStatePrefersRunningUnlessFiltered()
{
    Container running;
    running.id = QStringLiteral("ml-id");
    running.name = QStringLiteral("ml-medai");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 8888, 20003, QStringLiteral("tcp")}};

    Container notBound;
    notBound.id = QStringLiteral("alpine-id");
    notBound.name = QStringLiteral("alpine-9239");
    notBound.state = ContainerState::Running; // running, but nothing published

    Container stopped;
    stopped.id = QStringLiteral("dl-id");
    stopped.name = QStringLiteral("dl-medai");
    stopped.state = ContainerState::Exited;

    QHash<QString, QList<DeclaredPortBinding>> declared;
    declared.insert(notBound.id, {DeclaredPortBinding {80, QStringLiteral("tcp"), QString(), 20003, 20003}});
    declared.insert(stopped.id, {DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20003, 20003}});

    const QList<HostPortEntry> entries = HostPortUsage::entriesFor({running, notBound, stopped}, declared);
    // Three entries on 20003: in use (ml-medai), not published (alpine-9239), reserved-taken (dl-medai)
    QCOMPARE(entries.size(), 3);

    // All ports: a running holder wins
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 20003), QStringLiteral("inUse"));
    QCOMPARE(HostPortUsage::entryForPort(entries, 20003).containerName, QStringLiteral("ml-medai"));

    // "Not published" filter selected: show it as not published (otherwise it is invisible on the map)
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 20003, {QStringLiteral("declaredNotPublished")}),
             QStringLiteral("declaredNotPublished"));
    QCOMPARE(HostPortUsage::entryForPort(entries, 20003, {QStringLiteral("declaredNotPublished")}).containerName,
             QStringLiteral("alpine-9239"));

    // "Not started / taken" filter selected: show it as taken (a running container holds that port)
    QCOMPARE(HostPortUsage::stateKeyForPort(entries, 20003, {QStringLiteral("reserved")}),
             QStringLiteral("reservedTaken"));
    QCOMPARE(HostPortUsage::entryForPort(entries, 20003, {QStringLiteral("reserved")}).containerName,
             QStringLiteral("dl-medai"));

    // A port that is only "not started" (free) still shows as reserved in the all-ports view
    Container stoppedOnly;
    stoppedOnly.id = QStringLiteral("gt-id");
    stoppedOnly.name = QStringLiteral("gt-medai");
    stoppedOnly.state = ContainerState::Exited;
    QHash<QString, QList<DeclaredPortBinding>> onlyDeclared;
    onlyDeclared.insert(stoppedOnly.id, {DeclaredPortBinding {8888, QStringLiteral("tcp"), QString(), 20001, 20001}});
    const QList<HostPortEntry> onlyEntries = HostPortUsage::entriesFor({stoppedOnly}, onlyDeclared);
    QCOMPARE(HostPortUsage::stateKeyForPort(onlyEntries, 20001), QStringLiteral("reserved"));
}


QTEST_MAIN(HostPortUsageTest)

#include "tst_host_port_usage.moc"
