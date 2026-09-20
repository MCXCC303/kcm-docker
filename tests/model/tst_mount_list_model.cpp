/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Mount section (ARCH_V4 §2.1.1 / §5.1).
 *
 * Covers three things:
 *  - domain mount -> presentation entry field mapping (type / mode / volume name / paths)
 *  - how the host path probe decides whether a mount "can be opened"
 *  - a failed open must give a readable reason instead of failing silently
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
    bind.readOnly = true; // the engine's RW=false must show up in the presentation
    backend.setContainerDetail(detailWithMounts({bind}));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.mounts()->count(), 1);
    const QModelIndex index = controller.mounts()->index(0, 0);
    QCOMPARE(index.data(MountListModel::TypeKeyRole).toString(), QStringLiteral("bind"));
    QCOMPARE(index.data(MountListModel::SourceRole).toString(), QStringLiteral("/srv/data"));
    QCOMPARE(index.data(MountListModel::DestinationRole).toString(), QStringLiteral("/data"));
    // A read-only mount shows ro: the only clue whether the user can write into it
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

    // tmpfs has no host path: report "not applicable", not a bogus "missing"
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
    // Unchanged data: the 30-second inspect re-check must not reset the model
    // (a reset destroys and recreates the QML rows, the segfault trigger of ARCH_V3 Appendix A.1g)
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

    // An out-of-range index must neither crash nor trigger an open
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

    // Only "should have a path but cannot be opened" counts as blocked; tmpfs never has one
    QCOMPARE(controller.mounts()->count(), 2);
    QCOMPARE(controller.mounts()->blockedCount(), 1);
}

QTEST_MAIN(MountListModelTest)

#include "tst_mount_list_model.moc"
