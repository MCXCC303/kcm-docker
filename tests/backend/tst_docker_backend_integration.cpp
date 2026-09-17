/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"
#include "domain/image_pull_progress.h"
#include "i18n.h"
#include "model/docker_error_text.h"

#include <QFileInfo>
#include <QtTest>

using namespace Kontainer;

/*!
 * 针对真实 Docker Engine 的集成测试（ARCH_V1 §29 / ARCH_V4 §5.3）。
 *
 * 默认**只做 GET**；如果本机没有可用的 Docker socket，则整体跳过
 * （CI 不应依赖开发者的 Docker 数据目录）。
 *
 * 唯一的例外是显式 opt-in 的拉取测试（`pullsAnImageWhenExplicitlyRequested`）：
 * 自动化测试绝不擅自修改用户的 Docker 资源，因此它需要
 * `KONTAINER_PULL_TEST=1`，并且默认拉取一个**已经存在**的小镜像
 * （重复拉取对镜像库是无副作用的），引用可以用
 * `KONTAINER_PULL_REFERENCE` 覆盖。
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
    void pullsAnImageWhenExplicitlyRequested();
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


/*!
 * 真实 daemon 上的拉取（ARCH_V4 §2.4 / §5.3，opt-in）。
 *
 * 覆盖自动化测试里最难伪造的两件事：真实引擎的 chunked 进度流长什么样，
 * 以及「流内 error 行 / 正常结束」在真实实现下如何被判定。
 *
 * 默认拉取 `quay.io/libpod/alpine:latest`（podman 常用的公共小镜像）：
 * 如果本地已经有它，重复拉取只会返回「已是最新」，不会向镜像库新增任何东西。
 * 想换镜像请设置 `KONTAINER_PULL_REFERENCE`。
 *
 * 运行方式：
 *     KONTAINER_PULL_TEST=1 ./bin/tst_docker_backend_integration pullsAnImageWhenExplicitlyRequested
 */
void DockerBackendIntegrationTest::pullsAnImageWhenExplicitlyRequested()
{
    if (!qEnvironmentVariableIsSet("KONTAINER_PULL_TEST")) {
        QSKIP("opt-in: set KONTAINER_PULL_TEST=1 to allow a real image pull");
    }

    const DockerEndpoint endpoint = DockerEndpoint::fromEnvironment();
    if (!endpoint.isValid() || !QFileInfo::exists(endpoint.socketPath())) {
        QSKIP("no Docker socket available on this machine");
    }

    const QString reference = qEnvironmentVariable("KONTAINER_PULL_REFERENCE", QStringLiteral("quay.io/libpod/alpine:latest"));

    DockerBackend backend;
    backend.setEndpoint(endpoint);

    QList<ImagePullProgress> progress;
    connect(&backend, &DockerBackend::imagePullProgress, this, [&progress](const ImagePullProgress &update) {
        progress.append(update);
    });
    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);

    backend.pullImage(reference);
    // 首次拉取可能要下载若干 MB；给足时间，但仍然有上限
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 180000);

    const auto outcome = finishedSpy.at(0).at(2).value<DockerBackendInterface::MutationOutcome>();
    const DockerError error = finishedSpy.at(0).at(3).value<DockerError>();
    QVERIFY2(outcome == DockerBackendInterface::MutationOutcome::Succeeded,
             qPrintable(QStringLiteral("pull failed: kind=%1 http=%2 detail=%3")
                            .arg(int(error.kind()))
                            .arg(error.httpStatus())
                            .arg(error.detail())));

    QVERIFY2(!progress.isEmpty(), "a real pull must report at least one progress line");
    QVERIFY(!progress.last().reference.isEmpty());
    bool sawComplete = false;
    for (const ImagePullProgress &update : progress) {
        if (update.phase == ImagePullProgress::Phase::Complete) {
            sawComplete = true;
        }
    }
    QVERIFY2(sawComplete, "the pull stream must end with a completed state");
}

QTEST_GUILESS_MAIN(DockerBackendIntegrationTest)

#include "tst_docker_backend_integration.moc"
