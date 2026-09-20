/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/container_inspect_dto.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * inspect's `HostConfig.PortBindings` (ARCH_next_ports.md §1, milestone M1d).
 *
 * This is the **declared** half of "declared vs actually published": Docker keeps it even after a
 * container stops, while `NetworkSettings.Ports` (published) is empty. `WinBoat` really uses
 * port ranges.
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
    // Stable order: by container port, then host port (QJsonObject key order is not reliable)
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

    // Declared but **not** published: published (NetworkSettings.Ports) is empty
    QVERIFY2(detail.ports.isEmpty(), "a declared-only binding must not appear as published");
}

/*! The two sources must stay separate: published goes to `ports`, declared to `declaredPorts`. */
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

/*! Bad input (empty string = random assignment, non-numeric, reversed range) never enters the model. */
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
