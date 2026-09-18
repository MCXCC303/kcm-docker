/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 网络列表模型与过滤代理（ARCH_V5_V8 §3.2）。
 *
 * 与容器/镜像列表同一套约定，因此这里钉的是"同一套约定真的成立"：
 * 数据未变不重置模型（后台刷新不重建 delegate）、搜索覆盖名称/ID/驱动/子网、
 * 内置网络可单独过滤出来（用户通常只关心自己建的网络）、排序稳定。
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

    // host / none 没有子网：显示空而不是 "0.0.0.0/0"
    QVERIFY(model.index(1, 0).data(NetworkModel::SubnetRole).toString().isEmpty());

    // 按 Id（含短 Id）找行：详情页导航要用
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
    QCOMPARE(resetSpy.count(), 0); // 内容未变：delegate 不该被销毁重建

    // 值变化（成员列表清了）：只发 dataChanged，**不重置模型**——
    // 重置会让 ListView 跳回顶部（用户实测的体验问题）
    QList<Network> changed = sampleNetworks();
    changed[3].members.clear();
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    model.setNetworks(changed);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 1);

    model.clear();
    QVERIFY(model.empty());
    QCOMPARE(resetSpy.count(), 0); // 清空是"逐行删除"，不是整表重置
}

void NetworkModelTest::searchesAcrossNameIdDriverAndSubnet()
{
    NetworkModel model;
    model.setNetworks(sampleNetworks());
    NetworkFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.count(), 4);

    // 默认按名称升序（app_default / bridge / host / none）
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));
    QCOMPARE(filter.index(3, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("none"));

    filter.setSearchText(QStringLiteral("APP_DEFAULT")); // 大小写不敏感
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("app_default"));

    filter.setSearchText(QStringLiteral("172.17"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("bridge"));

    filter.setSearchText(QString(12, QLatin1Char('2')));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(NetworkModel::NameRole).toString(), QStringLiteral("host"));

    // 驱动搜索：null 只有 none 网络
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

    // 过滤与搜索可组合
    filter.setSearchText(QStringLiteral("app"));
    QCOMPARE(filter.count(), 1);

    filter.setOriginFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 1);

    // 后台刷新不会重置用户条件（§32）
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
    // 成员数降序：app_default(2) 在最前
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

    // 读失败：低频数据保留上一次的列表，只把状态标成失败（界面提示，不突然空掉）
    backend.setNextFailure(DockerBackendInterface::Section::Networks,
                           DockerError(DockerError::Kind::DockerUnavailable, QStringLiteral("socket gone")));
    backend.refreshNetworks();
    backend.completeRefresh();

    QCOMPARE(controller.networkModel()->count(), 4);
    // 状态是 error，但**列表内容保留**——这正是"低频数据读失败不突然空掉"的约定
    QCOMPARE(controller.networksStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.networksError().isEmpty());
}

QTEST_MAIN(NetworkModelTest)

#include "tst_network_model.moc"
