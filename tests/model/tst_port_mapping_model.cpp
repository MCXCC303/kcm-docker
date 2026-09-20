/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/container_detail_controller.h"
#include "model/port_mapping_model.h"
#include "support/mock_docker_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * Port mapping model (ARCH_V4 §2.1.2 / §5.1).
 *
 * The topology draws one line per row, so two things must hold:
 *  - published / unpublished split (unpublished ports have no host endpoint, they cannot enter the topology)
 *  - stable order (jitter makes the graph jump on every refresh)
 */
class PortMappingModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void splitsPublishedFromUnpublished();
    void keepsOneToManyMappings();
    void sortsByContainerPortThenProtocol();
    void chipTextsAreStable();
    void unchangedPortsDoNotResetTheModels();
    void groupsBindingsOfTheSameContainerPort();
    void groupingKeepsProtocolsApartAndIgnoresUnpublished();
    void groupsDoNotResetWhenUnchanged();
    void dualStackMergeIsPicky();

private:
    ContainerDetail detailWithPorts(const QList<Port> &ports);
};

void PortMappingModelTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
}

ContainerDetail PortMappingModelTest::detailWithPorts(const QList<Port> &ports)
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.ports = ports;
    return detail;
}

void PortMappingModelTest::splitsPublishedFromUnpublished()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    backend.setContainerDetail(detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
        Port {QString(), 9000, 0, QStringLiteral("tcp")}, // EXPOSE only
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.publishedPorts()->count(), 1);
    QCOMPARE(controller.unpublishedPorts()->count(), 1);
    QVERIFY(controller.publishedPorts()->index(0, 0).data(PortMappingModel::PublishedRole).toBool());
    QVERIFY(!controller.unpublishedPorts()->index(0, 0).data(PortMappingModel::PublishedRole).toBool());
    // Unpublished ports have no host endpoint: the host chip text stays empty so the UI can say "unpublished"
    QVERIFY(controller.unpublishedPorts()->index(0, 0).data(PortMappingModel::HostChipTextRole).toString().isEmpty());
}

void PortMappingModelTest::keepsOneToManyMappings()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    // One container port bound to two host addresses: two topology lines, never merged
    backend.setContainerDetail(detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 80, 8080, QStringLiteral("tcp")},
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.publishedPorts()->count(), 2);
    QCOMPARE(controller.publishedPorts()->index(0, 0).data(PortMappingModel::HostChipTextRole).toString(), QStringLiteral("0.0.0.0:8080"));
    QCOMPARE(controller.publishedPorts()->index(1, 0).data(PortMappingModel::HostChipTextRole).toString(), QStringLiteral("127.0.0.1:8080"));
}

void PortMappingModelTest::sortsByContainerPortThenProtocol()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    backend.setContainerDetail(detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 8443, 443, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 53, 53, QStringLiteral("udp")},
        Port {QStringLiteral("0.0.0.0"), 53, 53, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    // The engine's order is unreliable; sort by container port ascending, then protocol
    QCOMPARE(controller.publishedPorts()->index(0, 0).data(PortMappingModel::ContainerPortRole).toInt(), 53);
    QCOMPARE(controller.publishedPorts()->index(0, 0).data(PortMappingModel::ProtocolRole).toString(), QStringLiteral("tcp"));
    QCOMPARE(controller.publishedPorts()->index(1, 0).data(PortMappingModel::ProtocolRole).toString(), QStringLiteral("udp"));
    QCOMPARE(controller.publishedPorts()->index(2, 0).data(PortMappingModel::ContainerPortRole).toInt(), 443);
    QCOMPARE(controller.publishedPorts()->index(3, 0).data(PortMappingModel::ContainerPortRole).toInt(), 8443);

    // Repeated refreshes do not reorder (else the topology re-lays out every time)
    const QList<PortMappingEntry> before = controller.publishedPorts()->mappings();
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(controller.publishedPorts()->mappings(), before);
}

void PortMappingModelTest::chipTextsAreStable()
{
    PortMappingEntry published;
    published.containerPort = 80;
    published.protocol = QStringLiteral("tcp");
    published.hostIp = QStringLiteral("0.0.0.0");
    published.hostPort = 8080;
    QCOMPARE(published.containerChipText(), QStringLiteral("80/tcp"));
    QCOMPARE(published.hostChipText(), QStringLiteral("0.0.0.0:8080"));

    // Missing protocol means tcp (the engine sometimes omits Type)
    PortMappingEntry noProtocol;
    noProtocol.containerPort = 53;
    noProtocol.hostIp = QStringLiteral("0.0.0.0");
    noProtocol.hostPort = 53;
    QCOMPARE(noProtocol.containerChipText(), QStringLiteral("53/tcp"));

    // Missing host IP means 0.0.0.0: never render ":8080"
    PortMappingEntry noIp;
    noIp.containerPort = 80;
    noIp.hostIp = QString();
    noIp.hostPort = 8080;
    QCOMPARE(noIp.hostChipText(), QStringLiteral("0.0.0.0:8080"));
}

void PortMappingModelTest::unchangedPortsDoNotResetTheModels()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);
    backend.setContainerDetail(detailWithPorts({Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}}));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QSignalSpy publishedReset(controller.publishedPorts(), &QAbstractItemModel::modelReset);
    QSignalSpy unpublishedReset(controller.unpublishedPorts(), &QAbstractItemModel::modelReset);

    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(publishedReset.count(), 0);
    QCOMPARE(unpublishedReset.count(), 0);
}

/*!
 * Grouping (ARCH_V5_V8 §2.1 topology revision): bindings of one container port form a group,
 * so the left column shows a single chip and the right side branches per binding.
 */
void PortMappingModelTest::groupsBindingsOfTheSameContainerPort()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    backend.setContainerDetail(detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
        Port {QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 8888, 20204, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 4800, 4800, QStringLiteral("tcp")},
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    PortMappingGroupModel *groups = controller.portGroups();
    QVERIFY(groups);
    QCOMPARE(groups->count(), 2);
    // Of the 4 raw mappings, "0.0.0.0:20004" and "::20004" are the IPv4/IPv6 halves of one mapping → merged
    QCOMPARE(groups->bindingCount(), 3);

    const QModelIndex first = groups->index(0, 0);
    QCOMPARE(first.data(PortMappingGroupModel::ContainerChipTextRole).toString(), QStringLiteral("4800/tcp"));
    QCOMPARE(first.data(PortMappingGroupModel::BindingCountRole).toInt(), 1);

    const QModelIndex second = groups->index(1, 0);
    QCOMPARE(second.data(PortMappingGroupModel::ContainerChipTextRole).toString(), QStringLiteral("8888/tcp"));
    QCOMPARE(second.data(PortMappingGroupModel::BindingCountRole).toInt(), 2);
    // Deterministic in-group order: host port, then host address (rows do not jump on refresh)
    QCOMPARE(second.data(PortMappingGroupModel::HostChipTextsRole).toStringList(),
             QStringList({QStringLiteral("20004"), QStringLiteral("127.0.0.1:20204")}));
    // The merged binding is flagged dual-stack (the UI draws a double ring for it)
    QCOMPARE(second.data(PortMappingGroupModel::DualStackFlagsRole).toList(),
             QVariantList({true, false}));
}

void PortMappingModelTest::groupingKeepsProtocolsApartAndIgnoresUnpublished()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    // Same number, different protocol = two distinct ports; unpublished ports have no host endpoint
    backend.setContainerDetail(detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 53, 53, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 53, 53, QStringLiteral("udp")},
        Port {QString(), 9000, 0, QStringLiteral("tcp")},
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.portGroups()->count(), 2);
    QCOMPARE(controller.portGroups()->index(0, 0).data(PortMappingGroupModel::ProtocolRole).toString(), QStringLiteral("tcp"));
    QCOMPARE(controller.portGroups()->index(1, 0).data(PortMappingGroupModel::ProtocolRole).toString(), QStringLiteral("udp"));
    QCOMPARE(controller.portGroups()->bindingCount(), 2);
    // Unpublished ones still appear in their own model (the page groups them separately)
    QCOMPARE(controller.unpublishedPorts()->count(), 1);
}

void PortMappingModelTest::groupsDoNotResetWhenUnchanged()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);
    const auto detail = detailWithPorts({
        Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 80, 8081, QStringLiteral("tcp")},
    });
    backend.setContainerDetail(detail);

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QSignalSpy resetSpy(controller.portGroups(), &QAbstractItemModel::modelReset);
    QCOMPARE(resetSpy.count(), 0);
    // Refresh with identical data again: no model rebuild (no delegate destruction/re-creation)
    backend.setContainerDetail(detail);
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(controller.portGroups()->count(), 1);
    QCOMPARE(controller.portGroups()->bindingCount(), 2);
}

/*!
 * The IPv4/IPv6 wildcard merge must be picky: only **same container port, same host port, both wildcards**.
 *
 * Otherwise genuinely distinct mappings (two host ports, or wildcard + specific address) get fused
 * into one, hiding from the user that they really have two.
 */
void PortMappingModelTest::dualStackMergeIsPicky()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    backend.setContainerDetail(detailWithPorts({
        // Same container port → two **different** host ports: no merge
        Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
        Port {QStringLiteral("::"), 80, 8081, QStringLiteral("tcp")},
        // Both wildcards but different host ports: no merge
        Port {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
        Port {QStringLiteral("::"), 443, 9443, QStringLiteral("tcp")},
        // Wildcard + specific address (same port): no merge — the user did bind two addresses
        Port {QStringLiteral("0.0.0.0"), 53, 5353, QStringLiteral("udp")},
        Port {QStringLiteral("127.0.0.1"), 53, 5353, QStringLiteral("udp")},
        // The real pair: the only merge
        Port {QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
        Port {QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")},
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    PortMappingGroupModel *groups = controller.portGroups();
    QVERIFY(groups);
    // 8 raw mappings - 1 merge = 7
    QCOMPARE(groups->bindingCount(), 7);
    int dualStackCount = 0;
    for (int row = 0; row < groups->count(); ++row) {
        const QVariantList flags = groups->index(row, 0).data(PortMappingGroupModel::DualStackFlagsRole).toList();
        for (const QVariant &flag : flags) {
            if (flag.toBool()) {
                ++dualStackCount;
            }
        }
    }
    QCOMPARE(dualStackCount, 1);
}


QTEST_MAIN(PortMappingModelTest)

#include "tst_port_mapping_model.moc"
