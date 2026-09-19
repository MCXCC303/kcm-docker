/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/container_inspect_dto.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * inspect 的 `HostConfig.PortBindings`（ARCH_next_ports.md §1，里程碑 M1d）。
 *
 * 这是"声明 vs 实际发布"里**声明**那一半：Docker 在容器停止后依然保留它，
 * 而 `NetworkSettings.Ports`（实际发布）会是空的。实测 `WinBoat` 还用了端口区间。
 */
class ContainerInspectDeclaredPortsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesDeclaredBindingsWithRanges();
    void keepsPublishedAndDeclaredSeparate();
    void rejectsUnparsableHostPorts();
};

void ContainerInspectDeclaredPortsTest::parsesDeclaredBindingsWithRanges()
{
    const QByteArray json = R"({
        "Id": "cid-1",
        "HostConfig": {
            "PortBindings": {
                "8888/tcp": [{"HostIp": "", "HostPort": "20004"}, {"HostIp": "127.0.0.1", "HostPort": "20204"}],
                "3389/tcp": [{"HostIp": "127.0.0.1", "HostPort": "47300-47309"}]
            }
        },
        "NetworkSettings": {"Ports": {}}
    })";
    const auto dto = DockerContainerInspectDTO::fromPayload(json);
    QVERIFY(dto.has_value());
    const ContainerDetail detail = containerDetailFromDto(*dto);

    QCOMPARE(detail.declaredPorts.size(), 3);
    // 顺序稳定：按容器端口、宿主端口（QJsonObject 的键序不可依赖）
    const DeclaredPortBinding range = detail.declaredPorts.at(0);
    const DeclaredPortBinding first = detail.declaredPorts.at(1);
    QCOMPARE(first.containerPort, quint16(8888));
    QCOMPARE(first.protocol, QStringLiteral("tcp"));
    QCOMPARE(first.hostPort, quint16(20004));
    QVERIFY(!first.isRange());
    QCOMPARE(detail.declaredPorts.at(2).hostPort, quint16(20204));

    QCOMPARE(range.containerPort, quint16(3389));
    QCOMPARE(range.hostPort, quint16(47300));
    QCOMPARE(range.hostPortEnd, quint16(47309));
    QVERIFY2(range.isRange(), "the declared binding must keep its range");
    QCOMPARE(range.hostIp, QStringLiteral("127.0.0.1"));

    // 声明了但**没有**实际发布：published（NetworkSettings.Ports）为空
    QVERIFY2(detail.ports.isEmpty(), "a declared-only binding must not appear as published");
}

/*! 两个来源必须分开：实际发布的进 `ports`，声明的进 `declaredPorts`。 */
void ContainerInspectDeclaredPortsTest::keepsPublishedAndDeclaredSeparate()
{
    const QByteArray json = R"({
        "Id": "cid-2",
        "HostConfig": {"PortBindings": {"80/tcp": [{"HostIp": "", "HostPort": "8100"}]}},
        "NetworkSettings": {"Ports": {"80/tcp": [{"HostIp": "0.0.0.0", "HostPort": "8100"}]}}
    })";
    const auto dto = DockerContainerInspectDTO::fromPayload(json);
    QVERIFY(dto.has_value());
    const ContainerDetail detail = containerDetailFromDto(*dto);

    QCOMPARE(detail.ports.size(), 1);
    QCOMPARE(detail.ports.first().publicPort, quint16(8100));
    QCOMPARE(detail.declaredPorts.size(), 1);
    QCOMPARE(detail.declaredPorts.first().hostPort, quint16(8100));
}

/*! 坏输入（空串 = 随机分配、非数字、区间颠倒）不进模型。 */
void ContainerInspectDeclaredPortsTest::rejectsUnparsableHostPorts()
{
    const QByteArray json = R"({
        "Id": "cid-3",
        "HostConfig": {
            "PortBindings": {
                "80/tcp": [{"HostIp": "", "HostPort": ""}, {"HostIp": "", "HostPort": "abc"}, {"HostIp": "", "HostPort": "900-100"}],
                "8080/tcp": [{"HostIp": "", "HostPort": "8080"}]
            }
        },
        "NetworkSettings": {}
    })";
    const auto dto = DockerContainerInspectDTO::fromPayload(json);
    QVERIFY(dto.has_value());
    const ContainerDetail detail = containerDetailFromDto(*dto);

    QCOMPARE(detail.declaredPorts.size(), 1);
    QCOMPARE(detail.declaredPorts.first().hostPort, quint16(8080));
}

QTEST_MAIN(ContainerInspectDeclaredPortsTest)

#include "tst_container_inspect_declared_ports.moc"
