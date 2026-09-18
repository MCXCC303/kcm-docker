/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container.h"
#include "domain/container_create_request.h"
#include "model/container_detail_controller.h"
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
    void wizardGatesSteps();
    void wizardBuildsTheRequestAndSubmits();
    void cloneCopiesTheFullConfiguration();
    void suggestsNamesFromTheImageAndOccupancy();
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

/*!
 * 向导的分步校验（ARCH_V5_V8 §4.3）：每一步都不许带着问题往下走。
 */
void ContainerCreateTest::wizardGatesSteps()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kontainerrc")));
    MockDockerBackend backend;
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    backend.setImages({image});
    Container existing;
    existing.id = QStringLiteral("existing");
    existing.name = QStringLiteral("web");
    existing.image = QStringLiteral("alpine:3.19");
    existing.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};
    backend.setContainers({existing});
    OperationController operations(&backend);
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    operations.refreshWriteAccess();

    CreateContainerController wizard(&operations, &presets, &backend);
    QCOMPARE(CreateContainerController::stepKeys().size(), 7);
    QCOMPARE(wizard.stepKey(), QStringLiteral("image"));
    QCOMPARE(wizard.stepCount(), 7);

    // ① 镜像：先要求填，再要求本地存在（除非勾了"先拉取"）
    QVERIFY(!wizard.nextStep());
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("imageRequired"));
    wizard.setImage(QStringLiteral("busybox:latest"));
    QVERIFY(!wizard.nextStep());
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("imageNotLocal"));
    wizard.setPullIfMissing(true);
    QVERIFY(wizard.nextStep());
    QCOMPARE(wizard.stepKey(), QStringLiteral("basics"));

    // ② 基础：名称规则 + 重名
    wizard.setName(QStringLiteral("bad name"));
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("nameInvalid"));
    wizard.setName(QStringLiteral("WEB"));
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("nameInUse"));
    wizard.setName(QStringLiteral("worker"));
    QVERIFY(wizard.nextStep());

    // ③ 端口：容器端口必填、宿主端口不能冲突（0 = 随机，不冲突）
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 0},
                                     {QStringLiteral("hostPort"), 0}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portRequired"));
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80},
                                     {QStringLiteral("hostPort"), 8080}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("portInUse"));
    wizard.setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80},
                                     {QStringLiteral("hostPort"), 0}}});
    QVERIFY(wizard.nextStep());

    // ④ 环境与标签：键名规则
    wizard.setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("1BAD")},
                                            {QStringLiteral("value"), QStringLiteral("x")}}});
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("keyInvalid"));
    wizard.setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("GOOD")},
                                            {QStringLiteral("value"), QStringLiteral("x")}}});
    QVERIFY(wizard.nextStep());

    // ⑤ 挂载：目标必须绝对、不能重复；来源格式要被校验（bind 必须绝对）
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

    // ⑥ 资源：内存下限与 CPU 非负
    wizard.setMemoryLimitBytes(1024);
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("memoryTooSmall"));
    wizard.setMemoryLimitBytes(0);
    wizard.setCpus(-1.0);
    QCOMPARE(wizard.stepErrorKey(), QStringLiteral("cpusNegative"));
    wizard.setCpus(0.0);
    QVERIFY(wizard.nextStep());

    // ⑦ 总览：不能越级跳过来，也不能在总览上再"下一步"
    QCOMPARE(wizard.stepKey(), QStringLiteral("summary"));
    QVERIFY(wizard.onSummary());
    QVERIFY(!wizard.nextStep());
    // 往回跳随时可以（用户要能改前面的选择）
    QVERIFY(wizard.goToStep(QStringLiteral("ports")));
    QCOMPARE(wizard.stepKey(), QStringLiteral("ports"));
    // 往前跳则要逐步通过校验：总览不能从"还没填完"的地方直接到达
    wizard.setImage(QString());
    wizard.goToStep(QStringLiteral("image"));
    QVERIFY2(!wizard.goToStep(QStringLiteral("summary")), "the wizard must not skip unfilled steps");
}

/*!
 * 总览内容与提交（§4.4/§4.6）：环境变量只列键名，提交时把表单交给控制器。
 */
void ContainerCreateTest::wizardBuildsTheRequestAndSubmits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kontainerrc")));
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
    QVERIFY(wizard.nextStep()); // ports（无端口）
    QVERIFY(wizard.nextStep()); // environment
    QVERIFY(wizard.nextStep()); // mounts

    // 从预设快速添加（已存在的不会重复加）
    QVERIFY(wizard.addMountFromPreset(presetId));
    QVERIFY(!wizard.addMountFromPreset(presetId));
    QCOMPARE(wizard.mountRows().size(), 1);
    QVERIFY(wizard.presets().size() >= 1);

    QVERIFY(wizard.nextStep()); // resources
    QVERIFY(wizard.nextStep()); // summary

    // 总览：环境变量**只列键名**（值可能是密码）
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

    // 提交：写权限门在控制器里（这里是只读 endpoint），因此先换一个可写的
    QVERIFY(!wizard.submit());
    QVERIFY(!operations.resultText().isEmpty());
}

/*!
 * 克隆（ARCH_V5_V8 §4.5）：复制**配置**而不是运行时状态；命令/入口点/环境/标签/重启策略
 * 只有 inspect 里才有，因此从容器详情进入时要一并带过来。
 */
void ContainerCreateTest::cloneCopiesTheFullConfiguration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kontainerrc")));
    MockDockerBackend backend;

    Container listed;
    listed.id = QStringLiteral("cid-1");
    listed.name = QStringLiteral("web");
    listed.image = QStringLiteral("registry.example.com/team/app:1.0");
    listed.state = ContainerState::Running;
    listed.ports = {{QStringLiteral("0.0.0.0"), 8080, 18080, QStringLiteral("tcp")}};
    backend.setContainers({listed});

    // 详情（inspect）里才有的字段
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
    backend.completeRefresh(); // inspect 是异步的：必须先真的把详情读进来

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

    // 受控容器不存在时：返回 false，不改动现有表单
    const QString previousName = wizard.name();
    QVERIFY(!wizard.prefillFromContainer(QStringLiteral("does-not-exist")));
    QCOMPARE(wizard.name(), previousName);
}

/*!
 * 「用建议名称」（用户实测 ③）：名称为空时按镜像生成候选，并且**避开已占用的名字**。
 */
void ContainerCreateTest::suggestsNamesFromTheImageAndOccupancy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MountPresetStore presets(dir.filePath(QStringLiteral("kontainerrc")));
    MockDockerBackend backend;
    Container taken;
    taken.id = QStringLiteral("taken");
    taken.name = QStringLiteral("alpine-1111");
    backend.setContainers({taken});
    OperationController operations(&backend);
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    operations.refreshWriteAccess();

    CreateContainerController wizard(&operations, &presets, &backend);

    // 还没有镜像：也会给一个可用的候选（`container-xxxx`）——按钮因此始终可用，
    // 但真正要创建时镜像仍是必填项
    const QString fallback = wizard.suggestedName();
    QVERIFY2(!fallback.isEmpty(), "a suggestion must always be available");
    QVERIFY(validateContainerName(fallback).isEmpty());

    // 有镜像但没有名字：给 docker 风格的候选（去掉仓库前缀与 tag，非法字符换成 -）
    wizard.setImage(QStringLiteral("registry.example.com/team/My App:1.0"));
    const QString first = wizard.suggestedName();
    QVERIFY2(!first.isEmpty(), "a suggestion must be available as soon as an image is chosen");
    QVERIFY2(first.startsWith(QStringLiteral("my-app-")), qPrintable(first));
    QVERIFY2(!first.contains(QLatin1Char(':')), qPrintable(first));
    QVERIFY2(!first.contains(QLatin1Char('/')), qPrintable(first));
    QVERIFY(validateContainerName(first).isEmpty());

    // 已经有名字（克隆场景）：给 `<名字>-copy`
    wizard.setName(QStringLiteral("web"));
    QCOMPARE(wizard.suggestedName(), QStringLiteral("web-copy"));
    // 被占用时继续加序号
    Container occupied;
    occupied.id = QStringLiteral("occupied");
    occupied.name = QStringLiteral("web-copy");
    backend.setContainers({taken, occupied});
    QCOMPARE(wizard.suggestedName(), QStringLiteral("web-copy2"));

    // ⑧ 命令与入口点要能写进请求（原来只有控制器字段、界面没有入口）
    wizard.setCommandText(QStringLiteral("sh\n-c\nsleep infinity"));
    wizard.setEntrypointText(QStringLiteral("/usr/bin/env sh"));
    wizard.setWorkingDirectory(QStringLiteral("/app"));
    wizard.setUser(QStringLiteral("1000:1000"));
    // 总览里能看到命令/入口点/工作目录/用户（用户要能核对自己填了什么）
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

QTEST_MAIN(ContainerCreateTest)

#include "tst_container_create.moc"
