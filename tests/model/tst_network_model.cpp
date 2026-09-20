/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/network_filter_model.h"
#include "model/network_model.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * Network list model and filter proxy (ARCH_V5_V8 §3.2).
 *
 * Same contract as the container/image lists, so this pins that it really holds:
 * unchanged data does not reset the model (refresh must not rebuild delegates), search covers
 * name/id/driver/subnet, predefined networks filter separately (users care about their own), order is stable.
 */
class NetworkModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void exposesListRoles();
    void unchangedNetworksDoNotResetTheModel();
    void searchesAcrossNameIdDriverAndSubnet();
    void filtersPredefinedNetworks();
    void sortsByNameAndMembers();
    void controllerWiresNetworksSection();
    void controllerKeepsListWhenRefreshFails();

private:
    static QList<Network> sampleNetworks();
};

namespace
{
Network makeNetwork(const QString &id, const QString &name, const QString &driver, const QString &subnet, int members)
{
    Network network;
    network.id = id;
    network.name = name;
    network.driver = driver;
    network.scope = QStringLiteral("local");
    network.created = QDateTime::currentDateTimeUtc();
    if (!subnet.isEmpty()) {
        network.ipamConfigs.append({subnet, QStringLiteral("172.17.0.1")});
    }
    for (int i = 0; i < members; ++i) {
        NetworkMember member;
        member.containerId = QString(64, QLatin1Char('a'));
        member.name = QStringLiteral("container-%1").arg(i);
        member.ipv4Address = QStringLiteral("172.18.0.%1").arg(i + 2);
        network.members.append(member);
    }
    return network;
}
} // namespace

QList<Network> NetworkModelTest::sampleNetworks()
{
    return {
        makeNetwork(QString(64, QLatin1Char('1')), QStringLiteral("bridge"), QStringLiteral("bridge"), QStringLiteral("172.17.0.0/16"), 0),
        makeNetwork(QString(64, QLatin1Char('2')), QStringLiteral("host"), QStringLiteral("host"), QString(), 0),
        makeNetwork(QString(64, QLatin1Char('3')), QStringLiteral("none"), QStringLiteral("null"), QString(), 0),
        makeNetwork(QString(64, QLatin1Char('4')), QStringLiteral("app_default"), QStringLiteral("bridge"), QStringLiteral("172.18.0.0/16"), 2),
    };
}

void NetworkModelTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    qRegisterMetaType<Kontainer::Network>("Kontainer::Network");
    qRegisterMetaType<QList<Kontainer::Network>>("QList<Kontainer::Network>");
}

void NetworkModelTest::exposesListRoles()
{
    NetworkModel model;
    QVERIFY(model.empty());
    QCOMPARE(model.count(), 0);

    model.setNetworks(sampleNetworks());
    QCOMPARE(model.count(), 4);
    QVERIFY(!model.empty());

    const QModelIndex bridge = model.index(0, 0);
    QCOMPARE(bridge.data(NetworkModel::NameRole).toString(), QStringLiteral("bridge"));
    QCOMPARE(bridge.data(NetworkModel::ShortIdRole).toString(), QString(12, QLatin1Char('1')));
    QCOMPARE(bridge.data(NetworkModel::DriverRole).toString(), QStringLiteral("bridge"));
    QCOMPARE(bridge.data(NetworkModel::SubnetRole).toString(), QStringLiteral("172.17.0.0/16"));
    QVERIFY2(bridge.data(NetworkModel::PredefinedRole).toBool(), "bridge/host/none are pre-defined");
    QCOMPARE(bridge.data(NetworkModel::MemberCountRole).toInt(), 0);

    const QModelIndex compose = model.index(3, 0);
    QVERIFY2(!compose.data(NetworkModel::PredefinedRole).toBool(), "a compose network is not predefined");
    QCOMPARE(compose.data(NetworkModel::MemberCountRole).toInt(), 2);
    QCOMPARE(compose.data(NetworkModel::MembersRole).value<QList<NetworkMember>>().size(), 2);

    // host / none have no subnet: show empty, not "0.0.0.0/0"
    QVERIFY(model.index(1, 0).data(NetworkModel::SubnetRole).toString().isEmpty());

    // Row lookup by id (short id too): the detail page navigates with it
    QCOMPARE(model.rowForId(QString(64, QLatin1Char('4'))), 3);
    QCOMPARE(model.rowForId(QString(12, QLatin1Char('4'))), 3);
    QCOMPARE(model.rowForId(QStringLiteral("does-not-exist")), -1);
}

void NetworkModelTest::unchangedNetworksDoNotResetTheModel()
{
    NetworkModel model;
    model.setNetworks(sampleNetworks());

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setNetworks(sampleNetworks());
    QCOMPARE(resetSpy.count(), 0); // unchanged content: no delegate destruction/re-creation

    // Value change (members cleared): dataChanged only, **no model reset** —
    // a reset makes the ListView jump back to the top (seen in real use)
    QList<Network> changed = sampleNetworks();
    changed[3].members.clear();
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    model.setNetworks(changed);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 1);

    model.clear();
    QVERIFY(model.empty());
    QCOMPARE(resetSpy.count(), 0); // clear removes rows one by one, it is not a model reset
}

void NetworkModelTest::searchesAcrossNameIdDriverAndSubnet()
{
    NetworkModel model;
    model.setNetworks(sampleNetworks());
    NetworkFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.count(), 4);

    // Default sort is name ascending (app_default / bridge / host / none)
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));
    QCOMPARE(filter.index(3, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("none"));

    filter.setSearchText(QStringLiteral("APP_DEFAULT")); // case-insensitive
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));

    filter.setSearchText(QStringLiteral("172.17"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("bridge"));

    filter.setSearchText(QString(12, QLatin1Char('2')));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("host"));

    // Driver search: "null" matches only the none network
    filter.setSearchText(QStringLiteral("null"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("none"));

    filter.setSearchText(QString());
    QCOMPARE(filter.count(), 4);
}

void NetworkModelTest::filtersPredefinedNetworks()
{
    NetworkModel model;
    model.setNetworks(sampleNetworks());
    NetworkFilterModel filter;
    filter.setSourceModel(&model);

    filter.setOriginFilter(QStringLiteral("predefined"));
    QCOMPARE(filter.count(), 3);

    filter.setOriginFilter(QStringLiteral("custom"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));

    // Filter and search combine
    filter.setSearchText(QStringLiteral("app"));
    QCOMPARE(filter.count(), 1);

    filter.setOriginFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 1);

    // A background refresh does not reset user criteria (§32)
    model.setNetworks(sampleNetworks());
    QCOMPARE(filter.searchText(), QStringLiteral("app"));
    QCOMPARE(filter.count(), 1);
}

void NetworkModelTest::sortsByNameAndMembers()
{
    NetworkModel model;
    model.setNetworks(sampleNetworks());
    NetworkFilterModel filter;
    filter.setSourceModel(&model);

    filter.setSortKey(QStringLiteral("members"));
    // Member count descending: app_default(2) first
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));

    filter.setSortKey(QStringLiteral("driver"));
    const int firstRow = 0;
    QVERIFY(!filter.index(firstRow, 0).data(NetworkModel::DriverRole).toString().isEmpty());

    filter.setSortKey(QStringLiteral("name"));
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));
}

void NetworkModelTest::controllerWiresNetworksSection()
{
    MockDockerBackend backend;
    backend.setNetworks(sampleNetworks());
    StatusController controller(&backend);

    QSignalSpy stateSpy(&controller, &StatusController::networksStateChanged);
    controller.refreshNetworks();
    backend.completeRefresh();

    QCOMPARE(controller.networkModel()->count(), 4);
    QCOMPARE(controller.networkList()->count(), 4);
    QVERIFY(controller.networksError().isEmpty());
    QVERIFY2(stateSpy.count() >= 1, "the networks list state must be published to the UI");
    QCOMPARE(controller.networksStateKey(), QStringLiteral("ready"));
}

void NetworkModelTest::controllerKeepsListWhenRefreshFails()
{
    MockDockerBackend backend;
    backend.setNetworks(sampleNetworks());
    StatusController controller(&backend);

    controller.refreshNetworks();
    backend.completeRefresh();
    QCOMPARE(controller.networkModel()->count(), 4);

    // Read failure: keep the last list, only mark the state failed (banner, not a sudden empty view)
    backend.setNextFailure(DockerBackendInterface::Section::Networks,
                           DockerError(DockerError::Kind::DockerUnavailable, QStringLiteral("socket gone")));
    backend.refreshNetworks();
    backend.completeRefresh();

    QCOMPARE(controller.networkModel()->count(), 4);
    // State is error but the rows survive: that is the "no sudden empty on failed read" rule
    QCOMPARE(controller.networksStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.networksError().isEmpty());
}

QTEST_MAIN(NetworkModelTest)

#include "tst_network_model.moc"
