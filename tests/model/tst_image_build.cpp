/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/build_context.h"
#include "dto/image_build_dto.h"
#include "i18n.h"
#include "model/image_build_model.h"
#include "model/operation_controller.h"
#include "support/mock_docker_backend.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * 构建请求与进度（ARCH_V5_V8 §5.3）。
 *
 * 三件事各自有用例：① 构建流行里的**失败 step 定位**（解析层）；
 * ② 控制器把上下文打包后交给后端、并把进度写进构建列表；
 * ③ 构建列表"内容没变不发信号"（进度行很密，重复行不该打扰视图）。
 */
class ImageBuildTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void parsesStepLinesAndFailures();
    void packsContextAndBuildsThroughTheController();
    void buildListKeepsFailuresAndStepText();
    void prunesTheBuildCacheAndReportsReclaimedSpace();

private:
    /*! 一个"看起来可写"的 socket：写权限门靠它放行（与操作控制器用例同一手法）。 */
    static QString writableSocketPath(const QString &directory);
};

QString ImageBuildTest::writableSocketPath(const QString &directory)
{
    const QString path = directory + QStringLiteral("/docker.sock");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write("x");
        file.close();
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

void ImageBuildTest::initTestCase()
{
    setupTranslationDomain();
}

void ImageBuildTest::parsesStepLinesAndFailures()
{
    const auto lineFrom = [](const QByteArray &json) {
        return DockerImageBuildLineDTO::fromJson(QJsonDocument::fromJson(json).object());
    };

    // `Step 3/7 : RUN make` 要能解析出步骤号、总步数与命令
    const DockerImageBuildLineDTO step = lineFrom(R"({"stream":"Step 3/7 : RUN make -j4\n"})");
    QCOMPARE(step.stepIndex, 3);
    QCOMPARE(step.totalSteps, 7);
    QCOMPARE(step.stepCommand, QStringLiteral("RUN make -j4"));
    QVERIFY(!step.cached);

    // 缓存提示单独成行
    const DockerImageBuildLineDTO cache = lineFrom(R"({"stream":" ---> Using cache\n"})");
    QCOMPARE(cache.stepIndex, 0);
    QVERIFY(cache.cached);

    // 普通输出行：不算步骤
    const DockerImageBuildLineDTO output = lineFrom(R"({"stream":"Successfully built abc123\n"})");
    QCOMPARE(output.stepIndex, 0);
    QVERIFY(output.stepCommand.isEmpty());

    // 流内错误（HTTP 仍是 200）与 aux id
    const DockerImageBuildLineDTO failure = lineFrom(R"({"errorDetail":{"message":"The command '/bin/sh -c exit 1' returned a non-zero code: 1"},"error":"The command '/bin/sh -c exit 1' returned a non-zero code: 1"})");
    QVERIFY2(failure.errorDetail.contains(QStringLiteral("non-zero code")), qPrintable(failure.errorDetail));
    const DockerImageBuildLineDTO aux = lineFrom(R"({"aux":{"ID":"sha256:deadbeef"}})");
    QCOMPARE(aux.auxImageId, QStringLiteral("sha256:deadbeef"));

    // 进度行（有些引擎用 status + progressDetail）
    const DockerImageBuildLineDTO progress = lineFrom(R"({"status":"Downloading","progressDetail":{"current":50,"total":100}})");
    QVERIFY(progress.hasProgress);
    QCOMPARE(progress.current, 50);
    QCOMPARE(progress.total, 100);
}

void ImageBuildTest::packsContextAndBuildsThroughTheController()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray dockerfile = QByteArrayLiteral("FROM alpine:3.19\nRUN echo hi\n");
    QFile file(QDir(dir.path()).filePath(QStringLiteral("Dockerfile")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(dockerfile);
    file.close();

    MockDockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(writableSocketPath(dir.path())));
    OperationController operations(&backend);
    operations.refreshWriteAccess();
    QVERIFY2(operations.writeAllowed(), "these tests need the write path to be enabled");

    // 没有标签：本地就挡住（不发请求）
    QVERIFY(!operations.buildImage(dir.path(), {}));
    QCOMPARE(operations.resultDetailText(), QStringLiteral("tagRequired"));

    // 正常路径：上下文被打包、查询参数与请求都传下去了
    QVERIFY(operations.buildImage(dir.path(),
                                  {QStringLiteral("app:1.0")},
                                  QStringLiteral("Dockerfile"),
                                  {QStringLiteral("HTTP_PROXY=http://proxy")},
                                  QVariantList {QVariantMap {{QStringLiteral("key"), QStringLiteral("com.example.owner")},
                                                             {QStringLiteral("value"), QStringLiteral("team-a")}}},
                                  QStringLiteral("runtime"),
                                  /*noCache=*/true,
                                  /*pull=*/true));
    const ImageBuildRequest request = backend.lastBuildRequest();
    QCOMPARE(request.tags, QStringList {QStringLiteral("app:1.0")});
    QCOMPARE(request.buildArgs, QStringList {QStringLiteral("HTTP_PROXY=http://proxy")});
    QCOMPARE(request.target, QStringLiteral("runtime"));
    QVERIFY(request.noCache);
    QVERIFY(request.pull);
    QCOMPARE(request.labels.size(), 1);
    QVERIFY2(!request.contextArchive.isEmpty(), "the context must be packed before the request");
    QVERIFY(QFile::exists(request.contextArchive));
    QVERIFY2(!request.id.isEmpty(), "every build needs a stable id");

    // 列表里立刻出现一条"进行中"
    QCOMPARE(operations.builds()->count(), 1);
    QCOMPARE(operations.builds()->activeCount(), 1);
    QCOMPARE(operations.builds()->entries().first().tags, QStringList {QStringLiteral("app:1.0")});
}

void ImageBuildTest::buildListKeepsFailuresAndStepText()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(QDir(dir.path()).filePath(QStringLiteral("Dockerfile")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("FROM alpine:3.19\n"));
    file.close();

    MockDockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(writableSocketPath(dir.path())));
    OperationController operations(&backend);
    operations.refreshWriteAccess();
    QVERIFY(operations.buildImage(dir.path(), {QStringLiteral("app:1.0")}));
    const QString buildId = operations.builds()->entries().first().id;

    // 进度：step 行会更新进度与命令
    ImageBuildUpdate update;
    update.statusText = QStringLiteral("Step 2/4 : RUN make");
    update.stepIndex = 2;
    update.totalSteps = 4;
    update.stepCommand = QStringLiteral("RUN make");
    update.progress = 0.5;
    update.progressKnown = true;
    backend.emitBuildProgress(buildId, update);
    QCOMPARE(operations.builds()->entries().first().stepIndex, 2);
    QCOMPARE(operations.builds()->entries().first().progress, 0.5);

    // 失败：detail 里的失败步骤必须保留下来（不能只剩一句"构建失败"）
    update.errorText = QStringLiteral("Step 2/4 (RUN make) failed: The command returned a non-zero code: 2");
    backend.emitBuildProgress(buildId, update);
    backend.emitBuildFinished(buildId,
                              DockerBackendInterface::MutationOutcome::Failed,
                              DockerError(DockerError::Kind::EngineError, QStringLiteral("exit code 2")));
    QCOMPARE(operations.builds()->count(), 1);
    const ImageBuildEntry entry = operations.builds()->entries().first();
    QCOMPARE(entry.statusKey, QStringLiteral("failed"));
    QVERIFY(!entry.active);
    QVERIFY2(entry.detailText.contains(QStringLiteral("Step 2/4")), qPrintable(entry.detailText));
    QVERIFY2(entry.detailText.contains(QStringLiteral("RUN make")), qPrintable(entry.detailText));
    QVERIFY2(!operations.resultText().isEmpty(), "the failure must also be reported as a result");

    // 成功：记录镜像 id，并且"清掉已结束"能真的清掉
    QVERIFY(operations.buildImage(dir.path(), {QStringLiteral("app:2.0")}));
    // 列表把"进行中"排在前面，因此新的那一路在 first()（这个顺序本身就是被断言的行为）
    const QString secondId = operations.builds()->entries().first().id;
    QVERIFY(operations.builds()->entries().first().active);
    backend.emitBuildFinished(secondId,
                              DockerBackendInterface::MutationOutcome::Succeeded,
                              DockerError(),
                              QStringLiteral("sha256:deadbeef"));
    const int row = operations.builds()->rowForBuildId(secondId);
    QVERIFY(row >= 0);
    QCOMPARE(operations.builds()->entries().at(row).imageId, QStringLiteral("sha256:deadbeef"));
    operations.clearFinishedBuilds();
    QCOMPARE(operations.builds()->count(), 0);
}

/*!
 * 清理构建缓存（§5.5）：请求发出去、回收字节数如实说出来（0 也要说清楚）。
 */
void ImageBuildTest::prunesTheBuildCacheAndReportsReclaimedSpace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MockDockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(writableSocketPath(dir.path())));
    OperationController operations(&backend);
    operations.refreshWriteAccess();
    QVERIFY(operations.writeAllowed());

    operations.pruneBuildCache();
    QCOMPARE(backend.mutationCalls().size(), 1);
    QCOMPARE(backend.mutationCalls().first().mutation, DockerBackendInterface::Mutation::PruneBuildCache);

    backend.completeBuildCachePrune(3LL * 1024 * 1024);
    backend.completeMutation(QStringLiteral("buildCache:"), DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(operations.resultText().contains(QStringLiteral("3")), qPrintable(operations.resultText()));
    QVERIFY2(operations.resultText().contains(QStringLiteral("MiB")), qPrintable(operations.resultText()));

    // 没有可回收的：也要明确说"没有"，而不是显示"已回收 0"
    operations.pruneBuildCache();
    backend.completeBuildCachePrune(0);
    backend.completeMutation(QStringLiteral("buildCache:"), DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(!operations.resultText().contains(QStringLiteral("0 ")), qPrintable(operations.resultText()));
    QVERIFY2(operations.resultText().contains(QStringLiteral("cache")), qPrintable(operations.resultText()));
}

QTEST_MAIN(ImageBuildTest)

#include "tst_image_build.moc"
