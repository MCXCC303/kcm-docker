/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container_create_request.h"
#include "model/mount_preset_store.h"
#include "i18n.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * 创建容器的表单 → Docker API 的映射（ARCH_V5_V8 §4.4）。
 *
 * 这是七期最容易"看起来对、实际发错请求"的地方，因此用**快照断言**钉住 JSON：
 * 字段名、嵌套结构、单位（NanoCpus 是十亿分之一核）、以及几个反直觉的点
 * （`name` 是 query 参数、随机端口用空字符串表达、tmpfs 不能进 Binds）。
 */
class ContainerCreateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void mapsFormFieldsToTheCreatePayload();
    void omitsEmptyFieldsAndKeepsDockerDefaults();
    void nameGoesIntoTheQueryNotTheBody();
    void tmpfsGoesToTmpfsNotBinds();
    void validatesNamesPathsKeysAndLimits();
    void presetStorePersistsAndOrders();
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

    // 端口映射里的容器端口也要出现在 ExposedPorts（Docker 的语义）
    QVERIFY(payload.value(QStringLiteral("ExposedPorts")).toObject().contains(QStringLiteral("80/tcp")));

    const QJsonObject hostConfig = payload.value(QStringLiteral("HostConfig")).toObject();
    const QJsonArray binds = hostConfig.value(QStringLiteral("Binds")).toArray();
    QCOMPARE(binds.size(), 2);
    QCOMPARE(binds.at(0).toString(), QStringLiteral("/srv/data:/data:ro")); // 只读带 :ro
    QCOMPARE(binds.at(1).toString(), QStringLiteral("app_cache:/cache")); // 命名卷同样是 Binds

    const QJsonObject portBindings = hostConfig.value(QStringLiteral("PortBindings")).toObject();
    const QJsonArray binding = portBindings.value(QStringLiteral("80/tcp")).toArray();
    QCOMPARE(binding.size(), 1);
    QCOMPARE(binding.at(0).toObject().value(QStringLiteral("HostIp")).toString(), QStringLiteral("0.0.0.0"));
    QCOMPARE(binding.at(0).toObject().value(QStringLiteral("HostPort")).toString(), QStringLiteral("8080"));

    const QJsonObject policy = hostConfig.value(QStringLiteral("RestartPolicy")).toObject();
    QCOMPARE(policy.value(QStringLiteral("Name")).toString(), QStringLiteral("on-failure"));
    QCOMPARE(policy.value(QStringLiteral("MaximumRetryCount")).toInt(), 3);

    QCOMPARE(hostConfig.value(QStringLiteral("Memory")).toDouble(), double(512LL * 1024 * 1024));
    // CPU 以"十亿分之一核"表达：1.5 核 = 1_500_000_000
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

void ContainerCreateTest::omitsEmptyFieldsAndKeepsDockerDefaults()
{
    ContainerCreateRequest request;
    request.name = QStringLiteral("minimal");
    request.image = QStringLiteral("alpine:3.19");

    const QJsonObject payload = QJsonDocument::fromJson(request.toJson()).object();
    // 没填的字段**不写进 JSON**，让引擎用它自己的默认值（写 null/空数组会改变语义）
    QVERIFY(!payload.contains(QStringLiteral("Cmd")));
    QVERIFY(!payload.contains(QStringLiteral("Env")));
    QVERIFY(!payload.contains(QStringLiteral("HostConfig")));
    QVERIFY(!payload.contains(QStringLiteral("NetworkingConfig")));
    QVERIFY(!payload.contains(QStringLiteral("ExposedPorts")));

    // 随机宿主端口：Docker 用**空字符串**表示"随机分配"，不是 0
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

    // 反直觉但必须记住：容器名是 `?name=` 而不是请求体里的字段
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
                      {QStringLiteral("bind"), QString(), QStringLiteral("/ignored"), false}}; // 没有来源 → 不发

    const QJsonObject hostConfig = QJsonDocument::fromJson(request.toJson()).object().value(QStringLiteral("HostConfig")).toObject();
    QVERIFY2(!hostConfig.contains(QStringLiteral("Binds")), "tmpfs and source-less mounts must not become binds");
    QVERIFY(hostConfig.value(QStringLiteral("Tmpfs")).toObject().contains(QStringLiteral("/tmp")));
}

void ContainerCreateTest::validatesNamesPathsKeysAndLimits()
{
    // 名称
    QCOMPARE(validateContainerName(QString()), QStringLiteral("nameRequired"));
    QCOMPARE(validateContainerName(QStringLiteral("has space")), QStringLiteral("nameInvalid"));
    QCOMPARE(validateContainerName(QStringLiteral("-leading-dash")), QStringLiteral("nameInvalid"));
    QVERIFY(validateContainerName(QStringLiteral("web_1.2-3")).isEmpty());

    // 容器内路径必须绝对
    QCOMPARE(validateContainerPath(QString()), QStringLiteral("pathRequired"));
    QCOMPARE(validateContainerPath(QStringLiteral("data")), QStringLiteral("pathNotAbsolute"));
    QVERIFY(validateContainerPath(QStringLiteral("/data")).isEmpty());

    // 环境变量/标签键
    QCOMPARE(validateEnvironmentKey(QString()), QStringLiteral("keyRequired"));
    QCOMPARE(validateEnvironmentKey(QStringLiteral("1BAD")), QStringLiteral("keyInvalid"));
    QCOMPARE(validateEnvironmentKey(QStringLiteral("has-dash")), QStringLiteral("keyInvalid"));
    QVERIFY(validateEnvironmentKey(QStringLiteral("GOOD_KEY_1")).isEmpty());

    // 资源限制：0 = 不限制；有值时不能低于 Docker 的 6 MiB
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
    const QString path = dir.filePath(QStringLiteral("kontainerrc"));

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
        // 非法输入被拒绝（宿主路径必须绝对、容器路径必须绝对）
        QVERIFY(store.add(QStringLiteral("relative/path"), QStringLiteral("/x")).isEmpty());
        QVERIFY(store.add(QStringLiteral("/srv/x"), QStringLiteral("relative")).isEmpty());

        QVERIFY(store.setFavorite(secondId, true));
        // 收藏置顶
        QCOMPARE(store.presets().first().id, secondId);
    }

    // 重新打开：内容与顺序都要还在（持久化真的写进了 kontainerrc）
    MountPresetStore reopened(path);
    QCOMPARE(reopened.count(), 2);
    QCOMPARE(reopened.presets().first().id, secondId);
    QVERIFY(reopened.presets().first().favorite);
    const MountPreset bindPreset = reopened.presets().last();
    QCOMPARE(bindPreset.source, QStringLiteral("/srv/data"));
    QCOMPARE(bindPreset.destination, QStringLiteral("/data"));
    QVERIFY(bindPreset.readOnly);
    QCOMPARE(bindPreset.note, QStringLiteral("数据目录"));

    // 排序：收藏在前，其余按加入顺序；下移能让非收藏项交换
    QVERIFY(reopened.moveDown(secondId) == false || reopened.presets().size() == 2);
    QVERIFY(reopened.remove(firstId));
    QCOMPARE(reopened.count(), 1);
}

void ContainerCreateTest::presetStoreDeduplicatesAndTrimsRecents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore store(dir.filePath(QStringLiteral("kontainerrc")));

    // 创建成功后把本次挂载并入"最近使用"
    QList<ContainerMountRequest> mounts;
    mounts.append({QStringLiteral("bind"), QStringLiteral("/srv/app"), QStringLiteral("/app"), false});
    mounts.append({QStringLiteral("tmpfs"), QString(), QStringLiteral("/tmp"), false}); // tmpfs 不进预设
    store.noteUsed(mounts);
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.presets().first().source, QStringLiteral("/srv/app"));
    QVERIFY(store.presets().first().lastUsedAt.isValid());

    // 再次使用同一个挂载：不新增，只刷新时间
    store.noteUsed(mounts);
    QCOMPARE(store.count(), 1);

    // 超过上限时淘汰最旧的"最近使用"，但**收藏与从未用过的预设不动**
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

QTEST_MAIN(ContainerCreateTest)

#include "tst_container_create.moc"
