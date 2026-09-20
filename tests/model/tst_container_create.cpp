/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container.h"
#include "domain/container_create_request.h"
#include "model/container_detail_controller.h"
#include "model/command_history_store.h"
#include "model/create_container_controller.h"
#include "model/mount_preset_store.h"
#include "model/operation_controller.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * Create-container form -> Docker API mapping (ARCH_V5_V8 §4.4).
 *
 * This is where "looks right, sends the wrong request" happens most easily, so the JSON is pinned
 * with snapshot assertions: field names, nesting, units (NanoCpus is billionths of a core) and the
 * counter-intuitive bits (`name` is a query parameter, a random port is an empty string, tmpfs
 * never goes into Binds).
 */
class ContainerCreateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void portRowStatusesReportHoldersAndSuggestFreePorts();
    void duplicateHostPortsInOneRequestAreRejected();

    void mapsFormFieldsToTheCreatePayload();
    void interactiveFlagsDefaultOnAndReachThePayload();
    void omitsEmptyFieldsAndKeepsDockerDefaults();
    void nameGoesIntoTheQueryNotTheBody();
    void tmpfsGoesToTmpfsNotBinds();
    void validatesNamesPathsKeysAndLimits();
    void presetStorePersistsAndOrders();
    void wizardGatesSteps();
    void wizardBuildsTheRequestAndSubmits();
    void cloneCopiesTheFullConfiguration();
    void suggestsNamesFromTheImageAndOccupancy();
    void commandHistoryRecordsAndMerges();
    void presetStoreDeduplicatesAndTrimsRecents();
};

void ContainerCreateTest::initTestCase()
{
    setupTranslationDomain();
}

void ContainerCreateTest::mapsFormFieldsToTheCreatePayload()
{
    ContainerCreateRequest request;
    request.name = QStringLiteral("web");
    request.image = QStringLiteral("alpine:3.19");
    request.command = {QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("sleep 1")};
    request.entrypoint = {QStringLiteral("/entry.sh")};
    request.environment = {QStringLiteral("LANG=C"), QStringLiteral("TZ=UTC")};
    request.labels.append({QStringLiteral("com.example.owner"), QStringLiteral("team-a")});
    request.workingDirectory = QStringLiteral("/app");
    request.user = QStringLiteral("1000:1000");
    request.hostname = QStringLiteral("web-1");
    request.ports = {{QStringLiteral("0.0.0.0"), 8080, 80, QStringLiteral("tcp")}};
    request.mounts = {{QStringLiteral("bind"), QStringLiteral("/srv/data"), QStringLiteral("/data"), true},
                      {QStringLiteral("volume"), QStringLiteral("app_cache"), QStringLiteral("/cache"), false}};
    request.network = QStringLiteral("app_default");
    request.networkAliases = {QStringLiteral("web")};
    request.restartPolicy = QStringLiteral("on-failure");
    request.restartMaxRetries = 3;
    request.memoryLimitBytes = 512LL * 1024 * 1024;
    request.cpus = 1.5;
    request.privileged = true;

    const QJsonObject payload = QJsonDocument::fromJson(request.toJson()).object();
    QCOMPARE(payload.value(QStringLiteral("Image")).toString(), QStringLiteral("alpine:3.19"));
    QCOMPARE(payload.value(QStringLiteral("Cmd")).toArray().size(), 3);
    QCOMPARE(payload.value(QStringLiteral("Cmd")).toArray().at(0).toString(), QStringLiteral("sh"));
    QCOMPARE(payload.value(QStringLiteral("Entrypoint")).toArray().at(0).toString(), QStringLiteral("/entry.sh"));
    QCOMPARE(payload.value(QStringLiteral("Env")).toArray().size(), 2);
    QCOMPARE(payload.value(QStringLiteral("Labels")).toObject().value(QStringLiteral("com.example.owner")).toString(),
             QStringLiteral("team-a"));
    QCOMPARE(payload.value(QStringLiteral("WorkingDir")).toString(), QStringLiteral("/app"));
    QCOMPARE(payload.value(QStringLiteral("User")).toString(), QStringLiteral("1000:1000"));
    QCOMPARE(payload.value(QStringLiteral("Hostname")).toString(), QStringLiteral("web-1"));

    // Container ports in port mappings must also appear in ExposedPorts (Docker semantics)
    QVERIFY(payload.value(QStringLiteral("ExposedPorts")).toObject().contains(QStringLiteral("80/tcp")));

    const QJsonObject hostConfig = payload.value(QStringLiteral("HostConfig")).toObject();
    const QJsonArray binds = hostConfig.value(QStringLiteral("Binds")).toArray();
    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.at(0).toString(), QStringLiteral("/srv/data:/data:ro")); // read-only adds :ro
    QCOMPARE(binds.at(1).toString(), QStringLiteral("app_cache:/cache")); // a named volume is a Binds too

    const QJsonObject portBindings = hostConfig.value(QStringLiteral("PortBindings")).toObject();
    const QJsonArray binding = portBindings.value(QStringLiteral("80/tcp")).toArray();
    QCOMPARE(binding.size(), 1);
    QCOMPARE(binding.at(0).toObject().value(QStringLiteral("HostIp")).toString(), QStringLiteral("0.0.0.0"));
    QCOMPARE(binding.at(0).toObject().value(QStringLiteral("HostPort")).toString(), QStringLiteral("8080"));

    const QJsonObject policy = hostConfig.value(QStringLiteral("RestartPolicy")).toObject();
    QCOMPARE(policy.value(QStringLiteral("Name")).toString(), QStringLiteral("on-failure"));
    QCOMPARE(policy.value(QStringLiteral("MaximumRetryCount")).toInt(), 3);

    QCOMPARE(hostConfig.value(QStringLiteral("Memory")).toDouble(), double(512LL * 1024 * 1024));
    // CPU is expressed in billionths of a core: 1.5 cores = 1_500_000_000
    QCOMPARE(hostConfig.value(QStringLiteral("NanoCpus")).toDouble(), 1500000000.0);
    QVERIFY(hostConfig.value(QStringLiteral("Privileged")).toBool());

    const QJsonObject endpoints = payload.value(QStringLiteral("NetworkingConfig"))
                                      .toObject()
                                      .value(QStringLiteral("EndpointsConfig"))
                                      .toObject();
    QVERIFY(endpoints.contains(QStringLiteral("app_default")));
    QCOMPARE(endpoints.value(QStringLiteral("app_default")).toObject().value(QStringLiteral("Aliases")).toArray().at(0).toString(),
             QStringLiteral("web"));
}

/*!
 * Interactive capability (user-reported F1): the wizard defaults `-i -t` on and really sends them.
 *
 * The "container exits right after start" reported by users was caused by missing these two:
 * alpine's default command `/bin/sh` reads EOF and exits normally with neither stdin nor a TTY
 * (verified with a read-only inspect).
 */
void ContainerCreateTest::interactiveFlagsDefaultOnAndReachThePayload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;
    OperationController operations(&backend);
    CreateContainerController wizard(&operations, &presets, &backend);

    // Defaults: -i and -t on, stdin-once off
    QVERIFY2(wizard.openStdin(), "standard input must be enabled by default");
    QVERIFY2(wizard.tty(), "a TTY must be allocated by default");
    QVERIFY(!wizard.stdinOnce());

    // Defaults reach the request body (JSON snapshot)
    ContainerCreateRequest request;
    request.name = QStringLiteral("interactive");
    request.image = QStringLiteral("alpine:latest");
    request.openStdin = wizard.openStdin();
    request.tty = wizard.tty();
    request.stdinOnce = wizard.stdinOnce();
    QJsonObject payload = QJsonDocument::fromJson(request.toJson()).object();
    QCOMPARE(payload.value(QStringLiteral("OpenStdin")).toBool(), true);
    QCOMPARE(payload.value(QStringLiteral("Tty")).toBool(), true);
    QVERIFY2(!payload.contains(QStringLiteral("StdinOnce")), "stdin-once is off by default and must not be sent");

    // When off they are not written to JSON, so the engine uses its own defaults
    request.openStdin = false;
    request.tty = false;
    payload = QJsonDocument::fromJson(request.toJson()).object();
    QVERIFY(!payload.contains(QStringLiteral("OpenStdin")));
    QVERIFY(!payload.contains(QStringLiteral("Tty")));

    // Turning stdin-once on writes it in
    request.openStdin = true;
    request.stdinOnce = true;
    payload = QJsonDocument::fromJson(request.toJson()).object();
    QCOMPARE(payload.value(QStringLiteral("StdinOnce")).toBool(), true);
}

void ContainerCreateTest::omitsEmptyFieldsAndKeepsDockerDefaults()
{
    ContainerCreateRequest request;
    request.name = QStringLiteral("minimal");
    request.image = QStringLiteral("alpine:3.19");

    const QJsonObject payload = QJsonDocument::fromJson(request.toJson()).object();
    // Unset fields are NOT written to JSON: null or an empty array would change engine defaults
    QVERIFY(!payload.contains(QStringLiteral("Cmd")));
    QVERIFY(!payload.contains(QStringLiteral("Env")));
    QVERIFY(!payload.contains(QStringLiteral("HostConfig")));
    QVERIFY(!payload.contains(QStringLiteral("NetworkingConfig")));
    QVERIFY(!payload.contains(QStringLiteral("ExposedPorts")));

    // Random host port: Docker uses an EMPTY STRING for "assign one", not 0
    ContainerCreateRequest randomPort;
    randomPort.name = QStringLiteral("random-port");
    randomPort.image = QStringLiteral("alpine:3.19");
    randomPort.ports = {{QString(), 0, 80, QStringLiteral("tcp")}};
    const QJsonObject bindings = QJsonDocument::fromJson(randomPort.toJson())
                                     .object()
                                     .value(QStringLiteral("HostConfig"))
                                     .toObject()
                                     .value(QStringLiteral("PortBindings"))
                                     .toObject();
    QCOMPARE(bindings.value(QStringLiteral("80/tcp")).toArray().at(0).toObject().value(QStringLiteral("HostPort")).toString(),
             QString());
}

void ContainerCreateTest::nameGoesIntoTheQueryNotTheBody()
{
    ContainerCreateRequest request;
    request.name = QStringLiteral("web-1");
    request.image = QStringLiteral("alpine:3.19");

    // Counter-intuitive but essential: the container name is `?name=`, not a body field
    QCOMPARE(request.queryString(), QStringLiteral("name=web-1"));
    QVERIFY2(!QJsonDocument::fromJson(request.toJson()).object().contains(QStringLiteral("Name")),
             "the container name must not be sent in the body");
}

void ContainerCreateTest::tmpfsGoesToTmpfsNotBinds()
{
    ContainerCreateRequest request;
    request.name = QStringLiteral("tmpfs-demo");
    request.image = QStringLiteral("alpine:3.19");
    request.mounts = {{QStringLiteral("tmpfs"), QString(), QStringLiteral("/tmp"), false},
                      {QStringLiteral("bind"), QString(), QStringLiteral("/ignored"), false}}; // no source

    const QJsonObject hostConfig = QJsonDocument::fromJson(request.toJson()).object().value(QStringLiteral("HostConfig")).toObject();
    QVERIFY2(!hostConfig.contains(QStringLiteral("Binds")), "tmpfs and source-less mounts must not become binds");
    QVERIFY(hostConfig.value(QStringLiteral("Tmpfs")).toObject().contains(QStringLiteral("/tmp")));
}

void ContainerCreateTest::validatesNamesPathsKeysAndLimits()
{
    // Name
    QCOMPARE(validateContainerName(QString()), QStringLiteral("nameRequired"));
    QCOMPARE(validateContainerName(QStringLiteral("has space")), QStringLiteral("nameInvalid"));
    QCOMPARE(validateContainerName(QStringLiteral("-leading-dash")), QStringLiteral("nameInvalid"));
    QVERIFY(validateContainerName(QStringLiteral("web_1.2-3")).isEmpty());

    // Container paths must be absolute
    QCOMPARE(validateContainerPath(QString()), QStringLiteral("pathRequired"));
    QCOMPARE(validateContainerPath(QStringLiteral("data")), QStringLiteral("pathNotAbsolute"));
    QVERIFY(validateContainerPath(QStringLiteral("/data")).isEmpty());

    // Environment variable / label keys
    QCOMPARE(validateEnvironmentKey(QString()), QStringLiteral("keyRequired"));
    QCOMPARE(validateEnvironmentKey(QStringLiteral("1BAD")), QStringLiteral("keyInvalid"));
    QCOMPARE(validateEnvironmentKey(QStringLiteral("has-dash")), QStringLiteral("keyInvalid"));
    QVERIFY(validateEnvironmentKey(QStringLiteral("GOOD_KEY_1")).isEmpty());

    // Resource limits: 0 = unlimited; a real value must not be below Docker's 6 MiB
    QVERIFY(validateMemoryLimit(0).isEmpty());
    QCOMPARE(validateMemoryLimit(1024), QStringLiteral("memoryTooSmall"));
    QVERIFY(validateMemoryLimit(6 * 1024 * 1024).isEmpty());
    QCOMPARE(validateMemoryLimit(-1), QStringLiteral("memoryNegative"));
    QVERIFY(validateCpus(0.0).isEmpty());
    QCOMPARE(validateCpus(-0.5), QStringLiteral("cpusNegative"));
}

void ContainerCreateTest::presetStorePersistsAndOrders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("kcm_dockerrc"));

    QString firstId;
    QString secondId;
    {
        MountPresetStore store(path);
        QVERIFY(store.empty());
        firstId = store.add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true, QStringLiteral("数据目录"));
        secondId = store.add(QStringLiteral("app_cache"), QStringLiteral("/cache"), QStringLiteral("volume"));
        QVERIFY(!firstId.isEmpty());
        QVERIFY(!secondId.isEmpty());
        QCOMPARE(store.count(), 2);
        QVERIFY2(!store.add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true).isEmpty(),
                 "adding the same mount twice must not create a second entry");
        QCOMPARE(store.count(), 2);
        // Invalid input is rejected (host and container paths must be absolute)
        QVERIFY(store.add(QStringLiteral("relative/path"), QStringLiteral("/x")).isEmpty());
        QVERIFY(store.add(QStringLiteral("/srv/x"), QStringLiteral("relative")).isEmpty());

        QVERIFY(store.setFavorite(secondId, true));
        // Favourites sort first
        QCOMPARE(store.presets().first().id, secondId);
    }

    // Reopened: content and order must survive (persistence really wrote to kcm_dockerrc)
    MountPresetStore reopened(path);
    QCOMPARE(reopened.count(), 2);
    QCOMPARE(reopened.presets().first().id, secondId);
    QVERIFY(reopened.presets().first().favorite);
    const MountPreset bindPreset = reopened.presets().last();
    QCOMPARE(bindPreset.source, QStringLiteral("/srv/data"));
    QCOMPARE(bindPreset.destination, QStringLiteral("/data"));
    QVERIFY(bindPreset.readOnly);
    QCOMPARE(bindPreset.note, QStringLiteral("数据目录"));

    // Ordering: favourites first, then insertion order; moveDown swaps non-favourites
    QVERIFY(reopened.moveDown(secondId) == false || reopened.presets().size() == 2);
    QVERIFY(reopened.remove(firstId));
    QCOMPARE(reopened.count(), 1);
}

void ContainerCreateTest::presetStoreDeduplicatesAndTrimsRecents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore store(dir.filePath(QStringLiteral("kcm_dockerrc")));

    // After a successful create this request's mounts join "recently used"
    QList<ContainerMountRequest> mounts;
    mounts.append({QStringLiteral("bind"), QStringLiteral("/srv/app"), QStringLiteral("/app"), false});
    mounts.append({QStringLiteral("tmpfs"), QString(), QStringLiteral("/tmp"), false}); // tmpfs: no preset
    store.noteUsed(mounts);
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.presets().first().source, QStringLiteral("/srv/app"));
    QVERIFY(store.presets().first().lastUsedAt.isValid());

    // Using the same mount again adds nothing, it only refreshes the timestamp
    store.noteUsed(mounts);
    QCOMPARE(store.count(), 1);

    // Past the cap the oldest recent entries go, but favourites and never-used presets stay
    const QString favoriteId = store.presets().first().id;
    QVERIFY(store.setFavorite(favoriteId, true));
    for (int i = 0; i < MountPresetStore::kMaxRecent + 5; ++i) {
        QList<ContainerMountRequest> batch;
        batch.append({QStringLiteral("bind"),
                      QStringLiteral("/srv/extra%1").arg(i),
                      QStringLiteral("/extra%1").arg(i),
                      false});
        store.noteUsed(batch);
    }
    QCOMPARE(store.count(), MountPresetStore::kMaxRecent);
    QVERIFY2(store.presets().first().id == favoriteId, "a favourite must survive trimming");
}

/*!
 * Wizard step validation (ARCH_V5_V8 §4.3): no step may be left while it still has problems.
 */
void ContainerCreateTest::wizardGatesSteps()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    backend.setImages({image});
    Container existing;
    existing.id = QStringLiteral("existing");
    existing.name = QStringLiteral("web");
    existing.image = QStringLiteral("alpine:3.19");
    // Only RUNNING containers really hold a host port (see OperationController::holdsHostPorts)
    existing.state = ContainerState::Running;
    existing.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};
    backend.setContainers({existing});
    OperationController operations(&backend);
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    operations.refreshWriteAccess();

    CreateContainerController wizard(&operations, &presets, &backend);
    // Step order: image -> basics -> environment -> interactive -> ports -> mounts -> resources -> summary
    QCOMPARE(CreateContainerController::stepKeys(),
             QStringList({QStringLiteral("image"),
                          QStringLiteral("basics"),
                          QStringLiteral("environment"),
                          QStringLiteral("interactive"),
                          QStringLiteral("ports"),
                          QStringLiteral("mounts"),
                          QStringLiteral("resources"),
                          QStringLiteral("summary")}));
    QCOMPARE(wizard.stepKey(), QStringLiteral("image"));
    QCOMPARE(wizard.stepCount(), 8);

    // (1) Image: first required, then must exist locally unless "pull if missing" is on
    QVERIFY(!wizard.nextStep());
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("imageRequired"));
    wizard.setImage(QStringLiteral("busybox:latest"));
    QVERIFY(!wizard.nextStep());
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("imageNotLocal"));
    wizard.setPullIfMissing(true);
    QVERIFY(wizard.nextStep());
    QCOMPARE(wizard.stepKey(), QStringLiteral("basics"));

    // (2) Basics: name rules + duplicates
    wizard.setName(QStringLiteral("bad name"));
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("nameInvalid"));
    wizard.setName(QStringLiteral("WEB"));
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("nameInUse"));
    wizard.setName(QStringLiteral("worker"));
    QVERIFY(wizard.nextStep());

    // (3) Environment and labels: key naming rules
    wizard.setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("1BAD")},
                                            {QStringLiteral("value"), QStringLiteral("x")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("keyInvalid"));
    wizard.setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("GOOD")},
                                            {QStringLiteral("value"), QStringLiteral("x")}}});
    QVERIFY(wizard.nextStep());

    // (4) Interactive: nothing blocking, straight on
    QCOMPARE(wizard.stepKey(), QStringLiteral("interactive"));
    QVERIFY(wizard.nextStep());

    // (5) Ports: container port required, host ports must not clash (0 = random, never clashes)
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 0},
                                     {QStringLiteral("hostPort"), 0}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portRequired"));
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80},
                                     {QStringLiteral("hostPort"), 8080}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portInUse"));
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80},
                                     {QStringLiteral("hostPort"), 0}}});
    QVERIFY(wizard.nextStep());

    // (6) Mounts: destination absolute and unique; the source format is checked (a bind must be absolute)
    wizard.setMountRows({QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                      {QStringLiteral("source"), QStringLiteral("relative")},
                                      {QStringLiteral("destination"), QStringLiteral("/data")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("sourceNotAbsolute"));
    wizard.setMountRows({QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                      {QStringLiteral("source"), QStringLiteral("/srv/data")},
                                      {QStringLiteral("destination"), QStringLiteral("data")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("pathNotAbsolute"));
    wizard.setMountRows({QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                      {QStringLiteral("source"), QStringLiteral("/srv/data")},
                                      {QStringLiteral("destination"), QStringLiteral("/data")}},
                         QVariantMap {{QStringLiteral("type"), QStringLiteral("volume")},
                                      {QStringLiteral("source"), QStringLiteral("cache")},
                                      {QStringLiteral("destination"), QStringLiteral("/data")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("destinationDuplicate"));
    wizard.setMountRows({QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                      {QStringLiteral("source"), QStringLiteral("/srv/data")},
                                      {QStringLiteral("destination"), QStringLiteral("/data")}}});
    QVERIFY(wizard.nextStep());

    // (7) Resources: memory floor and non-negative CPU
    wizard.setMemoryLimitBytes(1024);
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("memoryTooSmall"));
    wizard.setMemoryLimitBytes(0);
    wizard.setCpus(-1.0);
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("cpusNegative"));
    wizard.setCpus(0.0);
    QVERIFY(wizard.nextStep());

    // (8) Summary: it cannot be jumped to from above, and there is no "next" on it
    QCOMPARE(wizard.stepKey(), QStringLiteral("summary"));
    QVERIFY(wizard.onSummary());
    QVERIFY(!wizard.nextStep());
    // Jumping back is always allowed (the user must be able to change earlier choices)
    QVERIFY(wizard.goToStep(QStringLiteral("ports")));
    QCOMPARE(wizard.stepKey(), QStringLiteral("ports"));
    // Jumping forward validates step by step: the summary is unreachable from an unfilled step
    wizard.setImage(QString());
    wizard.goToStep(QStringLiteral("image"));
    QVERIFY2(!wizard.goToStep(QStringLiteral("summary")), "the wizard must not skip unfilled steps");
}

/*!
 * Summary content and submit (§4.4/§4.6): environment variables list keys only; submit hands the
 * form to the controller.
 */
void ContainerCreateTest::wizardBuildsTheRequestAndSubmits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    const QString presetId = presets.add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true, QStringLiteral("数据"));
    QVERIFY(!presetId.isEmpty());

    MockDockerBackend backend;
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    backend.setImages({image});
    OperationController operations(&backend);
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    operations.refreshWriteAccess();

    CreateContainerController wizard(&operations, &presets, &backend);
    wizard.reset(QStringLiteral("alpine:3.19"));
    QCOMPARE(wizard.image(), QStringLiteral("alpine:3.19"));
    QCOMPARE(wizard.stepKey(), QStringLiteral("image"));
    QVERIFY(wizard.nextStep());
    wizard.setName(QStringLiteral("worker"));
    wizard.setNetwork(QStringLiteral("app_default"));
    wizard.setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("SECRET_TOKEN")},
                                            {QStringLiteral("value"), QStringLiteral("super-secret")}}});
    wizard.setStartAfterCreate(true);
    QVERIFY(wizard.nextStep()); // ports (no ports)
    QVERIFY(wizard.nextStep()); // environment
    QVERIFY(wizard.nextStep()); // mounts

    // Quick add from a preset (an existing one is not added twice)
    QVERIFY(wizard.addMountFromPreset(presetId));
    QVERIFY(!wizard.addMountFromPreset(presetId));
    QCOMPARE(wizard.mountRows().size(), 1);
    QVERIFY(wizard.presets().size() >= 1);

    QVERIFY(wizard.nextStep()); // resources
    QVERIFY(wizard.nextStep()); // summary

    // Summary: environment variables list KEYS ONLY (a value may be a password)
    const QVariantList rows = wizard.summary();
    QStringList labels;
    QStringList values;
    for (const QVariant &entry : rows) {
        labels.append(entry.toMap().value(QStringLiteral("label")).toString());
        values.append(entry.toMap().value(QStringLiteral("value")).toString());
    }
    QVERIFY(labels.contains(QStringLiteral("Image")));
    QVERIFY(labels.contains(QStringLiteral("Network")));
    QVERIFY(labels.contains(QStringLiteral("Environment variables")));
    QVERIFY2(!values.join(QStringLiteral("|")).contains(QStringLiteral("super-secret")),
             "secret environment values must never appear in the summary");
    QVERIFY(values.join(QStringLiteral("|")).contains(QStringLiteral("SECRET_TOKEN")));

    // Submit: the write gate lives in the controller and this endpoint is read-only, so it fails
    QVERIFY(!wizard.submit());
    QVERIFY(!operations.resultText().isEmpty());
}

/*!
 * Clone (ARCH_V5_V8 §4.5): copies the CONFIGURATION, not runtime state; command, entrypoint,
 * environment, labels and restart policy exist only in inspect, so they must be carried over when
 * entering from a container detail page.
 */
void ContainerCreateTest::cloneCopiesTheFullConfiguration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;

    Container listed;
    listed.id = QStringLiteral("cid-1");
    listed.name = QStringLiteral("web");
    listed.image = QStringLiteral("registry.example.com/team/app:1.0");
    listed.state = ContainerState::Running;
    listed.ports = {{QStringLiteral("0.0.0.0"), 8080, 18080, QStringLiteral("tcp")}};
    backend.setContainers({listed});

    // Fields that exist only in the inspect response
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("web");
    detail.image = QStringLiteral("registry.example.com/team/app:1.0");
    detail.command = {QStringLiteral("node"), QStringLiteral("server.js")};
    detail.entrypoint = {QStringLiteral("/entry.sh")};
    detail.environment = {QStringLiteral("LANG=C"), QStringLiteral("TZ=UTC")};
    detail.labels = {{QStringLiteral("com.example.owner"), QStringLiteral("team-a")}};
    detail.workingDirectory = QStringLiteral("/app");
    detail.user = QStringLiteral("1000:1000");
    detail.restartPolicy = QStringLiteral("unless-stopped");
    backend.setContainerDetail(detail);

    OperationController operations(&backend);
    ContainerDetailController detailController(&backend);
    detailController.setContainerId(QStringLiteral("cid-1"));
    detailController.start();
    backend.completeRefresh(); // inspect is asynchronous: the detail must really be loaded first

    CreateContainerController wizard(&operations, &presets, &backend, &detailController);
    QVERIFY(wizard.prefillFromContainer(QStringLiteral("cid-1")));

    QCOMPARE(wizard.image(), QStringLiteral("registry.example.com/team/app:1.0"));
    QVERIFY2(wizard.name().startsWith(QStringLiteral("web-copy")), qPrintable(wizard.name()));
    QCOMPARE(wizard.commandText(), QStringLiteral("node\nserver.js"));
    QCOMPARE(wizard.entrypointText(), QStringLiteral("/entry.sh"));
    QCOMPARE(wizard.environmentRows().size(), 2);
    QCOMPARE(wizard.environmentRows().first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("LANG"));
    QCOMPARE(wizard.environmentRows().first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("C"));
    QCOMPARE(wizard.labelRows().size(), 1);
    QCOMPARE(wizard.workingDirectory(), QStringLiteral("/app"));
    QCOMPARE(wizard.user(), QStringLiteral("1000:1000"));
    QCOMPARE(wizard.restartPolicy(), QStringLiteral("unless-stopped"));
    QCOMPARE(wizard.portRows().size(), 1);
    QCOMPARE(wizard.portRows().first().toMap().value(QStringLiteral("hostPort")).toInt(), 18080);

    // Unknown container: returns false and leaves the current form untouched
    const QString previousName = wizard.name();
    QVERIFY(!wizard.prefillFromContainer(QStringLiteral("does-not-exist")));
    QCOMPARE(wizard.name(), previousName);
}

/*!
 * "Use suggested name" (user-reported item 3): with an empty name, derive a candidate from the image
 * and avoid names that are already taken.
 */
void ContainerCreateTest::suggestsNamesFromTheImageAndOccupancy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;
    Container taken;
    taken.id = QStringLiteral("taken");
    taken.name = QStringLiteral("alpine-1111");
    backend.setContainers({taken});
    OperationController operations(&backend);
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    operations.refreshWriteAccess();

    CreateContainerController wizard(&operations, &presets, &backend);

    // No image yet: a usable candidate (`container-xxxx`) is still offered, so the button is always
    // enabled, but the image stays required for an actual create
    const QString fallback = wizard.suggestedName();
    QVERIFY2(!fallback.isEmpty(), "a suggestion must always be available");
    QVERIFY(validateContainerName(fallback).isEmpty());

    // Image but no name: docker-style candidate (drop the repository prefix and tag, illegal chars -> -)
    wizard.setImage(QStringLiteral("registry.example.com/team/My App:1.0"));
    const QString first = wizard.suggestedName();
    QVERIFY2(!first.isEmpty(), "a suggestion must be available as soon as an image is chosen");
    QVERIFY2(first.startsWith(QStringLiteral("my-app-")), qPrintable(first));
    QVERIFY2(!first.contains(QLatin1Char(':')), qPrintable(first));
    QVERIFY2(!first.contains(QLatin1Char('/')), qPrintable(first));
    QVERIFY(validateContainerName(first).isEmpty());

    // A name already exists (clone case): `<name>-copy`
    wizard.setName(QStringLiteral("web"));
    QCOMPARE(wizard.suggestedName(), QStringLiteral("web-copy"));
    // Keep appending a counter while the name is taken
    Container occupied;
    occupied.id = QStringLiteral("occupied");
    occupied.name = QStringLiteral("web-copy");
    backend.setContainers({taken, occupied});
    QCOMPARE(wizard.suggestedName(), QStringLiteral("web-copy2"));

    // (8) Command and entrypoint must reach the request (they were controller-only fields before)
    wizard.setCommandText(QStringLiteral("sh\n-c\nsleep infinity"));
    wizard.setEntrypointText(QStringLiteral("/usr/bin/env sh"));
    wizard.setWorkingDirectory(QStringLiteral("/app"));
    wizard.setUser(QStringLiteral("1000:1000"));
    // The summary shows command/entrypoint/working dir/user so the user can check what they entered
    const QVariantList summary = wizard.summary();
    QStringList summaryText;
    for (const QVariant &entry : summary) {
        const QVariantMap row = entry.toMap();
        summaryText.append(row.value(QStringLiteral("label")).toString() + QLatin1Char('=') + row.value(QStringLiteral("value")).toString());
    }
    const QString joined = summaryText.join(QStringLiteral(" | "));
    QVERIFY2(joined.contains(QStringLiteral("sh -c sleep infinity")), qPrintable(joined));
    QVERIFY2(joined.contains(QStringLiteral("/usr/bin/env sh")), qPrintable(joined));
    QVERIFY2(joined.contains(QStringLiteral("/app")), qPrintable(joined));
    QVERIFY2(joined.contains(QStringLiteral("1000:1000")), qPrintable(joined));
}

/*!
 * Command history (F3): local records (dedup / cap / persistence) plus merging commands from
 * existing containers.
 */
void ContainerCreateTest::commandHistoryRecordsAndMerges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("kcm_dockerrc"));

    {
        CommandHistoryStore history(path);
        QVERIFY(history.empty());

        // Multi-line commands are stored whole; a QStringList would split them on newlines,
        // so the backing store is a JSON array
        history.record(QStringLiteral("sh\n-c\nsleep infinity"));
        history.record(QStringLiteral("nginx -g 'daemon off;'"));
        QCOMPARE(history.commands().size(), 2);
        QCOMPARE(history.commands().first(), QStringLiteral("nginx -g 'daemon off;'")); // most recent first

        // A duplicate moves to the front instead of being added again
        history.record(QStringLiteral("sh\n-c\nsleep infinity"));
        QCOMPARE(history.commands().size(), 2);
        QCOMPARE(history.commands().first(), QStringLiteral("sh\n-c\nsleep infinity"));

        // Blank commands are not recorded
        history.record(QStringLiteral("   "));
        QCOMPARE(history.commands().size(), 2);

        // Cap: entries past the limit drop the oldest
        for (int i = 0; i < CommandHistoryStore::kMaxEntries + 5; ++i) {
            history.record(QStringLiteral("cmd-%1").arg(i));
        }
        QCOMPARE(history.commands().size(), CommandHistoryStore::kMaxEntries);
        QVERIFY(history.commands().first() == QStringLiteral("cmd-%1").arg(CommandHistoryStore::kMaxEntries + 4));

        // Commands from existing containers are merged in (not written to disk) and deduplicated
        history.mergeExternal({QStringLiteral("nginx -g 'daemon off;'"), QStringLiteral("from-container")});
        QVERIFY(history.commands().contains(QStringLiteral("from-container")));
        QCOMPARE(history.commands().count(QStringLiteral("nginx -g 'daemon off;'")), 1);
        // External entries are marked with source "container"
        bool foundExternal = false;
        for (const QVariant &entry : history.entries()) {
            const QVariantMap row = entry.toMap();
            if (row.value(QStringLiteral("command")).toString() == QLatin1String("from-container")) {
                foundExternal = row.value(QStringLiteral("source")).toString() == QLatin1String("container");
            }
        }
        QVERIFY2(foundExternal, "commands from existing containers must be marked as such");
    }

    // Reopened: only LOCAL records are persisted (external entries are transient)
    CommandHistoryStore reopened(path);
    QCOMPARE(reopened.commands().size(), CommandHistoryStore::kMaxEntries);
    QVERIFY(!reopened.commands().contains(QStringLiteral("from-container")));
    QVERIFY(reopened.commands().first() == QStringLiteral("cmd-%1").arg(CommandHistoryStore::kMaxEntries + 4));

    // Clear the local records
    reopened.clearLocal();
    QVERIFY(reopened.empty());
    CommandHistoryStore afterClear(path);
    QVERIFY2(afterClear.empty(), "clearing must persist");
}


/*!
 * Reusing the same host port within one request must be rejected (user-reported):
 * a user filled in 8100->80 / 8100->81 / 8100->82; the request itself is legal, but at start Docker
 * reports `Bind for 0.0.0.0:8100 failed: port is already allocated`.
 */
void ContainerCreateTest::duplicateHostPortsInOneRequestAreRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;
    OperationController operations(&backend);
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    backend.setImages({image});

    CreateContainerController wizard(&operations, &presets, &backend);
    wizard.setImage(QStringLiteral("alpine:3.19"));
    wizard.setPullIfMissing(true);
    wizard.setName(QStringLiteral("dup-ports"));
    // Move to the "ports" step (stepErrorKey() reports the CURRENT step)
    QVERIFY(wizard.nextStep()); // basics
    QVERIFY(wizard.nextStep()); // environment
    QVERIFY(wizard.nextStep()); // interactive
    QVERIFY(wizard.nextStep()); // ports
    QCOMPARE(wizard.stepKey(), QStringLiteral("ports"));

    // All three rows target the same host port -> rejected
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 8100}},
                        QVariantMap {{QStringLiteral("containerPort"), 81}, {QStringLiteral("hostPort"), 8100}},
                        QVariantMap {{QStringLiteral("containerPort"), 82}, {QStringLiteral("hostPort"), 8100}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portDuplicateInRequest"));

    // The same host port on DIFFERENT concrete addresses is legal
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80},
                                     {QStringLiteral("hostPort"), 8100},
                                     {QStringLiteral("hostIp"), QStringLiteral("127.0.0.1")}},
                        QVariantMap {{QStringLiteral("containerPort"), 81},
                                     {QStringLiteral("hostPort"), 8100},
                                     {QStringLiteral("hostIp"), QStringLiteral("192.168.1.5")}}});
    QVERIFY2(wizard.stepErrorKey().isEmpty(), qPrintable(wizard.stepErrorKey()));

    // Wildcard plus a concrete address on the same port still conflicts
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 8100}},
                        QVariantMap {{QStringLiteral("containerPort"), 81},
                                     {QStringLiteral("hostPort"), 8100},
                                     {QStringLiteral("hostIp"), QStringLiteral("127.0.0.1")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portDuplicateInRequest"));

    // Random ports (0) may be repeated any number of times
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 0}},
                        QVariantMap {{QStringLiteral("containerPort"), 81}, {QStringLiteral("hostPort"), 0}}});
    QVERIFY2(wizard.stepErrorKey().isEmpty(), qPrintable(wizard.stepErrorKey()));
}


/*!
 * Per-row port status (ARCH_next_ports.md §4.D, milestone M2).
 *
 * The UI must say, at the moment the user types, "this port is unusable, X holds it, try Y", so the
 * status is a property (refreshed whenever the container list changes), and: a free port produces no
 * hint, a stopped container does not count as a holder, and the suggestion avoids ports already
 * filled into the same form.
 */
void ContainerCreateTest::portRowStatusesReportHoldersAndSuggestFreePorts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kcm_dockerrc")));
    MockDockerBackend backend;
    OperationController operations(&backend);

    Container running;
    running.id = QStringLiteral("running-id");
    running.name = QStringLiteral("web");
    running.image = QStringLiteral("alpine:3.19");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("stopped-id");
    stopped.name = QStringLiteral("old");
    stopped.image = QStringLiteral("alpine:3.19");
    stopped.state = ContainerState::Exited;
    stopped.ports = {{QStringLiteral("0.0.0.0"), 80, 8200, QStringLiteral("tcp")}};

    backend.setContainers({running, stopped});
    CreateContainerController wizard(&operations, &presets, &backend);

    // One row held by a running container, one free, one declared by a stopped container
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 8100}},
                        QVariantMap {{QStringLiteral("containerPort"), 81}, {QStringLiteral("hostPort"), 9000}},
                        QVariantMap {{QStringLiteral("containerPort"), 82}, {QStringLiteral("hostPort"), 8200}}});
    const QVariantList statuses = wizard.portRowStatuses();
    QCOMPARE(statuses.size(), 3);

    const QVariantMap occupied = statuses.at(0).toMap();
    QCOMPARE(occupied.value(QStringLiteral("errorKey")).toString(), QStringLiteral("portInUse"));
    QCOMPARE(occupied.value(QStringLiteral("holder")).toString(), QStringLiteral("web"));
    QCOMPARE(occupied.value(QStringLiteral("hostPort")).toInt(), 8100);
    QVERIFY2(occupied.value(QStringLiteral("suggestion")).toInt() > 8100,
             "the suggestion must skip the port that is already used");

    // Free -> no hint at all (the UI then shows nothing)
    QVERIFY2(statuses.at(1).toMap().value(QStringLiteral("errorKey")).toString().isEmpty(),
             "a free port must not produce any hint");
    QCOMPARE(statuses.at(1).toMap().value(QStringLiteral("suggestion")).toInt(), 0);

    // A port declared by a stopped container is not a conflict (nothing runs, user confirmed)
    QVERIFY2(statuses.at(2).toMap().value(QStringLiteral("errorKey")).toString().isEmpty(),
             "a stopped container must not block its declared port");

    // The same host port twice in one form: reported, and the suggestion avoids ports this form uses
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 9100}},
                        QVariantMap {{QStringLiteral("containerPort"), 81}, {QStringLiteral("hostPort"), 9100}}});
    const QVariantList duplicates = wizard.portRowStatuses();
    QCOMPARE(duplicates.at(0).toMap().value(QStringLiteral("errorKey")).toString(), QStringLiteral("portDuplicateInRequest"));
    QCOMPARE(duplicates.at(1).toMap().value(QStringLiteral("errorKey")).toString(), QStringLiteral("portDuplicateInRequest"));
    const int suggestion = duplicates.at(1).toMap().value(QStringLiteral("suggestion")).toInt();
    QVERIFY2(suggestion != 9100, "the suggestion must not be one of the ports this form already uses");

    // Random port (0): neither a conflict nor a suggestion
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 0}}});
    QVERIFY(wizard.portRowStatuses().at(0).toMap().value(QStringLiteral("errorKey")).toString().isEmpty());
    QCOMPARE(wizard.portRowStatuses().at(0).toMap().value(QStringLiteral("suggestion")).toInt(), 0);
}


QTEST_MAIN(ContainerCreateTest)

#include "tst_container_create.moc"
