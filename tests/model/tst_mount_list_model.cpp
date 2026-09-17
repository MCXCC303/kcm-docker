/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/container_detail_controller.h"
#include "model/mount_list_model.h"
#include "support/fake_host_path_service.h"
#include "support/mock_docker_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * 挂载分区（ARCH_V4 §2.1.1 / §5.1）。
 *
 * 覆盖三件事：
 *  - domain 挂载 → presentation 条目的字段映射（类型 / 模式 / 卷名 / 路径）
 *  - 宿主路径探测结果如何决定「能不能打开」
 *  - 打开失败必须给出可读原因，而不是静默
 */
class MountListModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void mapsMountFieldsToRoles();
    void probesHostPathThroughInjectedService();
    void tmpfsHasNoHostPath();
    void unnamedVolumeHasNoVolumeName();
    void unchangedMountsDoNotResetTheModel();
    void openMountHostPathAsksTheService();
    void openFailureIsReportedAndDismissable();
    void blockedMountsAreCounted();

private:
    ContainerDetail detailWithMounts(const QList<ContainerMount> &mounts);
};

void MountListModelTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    qRegisterMetaType<Kontainer::HostPathError>("Kontainer::HostPathError");
}

ContainerDetail MountListModelTest::detailWithMounts(const QList<ContainerMount> &mounts)
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.mounts = mounts;
    return detail;
}

void MountListModelTest::mapsMountFieldsToRoles()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;
    ContainerDetailController controller(&backend, &hostPaths);

    ContainerMount bind;
    bind.type = QStringLiteral("bind");
    bind.source = QStringLiteral("/srv/data");
    bind.destination = QStringLiteral("/data");
    bind.mode = QStringLiteral("rw");
    bind.readOnly = true; // 引擎的 RW=false 必须体现在 presentation 里
    backend.setContainerDetail(detailWithMounts({bind}));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.mounts()->count(), 1);
    const QModelIndex index = controller.mounts()->index(0, 0);
    QCOMPARE(index.data(MountListModel::TypeKeyRole).toString(), QStringLiteral("bind"));
    QCOMPARE(index.data(MountListModel::SourceRole).toString(), QStringLiteral("/srv/data"));
    QCOMPARE(index.data(MountListModel::DestinationRole).toString(), QStringLiteral("/data"));
    // 只读挂载显示 ro：这是用户判断「能不能写进去」的唯一线索
    QCOMPARE(index.data(MountListModel::ModeRole).toString(), QStringLiteral("ro"));
    QCOMPARE(index.data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("directory"));
    QVERIFY(index.data(MountListModel::OpenableRole).toBool());
}

void MountListModelTest::probesHostPathThroughInjectedService()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;

    ContainerMount bind;
    bind.type = QStringLiteral("bind");
    bind.source = QStringLiteral("/srv/gone");
    bind.destination = QStringLiteral("/data");
    backend.setContainerDetail(detailWithMounts({bind}));

    hostPaths.setState(HostPathState::Missing);
    ContainerDetailController missing(&backend, &hostPaths);
    missing.setContainerId(QStringLiteral("cid-1"));
    missing.start();
    backend.completeRefresh();
    QCOMPARE(missing.mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("missing"));
    QVERIFY2(!missing.mounts()->index(0, 0).data(MountListModel::OpenableRole).toBool(), "a missing path must not be openable");

    hostPaths.setState(HostPathState::NotADirectory);
    ContainerDetailController file(&backend, &hostPaths);
    file.setContainerId(QStringLiteral("cid-1"));
    file.start();
    backend.completeRefresh();
    QCOMPARE(file.mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("notADirectory"));
    QVERIFY(!file.mounts()->index(0, 0).data(MountListModel::OpenableRole).toBool());
}

void MountListModelTest::tmpfsHasNoHostPath()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;

    ContainerMount tmpfs;
    tmpfs.type = QStringLiteral("tmpfs");
    tmpfs.destination = QStringLiteral("/tmp/cache");
    backend.setContainerDetail(detailWithMounts({tmpfs}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    // tmpfs 没有宿主机路径：不谎报「缺失」，而是「不适用」
    QCOMPARE(controller.mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("notApplicable"));
    QVERIFY(!controller.mounts()->index(0, 0).data(MountListModel::OpenableRole).toBool());
    QCOMPARE(controller.mounts()->blockedCount(), 0);
}

void MountListModelTest::unnamedVolumeHasNoVolumeName()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;

    ContainerMount volume;
    volume.type = QStringLiteral("volume");
    volume.source = QStringLiteral("/var/lib/docker/volumes/abc/_data");
    volume.destination = QStringLiteral("/var/lib/data");
    volume.name = QStringLiteral("data-vol");
    backend.setContainerDetail(detailWithMounts({volume}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();
    QCOMPARE(controller.mounts()->index(0, 0).data(MountListModel::VolumeNameRole).toString(), QStringLiteral("data-vol"));
}

void MountListModelTest::unchangedMountsDoNotResetTheModel()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;
    backend.setContainerDetail(detailWithMounts({}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QSignalSpy resetSpy(controller.mounts(), &QAbstractItemModel::modelReset);
    // 数据没变：30 秒一次的 inspect 复核不允许重置模型
    // （重置会销毁重建 QML 里的行，正是 ARCH_V3 附录 A.1g 的段错误诱因）
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(resetSpy.count(), 0);
}

void MountListModelTest::openMountHostPathAsksTheService()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;

    ContainerMount bind;
    bind.type = QStringLiteral("bind");
    bind.source = QStringLiteral("/srv/data");
    bind.destination = QStringLiteral("/data");
    backend.setContainerDetail(detailWithMounts({bind}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    controller.openMountHostPath(0);
    QCOMPARE(hostPaths.openCount(), 1);
    QCOMPARE(hostPaths.openedPaths().first(), QStringLiteral("/srv/data"));
    QVERIFY(controller.mountActionError().isEmpty());

    // 越界索引不该崩，也不该触发打开
    controller.openMountHostPath(7);
    controller.openMountHostPath(-1);
    QCOMPARE(hostPaths.openCount(), 1);
}

void MountListModelTest::openFailureIsReportedAndDismissable()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;

    ContainerMount bind;
    bind.type = QStringLiteral("bind");
    bind.source = QStringLiteral("/srv/data");
    bind.destination = QStringLiteral("/data");
    backend.setContainerDetail(detailWithMounts({bind}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QSignalSpy errorSpy(&controller, &ContainerDetailController::mountActionErrorChanged);
    hostPaths.setNextOpenError(HostPathError::LaunchFailed);
    controller.openMountHostPath(0);

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY2(!controller.mountActionError().isEmpty(), "a failed open must be explainable");

    controller.dismissMountActionError();
    QVERIFY(controller.mountActionError().isEmpty());
}

void MountListModelTest::blockedMountsAreCounted()
{
    MockDockerBackend backend;
    FakeHostPathService hostPaths;
    hostPaths.setState(HostPathState::Missing);

    ContainerMount missing;
    missing.type = QStringLiteral("bind");
    missing.source = QStringLiteral("/srv/gone");
    missing.destination = QStringLiteral("/data");

    ContainerMount tmpfs;
    tmpfs.type = QStringLiteral("tmpfs");
    tmpfs.destination = QStringLiteral("/tmp/cache");

    backend.setContainerDetail(detailWithMounts({missing, tmpfs}));

    ContainerDetailController controller(&backend, &hostPaths);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    // 只有「本该有路径却打不开」的才算被挡住；tmpfs 天生没有宿主路径
    QCOMPARE(controller.mounts()->count(), 2);
    QCOMPARE(controller.mounts()->blockedCount(), 1);
}

QTEST_MAIN(MountListModelTest)

#include "tst_mount_list_model.moc"
