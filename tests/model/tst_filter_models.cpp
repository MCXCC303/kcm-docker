/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_filter_model.h"
#include "model/container_model.h"
#include "model/image_filter_model.h"
#include "model/image_model.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * 搜索 / 过滤 / 排序测试（ARCH_V2 §9/§10/§32/§44 Filter）。
 *
 * 条件组合、大小写不敏感、默认排序、以及“刷新不重置条件”都在这里验证。
 */
class FilterModelsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- 容器 ---
    void searchesByNameIdAndImage();
    void searchIsCaseInsensitive();
    void filtersByState();
    void combinesStateAndSearch();
    void sortsByNameByDefault();
    void sortsByStateAndCreated();
    void conditionsSurviveModelReset();

    // --- 镜像 ---
    void searchesImageByRepositoryTagAndId();
    void filtersImagesByUsage();
    void sortsImagesByRepositoryByDefault();
};

namespace
{

Container makeContainer(const QString &name, const QString &id, const QString &image, ContainerState state, const QDateTime &created)
{
    Container container;
    container.name = name;
    container.id = id;
    container.image = image;
    container.state = state;
    container.created = created;
    return container;
}

QList<Container> sampleContainers()
{
    const QDateTime base = QDateTime::fromSecsSinceEpoch(1'789'000'000, QTimeZone::UTC);
    return {
        makeContainer(QStringLiteral("zeta"), QStringLiteral("aaaaaaaaaaaa1111"), QStringLiteral("nginx:latest"), ContainerState::Running, base.addSecs(30)),
        makeContainer(QStringLiteral("alpha"), QStringLiteral("bbbbbbbbbbbb2222"), QStringLiteral("postgres:18"), ContainerState::Exited, base.addSecs(10)),
        makeContainer(QStringLiteral("medai-worker"), QStringLiteral("cccccccccccc3333"), QStringLiteral("registry.bohrium.dp.tech/practice-py39:medai"), ContainerState::Paused, base.addSecs(20)),
        makeContainer(QStringLiteral("dead-one"), QStringLiteral("dddddddddddd4444"), QStringLiteral("alpine:latest"), ContainerState::Dead, base),
    };
}

} // namespace

void FilterModelsTest::searchesByNameIdAndImage()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());
    QCOMPARE(filter.count(), 4);

    filter.setSearchText(QStringLiteral("alpha"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("alpha"));

    // ID 前缀
    filter.setSearchText(QStringLiteral("cccccccc"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("medai-worker"));

    // 镜像名
    filter.setSearchText(QStringLiteral("postgres"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("alpha"));

    filter.setSearchText(QString());
    QCOMPARE(filter.count(), 4);
}

void FilterModelsTest::searchIsCaseInsensitive()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    filter.setSearchText(QStringLiteral("ALPHA"));
    QCOMPARE(filter.count(), 1);
    filter.setSearchText(QStringLiteral("MeDaI"));
    QCOMPARE(filter.count(), 1);
}

void FilterModelsTest::filtersByState()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    filter.setStateFilter(QStringLiteral("running"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("paused"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("stopped"));
    QCOMPARE(filter.count(), 1); // exited
    filter.setStateFilter(QStringLiteral("dead"));
    QCOMPARE(filter.count(), 1);
    filter.setStateFilter(QStringLiteral("restarting"));
    QCOMPARE(filter.count(), 0);
    filter.setStateFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 4);
}

void FilterModelsTest::combinesStateAndSearch()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    // State == Running AND search matches（§9.4：条件不能互相覆盖）
    filter.setStateFilter(QStringLiteral("running"));
    filter.setSearchText(QStringLiteral("zeta"));
    QCOMPARE(filter.count(), 1);

    filter.setSearchText(QStringLiteral("alpha")); // alpha 是 exited
    QCOMPARE(filter.count(), 0);

    filter.setStateFilter(QStringLiteral("stopped"));
    QCOMPARE(filter.count(), 1);
}

void FilterModelsTest::sortsByNameByDefault()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    QCOMPARE(filter.sortKey(), QStringLiteral("name")); // §10：默认排序固定为 Name 升序
    QCOMPARE(filter.index(0, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(filter.index(1, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("dead-one"));
}

void FilterModelsTest::sortsByStateAndCreated()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    filter.setSortKey(QStringLiteral("state"));
    QCOMPARE(filter.index(0, 0).data(ContainerModel::StateKeyRole).toString(), QStringLiteral("dead"));

    filter.setSortKey(QStringLiteral("created"));
    // created 降序：最新在前
    const QDateTime first = filter.index(0, 0).data(ContainerModel::CreatedRole).toDateTime();
    const QDateTime last = filter.index(filter.count() - 1, 0).data(ContainerModel::CreatedRole).toDateTime();
    QVERIFY(first > last);
}

void FilterModelsTest::conditionsSurviveModelReset()
{
    ContainerModel source;
    ContainerFilterModel filter;
    filter.setSourceModel(&source);
    source.setContainers(sampleContainers());

    filter.setStateFilter(QStringLiteral("running"));
    filter.setSearchText(QStringLiteral("zeta"));
    filter.setSortKey(QStringLiteral("created"));
    QCOMPARE(filter.count(), 1);

    // 后台刷新：source model 被整体重置（§32）
    source.setContainers(sampleContainers());

    QCOMPARE(filter.searchText(), QStringLiteral("zeta"));
    QCOMPARE(filter.stateFilter(), QStringLiteral("running"));
    QCOMPARE(filter.sortKey(), QStringLiteral("created"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(ContainerModel::NameRole).toString(), QStringLiteral("zeta"));
}

void FilterModelsTest::searchesImageByRepositoryTagAndId()
{
    ImageModel source;
    ImageFilterModel filter;
    filter.setSourceModel(&source);

    Image windows;
    windows.id = QStringLiteral("sha256:0cff9eb0e7aee9953e55bc682852ca4fdca233145a58ae1ec94f0b0c01a2ed30");
    windows.repoTags = {QStringLiteral("ghcr.io/dockur/windows:6.05")};
    windows.containerCount = 1;
    windows.inUse = true;

    Image dangling;
    dangling.id = QStringLiteral("sha256:deadbeef0000");
    dangling.containerCount = -1;

    source.setImages({windows, dangling});
    QCOMPARE(filter.count(), 2);

    filter.setSearchText(QStringLiteral("dockur"));
    QCOMPARE(filter.count(), 1);
    filter.setSearchText(QStringLiteral("6.05"));
    QCOMPARE(filter.count(), 1);
    filter.setSearchText(QStringLiteral("DEADBEEF"));
    QCOMPARE(filter.count(), 1);
    filter.setSearchText(QString());
    QCOMPARE(filter.count(), 2);
}

void FilterModelsTest::filtersImagesByUsage()
{
    ImageModel source;
    ImageFilterModel filter;
    filter.setSourceModel(&source);

    Image used;
    used.id = QStringLiteral("sha256:1111");
    used.repoTags = {QStringLiteral("nginx:latest")};
    used.inUse = true;
    used.containerCount = 2;

    Image dangling;
    dangling.id = QStringLiteral("sha256:2222");
    dangling.containerCount = -1;

    source.setImages({used, dangling});

    filter.setUseFilter(QStringLiteral("in-use"));
    QCOMPARE(filter.count(), 1);
    filter.setUseFilter(QStringLiteral("dangling"));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.index(0, 0).data(ImageModel::DanglingRole).toBool(), true);
    filter.setUseFilter(QStringLiteral("all"));
    QCOMPARE(filter.count(), 2);
}

void FilterModelsTest::sortsImagesByRepositoryByDefault()
{
    ImageModel source;
    ImageFilterModel filter;
    filter.setSourceModel(&source);

    Image beta;
    beta.id = QStringLiteral("sha256:bbbb");
    beta.repoTags = {QStringLiteral("zeta/app:1")};
    beta.sizeBytes = 100;

    Image alpha;
    alpha.id = QStringLiteral("sha256:aaaa");
    alpha.repoTags = {QStringLiteral("alpha/app:1")};
    alpha.sizeBytes = 200;

    source.setImages({beta, alpha});

    QCOMPARE(filter.sortKey(), QStringLiteral("repository")); // §10：默认 Repository 升序
    QCOMPARE(filter.index(0, 0).data(ImageModel::PrimaryTagRole).toString(), QStringLiteral("alpha/app:1"));

    filter.setSortKey(QStringLiteral("size"));
    QCOMPARE(filter.index(0, 0).data(ImageModel::PrimaryTagRole).toString(), QStringLiteral("alpha/app:1")); // 200 > 100，降序
}

QTEST_GUILESS_MAIN(FilterModelsTest)

#include "tst_filter_models.moc"
