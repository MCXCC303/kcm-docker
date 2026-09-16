/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/container_model.h"
#include "model/state_text.h"
#include "model/image_model.h"

#include <QtTest>

using namespace Kontainer;

/*! 状态映射与 Qt model 测试（ARCH_V1 §13/§28 State mapping）。 */
class ContainerModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void mapsContainerStates_data();
    void mapsContainerStates();

    void mapsHealthStates_data();
    void mapsHealthStates();

    void unknownStateIsNotLost();
    void containerModelExposesRoles();
    void containerModelFormatsPortsSummary();
    void containerModelShortId();
    void imageModelExposesRoles();
    void emptyModelsHaveZeroCount();
    void identicalDataDoesNotResetModel();
};

void ContainerModelTest::initTestCase()
{
    setupTranslationDomain();
}

void ContainerModelTest::mapsContainerStates_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<int>("expected");
    QTest::addColumn<QString>("key");

    QTest::newRow("created") << QStringLiteral("created") << int(ContainerState::Created) << QStringLiteral("created");
    QTest::newRow("restarting") << QStringLiteral("restarting") << int(ContainerState::Restarting) << QStringLiteral("restarting");
    QTest::newRow("running") << QStringLiteral("running") << int(ContainerState::Running) << QStringLiteral("running");
    QTest::newRow("removing") << QStringLiteral("removing") << int(ContainerState::Removing) << QStringLiteral("removing");
    QTest::newRow("paused") << QStringLiteral("paused") << int(ContainerState::Paused) << QStringLiteral("paused");
    QTest::newRow("exited") << QStringLiteral("exited") << int(ContainerState::Exited) << QStringLiteral("exited");
    QTest::newRow("dead") << QStringLiteral("dead") << int(ContainerState::Dead) << QStringLiteral("dead");
    QTest::newRow("mixed case") << QStringLiteral("Running") << int(ContainerState::Running) << QStringLiteral("running");
}

void ContainerModelTest::mapsContainerStates()
{
    QFETCH(QString, raw);
    QFETCH(int, expected);
    QFETCH(QString, key);

    QCOMPARE(int(containerStateFromString(raw)), expected);
    QCOMPARE(containerStateKey(containerStateFromString(raw)), key);
    QVERIFY(!containerStateText(containerStateFromString(raw)).isEmpty());
}

void ContainerModelTest::mapsHealthStates_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<int>("expected");
    QTest::addColumn<QString>("key");

    QTest::newRow("none") << QStringLiteral("none") << int(HealthState::None) << QStringLiteral("none");
    QTest::newRow("starting") << QStringLiteral("starting") << int(HealthState::Starting) << QStringLiteral("starting");
    QTest::newRow("healthy") << QStringLiteral("healthy") << int(HealthState::Healthy) << QStringLiteral("healthy");
    QTest::newRow("unhealthy") << QStringLiteral("unhealthy") << int(HealthState::Unhealthy) << QStringLiteral("unhealthy");
    // 引擎未提供 Health 字段时必须是 Unknown（与 none 语义不同）
    QTest::newRow("missing") << QString() << int(HealthState::Unknown) << QStringLiteral("unknown");
}

void ContainerModelTest::mapsHealthStates()
{
    QFETCH(QString, raw);
    QFETCH(int, expected);
    QFETCH(QString, key);

    QCOMPARE(int(healthStateFromString(raw)), expected);
    QCOMPARE(healthStateKey(healthStateFromString(raw)), key);
}

void ContainerModelTest::unknownStateIsNotLost()
{
    // 未来 Docker 新增状态时不能崩溃，也不能丢掉这条记录
    QCOMPARE(int(containerStateFromString(QStringLiteral("hibernating"))), int(ContainerState::Unknown));
    QCOMPARE(containerStateKey(ContainerState::Unknown), QStringLiteral("unknown"));
}

void ContainerModelTest::containerModelExposesRoles()
{
    Container container;
    container.id = QStringLiteral("cdb609ac2a7cd0ddfc2c33cee1a71b8314052b645b9cdc2d1793fc5a8e8b2976");
    container.name = QStringLiteral("dl-medai");
    container.image = QStringLiteral("registry.example/practice:medai");
    container.status = QStringLiteral("Up 9 hours");
    container.state = ContainerState::Running;
    container.health = HealthState::Healthy;
    container.created = QDateTime::fromSecsSinceEpoch(1789552206, QTimeZone::UTC);
    container.ports.append(Port {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")});

    ContainerModel model;
    model.setContainers({container});

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, ContainerModel::NameRole).toString(), QStringLiteral("dl-medai"));
    QCOMPARE(model.data(index, ContainerModel::StateKeyRole).toString(), QStringLiteral("running"));
    QCOMPARE(model.data(index, ContainerModel::HealthKeyRole).toString(), QStringLiteral("healthy"));
    QCOMPARE(model.data(index, ContainerModel::StatusRole).toString(), QStringLiteral("Up 9 hours"));
    QCOMPARE(model.data(index, ContainerModel::ShortIdRole).toString(), QStringLiteral("cdb609ac2a7c"));
    QVERIFY(model.data(index, ContainerModel::CreatedRole).toDateTime().isValid());
    QCOMPARE(model.data(index, ContainerModel::PortCountRole).toInt(), 1);

    // role 名称必须稳定
    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(ContainerModel::NameRole), QByteArray("name"));
    QCOMPARE(roles.value(ContainerModel::StateKeyRole), QByteArray("stateKey"));
    QCOMPARE(roles.value(ContainerModel::PortsSummaryRole), QByteArray("portsSummary"));

    QCOMPARE(model.data(QModelIndex(), ContainerModel::NameRole).isValid(), false);
}

void ContainerModelTest::containerModelFormatsPortsSummary()
{
    Container container;
    container.id = QStringLiteral("abc");
    container.ports.append(Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")});
    container.ports.append(Port {QString(), 443, 0, QStringLiteral("tcp")});

    ContainerModel model;
    model.setContainers({container});

    const QString summary = model.data(model.index(0, 0), ContainerModel::PortsSummaryRole).toString();
    QVERIFY(summary.contains(QStringLiteral("8080→80/tcp")));
    QVERIFY(summary.contains(QStringLiteral("443/tcp")));
}

void ContainerModelTest::containerModelShortId()
{
    Container container;
    container.id = QStringLiteral("0123456789abcdef");
    QCOMPARE(container.shortId(), QStringLiteral("0123456789ab"));
}

void ContainerModelTest::imageModelExposesRoles()
{
    Image image;
    image.id = QStringLiteral("sha256:0cff9eb0e7aee9953e55bc682852ca4fdca233145a58ae1ec94f0b0c01a2ed30");
    image.repoTags = {QStringLiteral("ghcr.io/dockur/windows:6.05")};
    image.sizeBytes = 840000000;
    image.created = QDateTime::fromSecsSinceEpoch(1789557262, QTimeZone::UTC);
    image.containerCount = 1;
    image.inUse = true;

    ImageModel model;
    model.setImages({image});
    QCOMPARE(model.count(), 1);

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, ImageModel::PrimaryTagRole).toString(), QStringLiteral("ghcr.io/dockur/windows:6.05"));
    QCOMPARE(model.data(index, ImageModel::ShortIdRole).toString(), QStringLiteral("0cff9eb0e7ae"));
    QCOMPARE(model.data(index, ImageModel::SizeBytesRole).toLongLong(), Q_INT64_C(840000000));
    QCOMPARE(model.data(index, ImageModel::InUseRole).toBool(), true);
    QCOMPARE(model.data(index, ImageModel::DanglingRole).toBool(), false);

    Image dangling;
    dangling.id = QStringLiteral("sha256:deadbeef");
    dangling.containerCount = -1;
    model.setImages({dangling});
    QCOMPARE(model.data(model.index(0, 0), ImageModel::DanglingRole).toBool(), true);
    QCOMPARE(model.data(model.index(0, 0), ImageModel::InUseRole).toBool(), false);
}

void ContainerModelTest::emptyModelsHaveZeroCount()
{
    ContainerModel containers;
    ImageModel images;
    QCOMPARE(containers.count(), 0);
    QCOMPARE(images.count(), 0);
    QCOMPARE(containers.rowCount(), 0);
    QCOMPARE(images.rowCount(), 0);
}

/*!
 * §32/§34：后台刷新如果数据没有变化，就不应该重置模型
 * （否则每 5 秒列表都会重建、滚动位置丢失）。
 */
void ContainerModelTest::identicalDataDoesNotResetModel()
{
    Container first;
    first.id = QStringLiteral("aaa");
    first.name = QStringLiteral("first");
    first.status = QStringLiteral("Up 1 hour");
    first.state = ContainerState::Running;

    ContainerModel model;
    model.setContainers({first});

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setContainers({first});
    QCOMPARE(resetSpy.count(), 0); // 数据相同 → 不发信号

    first.status = QStringLiteral("Up 2 hours");
    model.setContainers({first});
    QCOMPARE(resetSpy.count(), 1); // 数据变化 → 正常重置
    QCOMPARE(model.data(model.index(0, 0), ContainerModel::StatusRole).toString(), QStringLiteral("Up 2 hours"));
}

QTEST_GUILESS_MAIN(ContainerModelTest)

#include "tst_container_model.moc"
