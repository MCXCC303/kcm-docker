/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/container_dto.h"
#include "dto/stats_dto.h"
#include "dto/engine_dto.h"
#include "dto/image_dto.h"
#include "dto/network_dto.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * DTO / parser 单元测试（ARCH_V1 §28.1）：
 * 正常记录、缺少可选字段、未知字段、空列表、非法 JSON、字段类型错误。
 */
class DtoParsersTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- containers -----------------------------------------------------
    void parsesContainer();
    void containerWithoutOptionalFields();
    void parsesNetworks();
    void networkWithoutOptionalFields();
    void networkListIsRobust();
    void containerWithUnknownFields();
    void containerWithWrongFieldTypes();
    void containerWithoutRequiredFieldsIsSkipped();
    void containerListEmpty();
    void containerListInvalidJson();
    void containerListSkipsBrokenEntries();

    // --- images ---------------------------------------------------------
    void parsesImage();
    void imageWithoutTagsIsDangling();
    void imageWithMultipleTags();
    void imageListInvalidPayload();

    // --- engine ---------------------------------------------------------
    void parsesVersion();
    void parsesInfo();
    void versionWithoutApiVersionFails();
    void infoToleratesMissingFields();

    // --- stats（§44 Metrics：畸形数据） ---
    void parsesStats();
    void statsWithoutCpuStatsFails();
    void statsPayloadMustBeObject();
    void statsFallsBackToPercpuUsage();
    void statsHandlesCgroupV1Cache();
    void statsBlockIoToleratesSyncAsyncOnly();
};

void DtoParsersTest::parsesContainer()
{
    const QByteArray payload = R"([{
        "Id": "cdb609ac2a7cd0ddfc2c33cee1a71b8314052b645b9cdc2d1793fc5a8e8b2976",
        "Names": ["/dl-medai"],
        "Image": "registry.example/practice:medai",
        "ImageID": "sha256:0cff9e",
        "State": "running",
        "Status": "Up 9 hours",
        "Created": 1789552206,
        "Health": {"Status": "healthy", "FailingStreak": 0},
        "Ports": [{"IP": "0.0.0.0", "PrivatePort": 8888, "PublicPort": 20004, "Type": "tcp"}]
    }])";

    QString error;
    int skipped = 0;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error, &skipped);

    QVERIFY(error.isEmpty());
    QCOMPARE(skipped, 0);
    QCOMPARE(containers.size(), 1);

    const DockerContainerDTO &dto = containers.first();
    QCOMPARE(dto.id, QStringLiteral("cdb609ac2a7cd0ddfc2c33cee1a71b8314052b645b9cdc2d1793fc5a8e8b2976"));
    QCOMPARE(dto.name, QStringLiteral("dl-medai")); // 前导 '/' 已去掉
    QCOMPARE(dto.image, QStringLiteral("registry.example/practice:medai"));
    QCOMPARE(dto.state, QStringLiteral("running"));
    QCOMPARE(dto.status, QStringLiteral("Up 9 hours"));
    QCOMPARE(dto.healthStatus, QStringLiteral("healthy"));
    QCOMPARE(dto.createdUnix, 1789552206);
    QCOMPARE(dto.ports.size(), 1);
    QCOMPARE(dto.ports.first().publicPort, quint16(20004));
    QCOMPARE(dto.ports.first().privatePort, quint16(8888));
    QCOMPARE(dto.ports.first().type, QStringLiteral("tcp"));
}

void DtoParsersTest::containerWithoutOptionalFields()
{
    // 老引擎没有 Health 字段；Ports 可以为空
    const QByteArray payload = R"([{"Id": "abc123", "Names": ["/test"], "State": "exited"}])";

    QString error;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first().healthStatus, QString());
    QCOMPARE(containers.first().image, QString());
    QCOMPARE(containers.first().ports.size(), 0);
    QCOMPARE(containers.first().createdUnix, 0);
}

void DtoParsersTest::containerWithUnknownFields()
{
    // 未知字段必须被忽略（§41）
    const QByteArray payload = R"([{
        "Id": "abc123", "Names": ["/test"], "State": "running",
        "SomeFutureField": {"nested": [1, 2, 3]}, "ImageManifestDescriptor": {"digest": "sha256:x"}
    }])";

    QString error;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first().state, QStringLiteral("running"));
}

void DtoParsersTest::containerWithWrongFieldTypes()
{
    // 类型错误的可选字段不能被当作有效值，也不能让整条记录/整个响应失败
    const QByteArray payload = R"([{
        "Id": "abc123", "Names": ["/test"], "State": "running",
        "Created": "not-a-number", "Ports": "not-an-array", "Health": 42, "Image": 7
    }])";

    QString error;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first().createdUnix, 0);
    QCOMPARE(containers.first().ports.size(), 0);
    QCOMPARE(containers.first().healthStatus, QString());
    QCOMPARE(containers.first().image, QString());
}

void DtoParsersTest::containerWithoutRequiredFieldsIsSkipped()
{
    const QByteArray payload = R"([
        {"Names": ["/no-id"], "State": "running"},
        {"Id": "abc", "Names": ["/no-state"]},
        {"Id": "def", "State": "running"},
        {"Id": "ghi", "Names": [], "State": "running"}
    ])";

    QString error;
    int skipped = 0;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error, &skipped);

    QVERIFY(error.isEmpty());
    QCOMPARE(skipped, 2); // 缺 Id / 缺 State 的两条被跳过
    QCOMPARE(containers.size(), 2);
    // 没有 Names 时用短 ID 兜底，保证容器仍然可见
    QCOMPARE(containers.at(0).id, QStringLiteral("def"));
    QCOMPARE(containers.at(0).name, QStringLiteral("def"));
    QCOMPARE(containers.at(1).id, QStringLiteral("ghi"));
    QCOMPARE(containers.at(1).name, QStringLiteral("ghi"));
}

void DtoParsersTest::containerListEmpty()
{
    QString error;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(QByteArray("[]"), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(containers.isEmpty());
}

void DtoParsersTest::containerListInvalidJson()
{
    QString error;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(QByteArray("{not json"), &error);
    QVERIFY(containers.isEmpty());
    QVERIFY(!error.isEmpty());
}

void DtoParsersTest::containerListSkipsBrokenEntries()
{
    const QByteArray payload = R"([
        {"Id": "ok1", "Names": ["/a"], "State": "running"},
        "this-is-not-an-object",
        42,
        {"Id": "ok2", "Names": ["/b"], "State": "exited"}
    ])";

    QString error;
    int skipped = 0;
    const QList<DockerContainerDTO> containers = DockerContainerDTO::listFromJson(payload, &error, &skipped);

    QVERIFY(error.isEmpty());
    QCOMPARE(skipped, 2);
    QCOMPARE(containers.size(), 2);
}

void DtoParsersTest::parsesImage()
{
    const QByteArray payload = R"([{
        "Id": "sha256:0cff9eb0e7aee9953e55bc682852ca4fdca233145a58ae1ec94f0b0c01a2ed30",
        "RepoTags": ["ghcr.io/dockur/windows:6.05"],
        "RepoDigests": ["ghcr.io/dockur/windows@sha256:32cc92"],
        "Size": 840000000,
        "Created": 1789557262,
        "Containers": 1
    }])";

    QString error;
    const QList<DockerImageDTO> images = DockerImageDTO::listFromJson(payload, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.first().repoTags, QStringList {QStringLiteral("ghcr.io/dockur/windows:6.05")});
    QCOMPARE(images.first().sizeBytes, Q_INT64_C(840000000));
    QCOMPARE(images.first().containers, 1);
}

void DtoParsersTest::imageWithoutTagsIsDangling()
{
    // RepoTags / RepoDigests 为 null 是中间层镜像的正常情况
    const QByteArray payload = R"([{"Id": "sha256:deadbeef", "RepoTags": null, "RepoDigests": null, "Size": 1024}])";

    QString error;
    const QList<DockerImageDTO> images = DockerImageDTO::listFromJson(payload, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(images.size(), 1);
    QVERIFY(images.first().repoTags.isEmpty());
    QCOMPARE(images.first().containers, -1); // 未提供
}

void DtoParsersTest::imageWithMultipleTags()
{
    // §8.1：一个 image 可能对应多个 repository/tag，domain 必须完整保留
    const QByteArray payload = R"([{
        "Id": "sha256:aaaa",
        "RepoTags": ["ghcr.io/dockur/windows:6.05", "windows:latest", "windows:6"],
        "RepoDigests": ["ghcr.io/dockur/windows@sha256:bbbb"],
        "Size": 800733572
    }])";

    QString error;
    const QList<DockerImageDTO> images = DockerImageDTO::listFromJson(payload, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.first().repoTags.size(), 3);

    const Image image = imageFromDto(images.first());
    QCOMPARE(image.repoTags.size(), 3);
    QCOMPARE(image.primaryTag(), QStringLiteral("ghcr.io/dockur/windows:6.05"));
    QVERIFY(!image.isDangling());
}

void DtoParsersTest::imageListInvalidPayload()
{
    QString error;
    const QList<DockerImageDTO> images = DockerImageDTO::listFromJson(QByteArray(R"({"Images": []})"), &error);
    QVERIFY(images.isEmpty());
    QVERIFY(!error.isEmpty());
}

void DtoParsersTest::parsesVersion()
{
    // Docker 29 的 /version 结构（ApiVersion 是协商的唯一来源）
    const QByteArray payload = R"({
        "Platform": {"Name": ""},
        "Version": "29.8.0",
        "ApiVersion": "1.56",
        "MinAPIVersion": "1.40",
        "Os": "linux",
        "Arch": "amd64",
        "Components": [
            {"Name": "Engine", "Version": "29.8.0", "Details": {"KernelVersion": "6.18.51-1-lts", "GitCommit": "3ce5872b79"}},
            {"Name": "containerd", "Version": "2.0.0"}
        ]
    })";

    QString error;
    const auto version = DockerVersionDTO::fromPayload(payload, &error);
    QVERIFY(version.has_value());
    QCOMPARE(version->version, QStringLiteral("29.8.0"));
    QCOMPARE(version->apiVersion, QStringLiteral("1.56"));
    QCOMPARE(version->minApiVersion, QStringLiteral("1.40"));
    QCOMPARE(version->arch, QStringLiteral("amd64"));
    QCOMPARE(version->kernelVersion, QStringLiteral("6.18.51-1-lts"));
    QCOMPARE(version->gitCommit, QStringLiteral("3ce5872b79"));
}

void DtoParsersTest::parsesInfo()
{
    const QByteArray payload = R"({
        "Name": "THF-Desktop", "OperatingSystem": "Arch Linux", "OSType": "linux",
        "Architecture": "x86_64", "KernelVersion": "6.18.51-1-lts",
        "CgroupVersion": "2", "CgroupDriver": "systemd", "Driver": "overlayfs",
        "Containers": 5, "ContainersRunning": 4, "ContainersPaused": 0, "ContainersStopped": 1,
        "Images": 4, "NCPU": 16, "MemTotal": 41247203328
    })";

    QString error;
    const auto info = DockerInfoDTO::fromPayload(payload, &error);
    QVERIFY(info.has_value());
    QCOMPARE(info->engineName, QStringLiteral("THF-Desktop"));
    QCOMPARE(info->containers, 5);
    QCOMPARE(info->containersRunning, 4);
    QCOMPARE(info->containersStopped, 1);
    QCOMPARE(info->images, 4);
    QCOMPARE(info->cgroupVersion, QStringLiteral("2"));
    QCOMPARE(info->storageDriver, QStringLiteral("overlayfs"));
    QCOMPARE(info->memoryTotalBytes, Q_INT64_C(41247203328));
}

void DtoParsersTest::versionWithoutApiVersionFails()
{
    QString error;
    const auto version = DockerVersionDTO::fromPayload(QByteArray(R"({"Version": "29.8.0"})"), &error);
    QVERIFY(!version.has_value());
    QVERIFY(!error.isEmpty());
}

void DtoParsersTest::infoToleratesMissingFields()
{
    QString error;
    const auto info = DockerInfoDTO::fromPayload(QByteArray(R"({"Name": "x"})"), &error);
    QVERIFY(info.has_value());
    QCOMPARE(info->containers, 0);
    QCOMPARE(info->images, 0);
    QVERIFY(info->kernelVersion.isEmpty());
}

void DtoParsersTest::parsesStats()
{
    const QByteArray payload = R"({
        "id": "cid",
        "cpu_stats": {"cpu_usage": {"total_usage": 3000000000}, "system_cpu_usage": 600000000000, "online_cpus": 4},
        "precpu_stats": {"cpu_usage": {"total_usage": 2000000000}, "system_cpu_usage": 500000000000},
        "memory_stats": {"usage": 110000000, "limit": 268435456, "stats": {"inactive_file": 10000000}},
        "networks": {"eth0": {"rx_bytes": 1000, "tx_bytes": 2000}, "eth1": {"rx_bytes": 500, "tx_bytes": 100}},
        "blkio_stats": {"io_service_bytes_recursive": [{"op": "read", "value": 4096}, {"op": "write", "value": 8192}]},
        "pids_stats": {"current": 7}
    })";

    QString error;
    const auto dto = DockerStatsDTO::fromPayload(payload, &error);
    QVERIFY(dto.has_value());
    QCOMPARE(dto->onlineCpus, 4);
    QCOMPARE(dto->memoryCacheBytes, quint64(10000000));
    QCOMPARE(dto->networkRxBytes, quint64(1500)); // 多网卡求和
    QCOMPARE(dto->networkTxBytes, quint64(2100));
    QCOMPARE(dto->blockReadBytes, quint64(4096));
    QCOMPARE(dto->blockWriteBytes, quint64(8192));
    QCOMPARE(dto->pids, 7);
}

void DtoParsersTest::statsWithoutCpuStatsFails()
{
    QString error;
    QVERIFY(!DockerStatsDTO::fromPayload(QByteArray(R"({"memory_stats": {"usage": 1}})"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void DtoParsersTest::statsPayloadMustBeObject()
{
    QString error;
    QVERIFY(!DockerStatsDTO::fromPayload(QByteArray("[1, 2, 3]"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void DtoParsersTest::statsFallsBackToPercpuUsage()
{
    // 老引擎没有 online_cpus：用 percpu_usage 条目数兜底
    const QByteArray payload = R"({
        "cpu_stats": {"cpu_usage": {"total_usage": 10, "percpu_usage": [1, 2, 3, 4, 5, 6, 7, 8]},
                      "system_cpu_usage": 100},
        "precpu_stats": {"cpu_usage": {"total_usage": 5}, "system_cpu_usage": 50}
    })";
    QString error;
    const auto dto = DockerStatsDTO::fromPayload(payload, &error);
    QVERIFY(dto.has_value());
    QCOMPARE(dto->onlineCpus, 8);
}

void DtoParsersTest::statsHandlesCgroupV1Cache()
{
    // cgroup v1 只有 cache / total_inactive_file
    const QByteArray payload = R"({
        "cpu_stats": {"cpu_usage": {"total_usage": 10}, "system_cpu_usage": 100, "online_cpus": 2},
        "memory_stats": {"usage": 1000, "limit": 2000, "stats": {"total_inactive_file": 400}}
    })";
    QString error;
    const auto dto = DockerStatsDTO::fromPayload(payload, &error);
    QVERIFY(dto.has_value());
    QCOMPARE(dto->memoryCacheBytes, quint64(400));
    QCOMPARE(containerStatsFromDto(*dto).memoryUsedBytes(), quint64(600));
}

void DtoParsersTest::statsBlockIoToleratesSyncAsyncOnly()
{
    // 只有 sync/async 时不能重复计数（sync+async 视为总量）
    const QByteArray payload = R"({
        "cpu_stats": {"cpu_usage": {"total_usage": 10}, "system_cpu_usage": 100, "online_cpus": 2},
        "blkio_stats": {"io_service_bytes_recursive": [{"op": "sync", "value": 100}, {"op": "async", "value": 50}]}
    })";
    QString error;
    const auto dto = DockerStatsDTO::fromPayload(payload, &error);
    QVERIFY(dto.has_value());
    QCOMPARE(dto->blockReadBytes, quint64(150));
    QCOMPARE(dto->blockWriteBytes, quint64(0));
}

/*!
 * 网络列表解析（ARCH_V5_V8 §3.2）。
 *
 * 样例取自本机真实 daemon 的 `GET /networks`（bridge / none / winboat_default / host），
 * 字段缺失、类型不符、成员地址带子网前缀这些真实形态都要处理。
 */
void DtoParsersTest::parsesNetworks()
{
    const QByteArray payload = R"([
        {
            "Name": "bridge",
            "Id": "5cd9ee041dffb38f712fa8b9284ef54c3fcaffd720958515aa3c155b82af90b9",
            "Created": "2026-09-16T17:44:09.395242226+08:00",
            "Scope": "local",
            "Driver": "bridge",
            "EnableIPv4": true,
            "EnableIPv6": false,
            "IPAM": {"Driver": "default", "Options": null,
                     "Config": [{"Subnet": "172.17.0.0/16", "Gateway": "172.17.0.1"}]},
            "Internal": false,
            "Attachable": false,
            "Ingress": false,
            "Options": {"com.docker.network.bridge.name": "docker0",
                        "com.docker.network.driver.mtu": "1500"},
            "Labels": {},
            "Containers": {}
        },
        {
            "Name": "winboat_default",
            "Id": "aaaaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffff1234",
            "Created": "2026-09-16T19:14:22.391557578+08:00",
            "Scope": "local",
            "Driver": "bridge",
            "IPAM": {"Config": [{"Subnet": "172.18.0.0/16", "Gateway": "172.18.0.1"}]},
            "Options": {},
            "Labels": {"com.docker.compose.project": "winboat"},
            "Containers": {
                "1111111111111111111111111111111111111111111111111111111111111111": {
                    "Name": "winboat", "EndpointID": "ep1",
                    "MacAddress": "02:42:ac:12:00:02",
                    "IPv4Address": "172.18.0.2/16", "IPv6Address": ""
                }
            }
        }
    ])";

    QString error;
    int skipped = 0;
    const QList<DockerNetworkDTO> dtos = DockerNetworkDTO::listFromJson(payload, &error, &skipped);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(skipped, 0);
    QCOMPARE(dtos.size(), 2);

    const QList<Network> networks = networksFromDto(dtos);
    const Network &builtin = networks.at(0);
    QCOMPARE(builtin.name, QStringLiteral("bridge"));
    QCOMPARE(builtin.shortId(), QStringLiteral("5cd9ee041dff"));
    QCOMPARE(builtin.driver, QStringLiteral("bridge"));
    QCOMPARE(builtin.scope, QStringLiteral("local"));
    QVERIFY(builtin.created.isValid());
    QVERIFY2(builtin.isPredefined(), "bridge/host/none are the daemon's pre-defined networks");
    QCOMPARE(builtin.subnetText(), QStringLiteral("172.17.0.0/16"));
    QCOMPARE(builtin.primaryGateway(), QStringLiteral("172.17.0.1"));
    QCOMPARE(builtin.memberCount(), 0);
    // 选项按 key 排序：顺序稳定，界面不会每次刷新都换顺序
    QCOMPARE(builtin.options.size(), 2);
    QCOMPARE(builtin.options.first().first, QStringLiteral("com.docker.network.bridge.name"));

    const Network &compose = networks.at(1);
    QVERIFY2(!compose.isPredefined(), "a compose network must be deletable");
    QCOMPARE(compose.labels.size(), 1);
    QCOMPARE(compose.memberCount(), 1);
    // 成员地址去掉子网前缀（daemon 给的是 172.18.0.2/16）
    QCOMPARE(compose.members.first().name, QStringLiteral("winboat"));
    QCOMPARE(compose.members.first().ipv4Address, QStringLiteral("172.18.0.2"));
    QCOMPARE(compose.members.first().macAddress, QStringLiteral("02:42:ac:12:00:02"));
}

void DtoParsersTest::networkWithoutOptionalFields()
{
    // host / none 这类网络没有 IPAM、没有选项：不能因为缺字段就整条丢掉
    const QByteArray payload = R"([
        {"Name": "host", "Id": "2222222222222222222222222222222222222222222222222222222222222222", "Driver": "host"},
        {"Name": "none", "Id": "3333333333333333333333333333333333333333333333333333333333333333", "Driver": "null",
         "IPAM": {"Config": []}, "Options": null, "Labels": null}
    ])";

    int skipped = 0;
    const QList<Network> networks = networksFromDto(DockerNetworkDTO::listFromJson(payload, nullptr, &skipped));
    QCOMPARE(skipped, 0);
    QCOMPARE(networks.size(), 2);
    QCOMPARE(networks.at(0).driver, QStringLiteral("host"));
    QVERIFY(networks.at(0).subnetText().isEmpty());
    QVERIFY(networks.at(0).created.isNull());
    QVERIFY(networks.at(0).isPredefined());
    QVERIFY(networks.at(1).options.isEmpty());
    QVERIFY(networks.at(1).isPredefined());
}

void DtoParsersTest::networkListIsRobust()
{
    // 坏条目跳过、好的保留；整体不是数组则报错且返回空
    const QByteArray payload = R"([
        {"Name": "good", "Id": "4444444444444444444444444444444444444444444444444444444444444444", "Driver": "bridge"},
        {"Name": "no-id"},
        "not-an-object"
    ])";
    QString error;
    int skipped = 0;
    const QList<Network> networks = networksFromDto(DockerNetworkDTO::listFromJson(payload, &error, &skipped));
    QVERIFY(error.isEmpty());
    QCOMPARE(skipped, 2);
    QCOMPARE(networks.size(), 1);
    QCOMPARE(networks.first().name, QStringLiteral("good"));

    error.clear();
    const QList<DockerNetworkDTO> broken = DockerNetworkDTO::listFromJson(QByteArrayLiteral("{\"not\":\"an array\"}"), &error, nullptr);
    QVERIFY(broken.isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(DtoParsersTest)

#include "tst_dto_parsers.moc"
