/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/volume_dto.h"
#include "model/status_controller.h"
#include "model/volume_filter_model.h"
#include "model/volume_model.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * Volume list (ARCH_V5_V8 §3.5).
 *
 * Three pins: **"unknown" is not 0** (no engine UsageData → size and ref count are -1, the UI shows "—",
 * because 0 would read as "safe to prune"); unused volumes filter separately with unknown usage **excluded**;
 * `/volumes` is an object (`{Volumes: [...] | null}`), null when the list is empty.
 */
class VolumeModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void parsesUsageAndUnknownUsage();
    void parsesNullVolumeList();
    void exposesRolesAndLooksUpByName();
    void unchangedVolumesDoNotResetTheModel();
    void filtersUnusedAndInUseSeparately();
    void sortsBySizeAndRefs();
    void controllerWiresVolumesSection();

private:
    static QList<Volume> sampleVolumes();
};

namespace
{
Volume makeVolume(const QString &name, const QString &driver, qint64 sizeBytes, int refCount, bool usageKnown = true)
{
    Volume volume;
    volume.name = name;
    volume.driver = driver;
    volume.mountpoint = QStringLiteral("/var/lib/docker/volumes/%1/_data").arg(name);
    volume.createdAt = QDateTime::currentDateTimeUtc().addSecs(-3600);
    volume.scope = QStringLiteral("local");
    volume.sizeBytes = usageKnown ? sizeBytes : -1;
    volume.refCount = usageKnown ? refCount : -1;
    return volume;
}
} // namespace

QList<Volume> VolumeModelTest::sampleVolumes()
{
    return {
        makeVolume(QStringLiteral("app_data"), QStringLiteral("local"), 4096, 2),
        makeVolume(QStringLiteral("cache"), QStringLiteral("local"), 1024 * 1024, 0),
        makeVolume(QStringLiteral("nfs_share"), QStringLiteral("nfs"), 0, 1),
        makeVolume(QStringLiteral("legacy"), QStringLiteral("local"), 0, 0, /*usageKnown=*/false),
    };
}

void VolumeModelTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    qRegisterMetaType<Kontainer::Volume>("Kontainer::Volume");
    qRegisterMetaType<QList<Kontainer::Volume>>("QList<Kontainer::Volume>");
}

void VolumeModelTest::parsesUsageAndUnknownUsage()
{
    const QByteArray payload = R"({
        "Volumes": [
            {
                "Name": "app_data",
                "Driver": "local",
                "Mountpoint": "/var/lib/docker/volumes/app_data/_data",
                "CreatedAt": "2026-09-16T17:44:09.395242226+08:00",
                "Scope": "local",
                "Labels": {"com.example.owner": "team-a"},
                "Options": {"type": "none", "device": "tmpfs"},
                "UsageData": {"Size": 4096, "RefCount": 2}
            },
            {
                "Name": "unknown_usage",
                "Driver": "local",
                "Mountpoint": "/var/lib/docker/volumes/unknown_usage/_data"
            }
        ],
        "Warnings": ["volume driver nfs is not available"]
    })";

    QString error;
    QStringList warnings;
    int skipped = 0;
    const QList<Volume> volumes = volumesFromDto(DockerVolumeDTO::listFromPayload(payload, &warnings, &error, &skipped));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(skipped, 0);
    QCOMPARE(warnings.size(), 1); // engine warnings must surface, never be swallowed
    QCOMPARE(volumes.size(), 2);

    const Volume &used = volumes.at(0);
    QCOMPARE(used.name, QStringLiteral("app_data"));
    QVERIFY(used.sizeKnown());
    QCOMPARE(used.sizeBytes, 4096);
    QVERIFY(used.isInUse());
    QCOMPARE(used.refCount, 2);
    QVERIFY(used.createdAt.isValid());
    QCOMPARE(used.labels.size(), 1);
    QCOMPARE(used.options.first().first, QStringLiteral("device")); // sorted by key

    // No UsageData means unknown, not 0
    const Volume &unknown = volumes.at(1);
    QVERIFY2(!unknown.sizeKnown(), "missing UsageData must stay unknown");
    QVERIFY2(!unknown.usageKnown(), "missing RefCount must stay unknown");
    QVERIFY2(!unknown.isInUse(), "unknown usage is not 'in use'");
    QCOMPARE(unknown.sizeBytes, -1);
    QCOMPARE(unknown.refCount, -1);
}

void VolumeModelTest::parsesNullVolumeList()
{
    // Observed locally: with no volumes, Volumes is null (not an empty array)
    QString error;
    int skipped = 0;
    const QList<Volume> volumes = volumesFromDto(DockerVolumeDTO::listFromPayload(QByteArrayLiteral("{\"Volumes\":null,\"Warnings\":null}"),
                                                                                 nullptr, &error, &skipped));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(volumes.isEmpty());

    // Bad payload: not an object → error
    error.clear();
    QVERIFY(DockerVolumeDTO::listFromPayload(QByteArrayLiteral("[]"), nullptr, &error, nullptr).isEmpty());
    QVERIFY(!error.isEmpty());

    // Bad entries are skipped, good ones kept
    const QByteArray mixed = R"({"Volumes": [{"Name": "good"}, {"Driver": "local"}, "not-an-object"]})";
    skipped = 0;
    const QList<Volume> parsed = volumesFromDto(DockerVolumeDTO::listFromPayload(mixed, nullptr, nullptr, &skipped));
    QCOMPARE(skipped, 2);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().name, QStringLiteral("good"));
    QCOMPARE(parsed.first().driver, QStringLiteral("local")); // missing driver defaults to local
}

void VolumeModelTest::exposesRolesAndLooksUpByName()
{
    VolumeModel model;
    QVERIFY(model.empty());
    model.setVolumes(sampleVolumes());
    QCOMPARE(model.count(), 4);

    const QModelIndex first = model.index(0, 0);
    QCOMPARE(first.data(VolumeModel::NameRole).toString(), QStringLiteral("app_data"));
    QCOMPARE(first.data(VolumeModel::DriverRole).toString(), QStringLiteral("local"));
    QVERIFY(first.data(VolumeModel::MountpointRole).toString().contains(QStringLiteral("app_data")));
    QCOMPARE(first.data(VolumeModel::SizeBytesRole).toLongLong(), 4096);
    QVERIFY(first.data(VolumeModel::SizeKnownRole).toBool());
    QVERIFY(first.data(VolumeModel::InUseRole).toBool());

    const QModelIndex unknown = model.index(3, 0);
    QCOMPARE(unknown.data(VolumeModel::NameRole).toString(), QStringLiteral("legacy"));
    QVERIFY2(!unknown.data(VolumeModel::UsageKnownRole).toBool(), "the fixture marks this one as unknown");
    QCOMPARE(unknown.data(VolumeModel::RefCountRole).toInt(), -1);

    QCOMPARE(model.rowForName(QStringLiteral("cache")), 1);
    QCOMPARE(model.rowForName(QStringLiteral("nope")), -1);
    QCOMPARE(model.names().size(), 4);
    // Prune targets: **only** certainly unused ones (unknown excluded)
    QCOMPARE(model.unusedNames(), QStringList {QStringLiteral("cache")});
    QVERIFY(model.summaries().first().toMap().contains(QStringLiteral("mountpoint")));
}

void VolumeModelTest::unchangedVolumesDoNotResetTheModel()
{
    VolumeModel model;
    model.setVolumes(sampleVolumes());

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setVolumes(sampleVolumes());
    QCOMPARE(resetSpy.count(), 0); // unchanged content: no delegate rebuild

    // Value change (ref count): dataChanged only, no reset (same UX fix as the container list)
    QList<Volume> changed = sampleVolumes();
    changed[1].refCount = 3;
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    model.setVolumes(changed);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 1);

    model.clear();
    QVERIFY(model.empty());
    QCOMPARE(resetSpy.count(), 0); // clear removes rows one by one, it is not a model reset
}

void VolumeModelTest::filtersUnusedAndInUseSeparately()
{
    VolumeModel model;
    model.setVolumes(sampleVolumes());
    VolumeFilterModel filter;
    filter.setSourceModel(&model);

    QCOMPARE(filter.count(), 4);

    // Unused: only cache (legacy's ref count is unknown, so it is not "prunable")
    filter.setUsageFilter(QStringLiteral("unused"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(VolumeModel::NameRole).toString(), QStringLiteral("cache"));

    filter.setUsageFilter(QStringLiteral("inUse"));
    QCOMPARE(filter.count(), 2); // app_data / nfs_share

    filter.setUsageFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 4);

    // Search covers name / driver / mountpoint
    filter.setSearchText(QStringLiteral("NFS"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(VolumeModel::NameRole).toString(), QStringLiteral("nfs_share"));

    filter.setSearchText(QStringLiteral("/var/lib/docker/volumes/cache"));
    QCOMPARE(filter.count(), 1);

    filter.setSearchText(QString());
    // A background refresh does not reset user criteria
    filter.setUsageFilter(QStringLiteral("unused"));
    model.setVolumes(sampleVolumes());
    QCOMPARE(filter.usageFilter(), QStringLiteral("unused"));
    QCOMPARE(filter.count(), 1);
}

void VolumeModelTest::sortsBySizeAndRefs()
{
    VolumeModel model;
    model.setVolumes(sampleVolumes());
    VolumeFilterModel filter;
    filter.setSourceModel(&model);

    filter.setSortKey(QStringLiteral("size"));
    QCOMPARE(filter.index(0, 0).data(VolumeModel::NameRole).toString(), QStringLiteral("cache")); // largest

    filter.setSortKey(QStringLiteral("refs"));
    QCOMPARE(filter.index(0, 0).data(VolumeModel::NameRole).toString(), QStringLiteral("app_data")); // refs=2

    filter.setSortKey(QStringLiteral("name"));
    QCOMPARE(filter.index(0, 0).data(VolumeModel::NameRole).toString(), QStringLiteral("app_data"));
}

void VolumeModelTest::controllerWiresVolumesSection()
{
    MockDockerBackend backend;
    backend.setVolumes(sampleVolumes());
    StatusController controller(&backend);

    QSignalSpy stateSpy(&controller, &StatusController::volumesStateChanged);
    controller.refreshVolumes();
    backend.completeRefresh();

    QCOMPARE(controller.volumeModel()->count(), 4);
    QCOMPARE(controller.volumeList()->count(), 4);
    QVERIFY(controller.volumesError().isEmpty());
    QVERIFY2(stateSpy.count() >= 1, "the volume list state must be published to the UI");
    QCOMPARE(controller.volumesStateKey(), QStringLiteral("ready"));
    QVERIFY(backend.lastVolumesRefreshUsedUsage());

    // Usage stats can be skipped when only names are needed (that step is slow on big hosts)
    controller.refreshVolumes(false);
    backend.completeRefresh();
    QVERIFY(!backend.lastVolumesRefreshUsedUsage());

    // Read failure: keep the last list, only mark the state failed
    backend.setNextFailure(DockerBackendInterface::Section::Volumes,
                           DockerError(DockerError::Kind::DockerUnavailable, QStringLiteral("socket gone")));
    controller.refreshVolumes();
    backend.completeRefresh();
    QCOMPARE(controller.volumeModel()->count(), 4);
    QCOMPARE(controller.volumesStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.volumesError().isEmpty());
}

QTEST_MAIN(VolumeModelTest)

#include "tst_volume_model.moc"
