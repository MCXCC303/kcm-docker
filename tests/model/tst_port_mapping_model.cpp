/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 端口映射模型（ARCH_V4 §2.1.2 / §5.1）。
 *
 * 拓扑图按行画连线，因此模型的两件事必须钉死：
 *  - 已发布 / 未发布的分组（未发布的端口没有宿主端点，不能进拓扑）
 *  - 排序稳定（顺序抖动会让图形每次刷新都在跳）
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
        Port {QString(), 9000, 0, QStringLiteral("tcp")}, // 只 EXPOSE
    }));

    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.publishedPorts()->count(), 1);
    QCOMPARE(controller.unpublishedPorts()->count(), 1);
    QVERIFY(controller.publishedPorts()->index(0, 0).data(PortMappingModel::PublishedRole).toBool());
    QVERIFY(!controller.unpublishedPorts()->index(0, 0).data(PortMappingModel::PublishedRole).toBool());
    // 未发布的端口没有宿主端点：宿主侧文本必须为空，界面才好显示「未发布」
    QVERIFY(controller.unpublishedPorts()->index(0, 0).data(PortMappingModel::HostChipTextRole).toString().isEmpty());
}

void PortMappingModelTest::keepsOneToManyMappings()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    // 同一个容器端口绑定到两个宿主地址：拓扑里必须是两条线，不能合并
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

    // 引擎给的顺序是不可依赖的；界面按容器端口升序、同端口按协议排
    QCOMPARE(controller.publishedPorts()->index(0, 0).data(PortMappingModel::ContainerPortRole).toInt(), 53);
    QCOMPARE(controller.publishedPorts()->index(0, 0).data(PortMappingModel::ProtocolRole).toString(), QStringLiteral("tcp"));
    QCOMPARE(controller.publishedPorts()->index(1, 0).data(PortMappingModel::ProtocolRole).toString(), QStringLiteral("udp"));
    QCOMPARE(controller.publishedPorts()->index(2, 0).data(PortMappingModel::ContainerPortRole).toInt(), 443);
    QCOMPARE(controller.publishedPorts()->index(3, 0).data(PortMappingModel::ContainerPortRole).toInt(), 8443);

    // 重复刷新不改变顺序（否则拓扑图每次刷新都会重排）
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

    // 协议缺失时按 tcp 处理（引擎偶尔省略 Type）
    PortMappingEntry noProtocol;
    noProtocol.containerPort = 53;
    noProtocol.hostIp = QStringLiteral("0.0.0.0");
    noProtocol.hostPort = 53;
    QCOMPARE(noProtocol.containerChipText(), QStringLiteral("53/tcp"));

    // 宿主 IP 缺失时按 0.0.0.0 处理：不能显示成 ":8080"
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

QTEST_MAIN(PortMappingModelTest)

#include "tst_port_mapping_model.moc"
