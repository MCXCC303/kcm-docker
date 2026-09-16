/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"
#include "i18n.h"
#include "model/docker_error_text.h"

#include <QFileInfo>
#include <QtTest>

using namespace Kontainer;

/*!
 * 针对真实 Docker Engine 的只读集成测试（ARCH_V1 §29）。
 *
 * 只做 GET；如果本机没有可用的 Docker socket，则整体跳过（CI 不应依赖开发者的
 * Docker 数据目录）。测试不修改任何 Docker 状态。
 */
class DockerBackendIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void readsEngineStatus();
    void readsContainerList();
    void readsImageList();
    void missingSocketProducesClearError();
};

void DockerBackendIntegrationTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");

    const DockerEndpoint endpoint = DockerEndpoint::fromEnvironment();
    if (!endpoint.isValid() || !QFileInfo::exists(endpoint.socketPath())) {
        QSKIP("no Docker socket available on this machine");
    }
}

void DockerBackendIntegrationTest::readsEngineStatus()
{
    DockerBackend backend;
    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const EngineInfo info = backend.engineInfo();
    QVERIFY(info.available);
    QVERIFY(!info.serverVersion.isEmpty());
    QVERIFY(!info.apiVersion.isEmpty());
    QVERIFY(info.countsAvailable);
    QVERIFY(info.containerTotal >= info.containersRunning);
    QVERIFY(!backend.isLoading());
}

void DockerBackendIntegrationTest::readsContainerList()
{
    DockerBackend backend;
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Container> containers = backend.containers();
    for (const Container &container : containers) {
        QVERIFY(!container.id.isEmpty());
        QVERIFY(!container.name.isEmpty());
        QVERIFY(!container.shortId().isEmpty());
        QVERIFY(container.created.isValid());
    }

    // chunked 响应必须被完整解码为合法 JSON 数组（能解析出条目即说明解码正确）
    const EngineInfo info = backend.engineInfo();
    if (info.countsAvailable) {
        QCOMPARE(containers.size(), info.containerTotal);
    }
}

void DockerBackendIntegrationTest::readsImageList()
{
    DockerBackend backend;
    QSignalSpy imagesSpy(&backend, &DockerBackend::imagesUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(imagesSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Image> images = backend.images();
    for (const Image &image : images) {
        QVERIFY(!image.id.isEmpty());
        QVERIFY(!image.shortId().isEmpty());
        QVERIFY(image.created.isValid());
    }

    const EngineInfo info = backend.engineInfo();
    if (info.countsAvailable) {
        QCOMPARE(images.size(), info.imageCount);
    }
}

void DockerBackendIntegrationTest::missingSocketProducesClearError()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/nonexistent/kontainer-test.sock")));

    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() + failureSpy.count() > 0, 10000);

    // 任何后端失败都必须是可观察的（§23.1），且带可理解的用户文本
    QCOMPARE(failureSpy.count(), 1);
    const auto arguments = failureSpy.first();
    const auto error = arguments.at(1).value<DockerError>();
    QCOMPARE(int(error.kind()), int(DockerError::Kind::DockerUnavailable));
    QVERIFY(!dockerErrorText(error).isEmpty());
    QVERIFY(!backend.engineInfo().available);
}

QTEST_GUILESS_MAIN(DockerBackendIntegrationTest)

#include "tst_docker_backend_integration.moc"
