/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/qml_registration.h"
#include "support/qml_item_utils.h"
#include "model/metrics_model.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QtTest>

#include <functional>
#include <memory>

using namespace Kontainer;

/*!
 * kcmshell6 宿主形态下的刷新抖动测试（复现真实会话里的段错误）。
 *
 * 为什么需要单独一个测试文件：kcmshell6 把 KCM 的 QML 放进 **QQuickWidget**
 * （core dump 的调用栈里能看到 libQt6QuickWidgets → QQuickRenderControl::polishItems
 * → QQuickWindowPrivate::polishItems → QQuickLayout::updatePolish）。
 * QQuickWidget 走的是离屏渲染 + 手动 polish 的路径，与 QQuickWindow 不同；
 * 只用 QQuickWindow 的压力测试（tst_refresh_churn）无法覆盖这条路径。
 *
 * 崩溃点（用户实际 core dump）：
 *
 *     qmlAttachedPropertiesObject ← QQuickLayoutAttached::sizeHint
 *     ← QGridLayoutEngine::fillRowData ← QQuickLayout::effectiveSizeHints_helper
 *
 * 即在布局 polish 期间访问布局条目的附加属性时踩到悬空指针——
 * 典型触发条件是「布局正在算尺寸时，里面的条目被销毁重建」。
 *
 * 本测试因此做两件事：
 *   1. 用 QQuickWidget 承载真实入口 main.qml（含 StackView 导航）；
 *   2. 在总览 ↔ 容器详情之间反复进出，同时让数据持续变化、窗口反复缩放。
 * 任何一步再踩到悬空指针，进程都会在这里直接崩掉，而不是等到用户会话里。
 */
class KcmWidgetChurnTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void widgetHostedKcmSurvivesNavigationChurn();
    void delegatesSurviveDataChanges_data();
    void delegatesSurviveDataChanges();
    void silentRefreshesDoNotRecreateDetailEntries_data();
    void silentRefreshesDoNotRecreateDetailEntries();
    void statsSamplesDoNotRecreateTrendBars();
    void statsSamplesDoNotDestroyAnyItem();
    void listKeepsScrollPositionOnValueRefresh();

private:
    static void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message);
    static QStringList takeQmlErrors();
    static QQuickItem *childByObjectName(QQuickItem *root, const QString &objectName);

    void fillData(int iteration);
    /*!
     * 只改数值、不改结构（存储段数恒为 4、统计块恒为 5）：
     * 用来断言「同一结构下的数值刷新不得重建条目」。
     */
    void fillDataStableStructure(int iteration);
    /*! 固定 30 个容器，只让"运行时长"这种**值**变化（列表可滚动、结构不变）。 */
    void fillManyContainersWithChangingStatus(int iteration);

    std::unique_ptr<MockDockerBackend> m_backend;
    std::unique_ptr<QmlStubKcm> m_stubKcm;
};

namespace
{
QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;
} // namespace

void KcmWidgetChurnTest::captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(type)
    Q_UNUSED(context)
    g_messages.append(message);
}

QStringList KcmWidgetChurnTest::takeQmlErrors()
{
    QStringList errors;
    for (const QString &message : std::as_const(g_messages)) {
        if (message.contains(QLatin1String("ReferenceError")) || message.contains(QLatin1String("TypeError"))
            || message.contains(QLatin1String("is not defined")) || message.contains(QLatin1String("Unable to assign"))
            || message.contains(QLatin1String("Binding loop"))) {
            errors.append(message);
        }
    }
    g_messages.clear();
    return errors;
}

QQuickItem *KcmWidgetChurnTest::childByObjectName(QQuickItem *root, const QString &objectName)
{
    // 走可视树：Repeater 创建的 delegate 不在 QObject 树里（详见该助手头文件说明）
    return TestSupport::findItemByObjectName(root, objectName);
}

void KcmWidgetChurnTest::initTestCase()
{
    setupTranslationDomain();
    registerKontainerQmlTypes();
    g_previousHandler = qInstallMessageHandler(&KcmWidgetChurnTest::captureMessages);
}

void KcmWidgetChurnTest::init()
{
    m_backend = std::make_unique<MockDockerBackend>();
    m_stubKcm = std::make_unique<QmlStubKcm>(m_backend.get());
    g_messages.clear();
}

void KcmWidgetChurnTest::cleanup()
{
    if (g_previousHandler) {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }
    m_stubKcm.reset();
    m_backend.reset();
}

void KcmWidgetChurnTest::fillData(int iteration)
{
    EngineInfo engine;
    engine.available = true;
    engine.countsAvailable = (iteration % 4) != 3;
    engine.serverVersion = QStringLiteral("29.8.0");
    engine.apiVersion = QStringLiteral("1.56");
    engine.osType = QStringLiteral("linux");
    engine.architecture = QStringLiteral("x86_64");
    engine.containerTotal = 1 + (iteration % 5);
    engine.containersRunning = iteration % 4;
    engine.containersPaused = iteration % 2;
    engine.containersStopped = iteration % 3;
    engine.imageCount = 1 + (iteration % 6);
    m_backend->setEngineInfo(engine);

    QList<Container> containers;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int containerCount = 1 + (iteration % 7);
    for (int i = 0; i < containerCount; ++i) {
        Container container;
        container.id = QStringLiteral("cid-%1").arg(i);
        container.name = QStringLiteral("container-%1").arg(i);
        container.image = QStringLiteral("alpine:latest");
        container.imageId = QStringLiteral("sha256:aaaa");
        container.status = QStringLiteral("Up %1 minutes").arg(i);
        container.state = (i % 3 == 0) ? ContainerState::Running : ((i % 3 == 1) ? ContainerState::Paused : ContainerState::Exited);
        container.health = (i % 2 == 0) ? HealthState::Healthy : HealthState::Unhealthy;
        container.created = now.addSecs(-60ll * (i + 1));
        containers.append(container);
    }
    m_backend->setContainers(containers);

    ContainerDetail detail;
    detail.id = QStringLiteral("cid-0");
    detail.name = QStringLiteral("container-0");
    detail.state = (iteration % 2 == 0) ? ContainerState::Running : ContainerState::Exited;
    detail.health = (iteration % 3 == 0) ? HealthState::Healthy : HealthState::None;
    detail.status = QStringLiteral("Up %1 minutes").arg(iteration);
    detail.created = now;
    detail.started = now;
    detail.environment = {QStringLiteral("A=%1").arg(iteration), QStringLiteral("B=2"), QStringLiteral("C=3")};
    detail.labels = {{QStringLiteral("k%1").arg(iteration), QStringLiteral("v")}};
    detail.mounts = {{QStringLiteral("bind"), QString(), QStringLiteral("/srv/%1").arg(iteration), QStringLiteral("/data"), QStringLiteral("rw"), false}};
    detail.networks = {{QStringLiteral("bridge"), QStringLiteral("id"), QStringLiteral("172.17.0.%1").arg(iteration % 250), {}, {}, QStringLiteral("172.17.0.1")}};
    m_backend->setContainerDetail(detail);

    ImageDetail imageDetail;
    imageDetail.id = QStringLiteral("sha256:aaaa");
    imageDetail.repoTags = {QStringLiteral("alpine:latest"), QStringLiteral("alpine:3.21")};
    imageDetail.sizeBytes = 8ll * 1024 * 1024;
    imageDetail.created = now;
    imageDetail.architecture = QStringLiteral("amd64");
    imageDetail.os = QStringLiteral("linux");
    for (int layer = 0; layer < 3; ++layer) {
        imageDetail.layers.append(QStringLiteral("sha256:layer%1").arg(layer));
    }
    imageDetail.environment = {QStringLiteral("PATH=/usr/bin")};
    m_backend->setImageDetail(imageDetail);

    StorageUsage storage;
    storage.valid = true;
    storage.buildCacheAvailable = (iteration % 3) != 1;
    storage.imagesBytes = (iteration % 4 == 2) ? -1 : (iteration + 1) * 100ll * 1024 * 1024;
    storage.containersBytes = (iteration + 1) * 20ll * 1024 * 1024;
    storage.volumesBytes = (iteration + 1) * 300ll * 1024 * 1024;
    storage.buildCacheBytes = (iteration + 1) * 10ll * 1024 * 1024;
    m_backend->setStorageUsage(storage);
}

void KcmWidgetChurnTest::fillDataStableStructure(int iteration)
{
    fillData(iteration);

    StorageUsage storage;
    storage.valid = true;
    // 段数保持 4（构建缓存始终存在），且所有取值都有效 → 结构不变、只有数值在变
    storage.buildCacheAvailable = true;
    storage.imagesBytes = (iteration + 1) * 111ll * 1024 * 1024;
    storage.containersBytes = (iteration + 1) * 22ll * 1024 * 1024;
    storage.volumesBytes = (iteration + 1) * 333ll * 1024 * 1024;
    storage.buildCacheBytes = (iteration + 1) * 11ll * 1024 * 1024;
    storage.imageCount = 1 + (iteration % 6);
    storage.containerCount = 1 + (iteration % 5);
    storage.volumeCount = 1 + (iteration % 4);
    storage.buildCacheCount = 1 + (iteration % 7);
    m_backend->setStorageUsage(storage);
}

/*!
 * 用 QQuickWidget 承载 main.qml，然后反复：
 *   - 刷新数据（统计块、列表、存储区都会变）
 *   - 缩放窗口（统计块在 5/3/2 列之间切换、详情页正文在限宽与非限宽之间切换）
 *   - 进入 / 离开容器详情（页面创建与销毁）
 *   - 在详情分区之间切换
 */
void KcmWidgetChurnTest::widgetHostedKcmSurvivesNavigationChurn()
{
    fillData(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    // QQuickWidget 自带一个 QQmlEngine：i18n 桩与 kcm 上下文都必须注入到它上面
    // （真实运行时由 KCMUtils 的 KLocalizedQmlContext 提供 i18n）。
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();

    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml")));
    QVERIFY2(widget.status() != QQuickWidget::Error, qPrintable(widget.errors().isEmpty() ? QStringLiteral("failed to load main.qml") : widget.errors().constFirst().toString()));

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY(root->width() > 0);

    const QList<int> widths = {900, 640, 420, 1120, 520, 780};
    for (int iteration = 1; iteration <= 40; ++iteration) {
        fillData(iteration);
        controller->refresh();
        m_backend->completeRefresh();
        QCoreApplication::processEvents();

        // 进入容器详情：由 MainPage 发出导航信号（main.qml 里接到 StackView.push）
        QQuickItem *stack = childByObjectName(root, QStringLiteral("pageStack"));
        QVERIFY2(stack, "pageStack not found");
        // 同样必须走可视树：main.qml 的 MainPage 在 StackView 内部
        QQuickItem *mainPage = nullptr;
        const auto findMainPage = [root]() -> QQuickItem * {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item || found) {
                    return;
                }
                if (item->metaObject()->indexOfSignal("containerActivated(QString)") >= 0) {
                    found = item;
                    return;
                }
                const QList<QQuickItem *> children = item->childItems();
                for (QQuickItem *child : children) {
                    walk(child);
                }
            };
            walk(root);
            return found;
        };
        QTRY_VERIFY_WITH_TIMEOUT((mainPage = findMainPage()) != nullptr, 5000);
        QVERIFY(QMetaObject::invokeMethod(mainPage, "containerActivated", Q_ARG(QString, QStringLiteral("cid-0"))));
        QCoreApplication::processEvents();

        // 详情页里切换分区（QQuickWidget 下每一帧都要手动 polish）
        if (QQuickItem *detailTabs = childByObjectName(root, QStringLiteral("detailTabBar"))) {
            detailTabs->setProperty("currentIndex", iteration % 5);
            QCoreApplication::processEvents();
        }

        // 每两轮返回列表页（页面销毁），再重新进入
        if (iteration % 2 == 0) {
            QMetaObject::invokeMethod(stack, "pop");
            QCoreApplication::processEvents();
        }

        if (iteration % 3 == 0) {
            const int width = widths.at(iteration % widths.size());
            widget.resize(width, 700);
            QCoreApplication::processEvents();
        }
    }

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

/*!
 * 用户 core dump 的根因是「布局正在算尺寸时，布局内的条目被销毁重建」。
 * 因此这里把不变量直接断言出来：**数据变化不得重建统计块与存储图例的条目**。
 *
 * 只要有人再次把 JS 数组直接当作 Repeater 的 model（数组每次刷新都会重新求值），
 * 条目实例就会变，测试立刻失败——比「跑到用户那里崩溃」早得多。
 */
/*!
 * 刷新时列表不能被拉回最上方（用户实测）。
 *
 * 场景：容器很多（这里 30 个，可滚动），用户滚到中间；后台刷新只改了
 * "运行时长/状态文本"这类**值**。旧实现每次刷新都 `beginResetModel()`，
 * 而模型重置必然让 ListView 回到顶部——用户来不及翻到目标。
 *
 * 断言两件事：① 这次刷新**没有**发生 modelReset（根因）；② contentY 原地不动（用户可见结果）。
 * 只看 contentY 是不够的：页面上还有一段"重置后恢复 contentY"的兜底代码，
 * 它在新内容尚未布局完时会被夹到 0，正是线上表现。
 */
void KcmWidgetChurnTest::listKeepsScrollPositionOnValueRefresh()
{
    fillManyContainersWithChangingStatus(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QTest::qWait(50);

    QQuickItem *page = widget.rootObject();
    QVERIFY(page);
    QTRY_VERIFY_WITH_TIMEOUT(page->width() > 0 && page->height() > 0, 5000);

    QQuickItem *view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = TestSupport::findItemByObjectName(page, QStringLiteral("containerView"))) != nullptr, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->property("count").toInt(), 30, 5000);

    // 模型重置是"被拉回顶部"的根因：这一路刷新里一次都不该出现
    QSignalSpy resetSpy(view->property("model").value<QObject *>(), SIGNAL(modelReset()));
    QVERIFY(resetSpy.isValid());

    // 滚到中间（内容够高才滚得动）
    QTRY_VERIFY_WITH_TIMEOUT(view->property("contentHeight").toReal() > view->height(), 5000);
    view->setProperty("contentY", 400.0);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(view->property("contentY").toReal() - 400.0) < 1.0, 3000);

    for (int iteration = 1; iteration <= 3; ++iteration) {
        fillManyContainersWithChangingStatus(iteration);
        controller->refresh();
        m_backend->completeRefresh();
        QTest::qWait(40);

        QCOMPARE(resetSpy.count(), 0);
        QVERIFY2(qAbs(view->property("contentY").toReal() - 400.0) < 1.0,
                 qPrintable(QStringLiteral("iteration %1: the list jumped to %2 (must stay at 400)")
                                .arg(iteration)
                                .arg(view->property("contentY").toReal())));
    }
}

void KcmWidgetChurnTest::fillManyContainersWithChangingStatus(int iteration)
{
    EngineInfo engine;
    engine.available = true;
    engine.countsAvailable = true;
    engine.serverVersion = QStringLiteral("29.8.0");
    engine.apiVersion = QStringLiteral("1.56");
    engine.containerTotal = 30;
    engine.containersRunning = 30;
    m_backend->setEngineInfo(engine);

    QList<Container> containers;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (int i = 0; i < 30; ++i) {
        Container container;
        container.id = QStringLiteral("cid-%1").arg(i);
        container.name = QStringLiteral("container-%1").arg(i, 2, 10, QLatin1Char('0'));
        container.image = QStringLiteral("alpine:latest");
        container.imageId = QStringLiteral("sha256:aaaa");
        // 只有这里在变：运行时长/状态文本
        container.status = QStringLiteral("Up %1 minutes").arg(i + iteration);
        container.state = ContainerState::Running;
        container.health = HealthState::Healthy;
        container.created = now.addSecs(-60ll * (i + 1));
        containers.append(container);
    }
    m_backend->setContainers(containers);
}


void KcmWidgetChurnTest::delegatesSurviveDataChanges_data()
{
    QTest::addColumn<QString>("objectName");

    QTest::newRow("statTile") << QStringLiteral("statTile");
    QTest::newRow("storageLegendSwatch") << QStringLiteral("storageLegendEntry");
}

void KcmWidgetChurnTest::delegatesSurviveDataChanges()
{
    QFETCH(QString, objectName);

    fillDataStableStructure(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QTest::qWait(50);

    QQuickItem *page = widget.rootObject();
    QVERIFY(page);
    // 必须等根条目真正拿到尺寸：ScrollView 的 contentItem 在尺寸为 0 时不会建立内容树
    QTRY_VERIFY_WITH_TIMEOUT(page->width() > 0 && page->height() > 0, 5000);
    QTest::qWait(100);

    // 必须走可视树：Repeater 创建的 delegate 不在 QObject 树里
    const auto firstEntry = [page, &objectName]() -> QQuickItem * {
        return TestSupport::findItemByObjectName(page, objectName);
    };

    QQuickItem *before = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((before = firstEntry()) != nullptr, 5000);

    // 只让**数值**连续变化：结构（条目数量）不变，因此条目实例必须复用。
    // 条目数量变化时重建是正常的（那是另一回事，由导航抖动用例覆盖）。
    for (int iteration = 1; iteration <= 6; ++iteration) {
        fillDataStableStructure(iteration);
        controller->refresh();
        m_backend->completeRefresh();
        QTest::qWait(20);
    }

    QQuickItem *after = firstEntry();
    QVERIFY2(after, qPrintable(objectName + QStringLiteral(" disappeared")));
    QVERIFY2(after == before,
             qPrintable(QStringLiteral("%1 was recreated on data change: a Repeater model is not stable "
                                       "(a JS array re-evaluates on every refresh and destroys its delegates "
                                       "during layout polish)")
                            .arg(objectName)));
}

/*!
 * 崩溃路径的精确回归：**数据没变的刷新不得重建详情页里的条目**。
 *
 * 详情页的列表会被周期性重建：
 *   - 容器列表每 5 秒刷新一次 → ImageDetailController::rebuildUsedBy()
 *   - inspect 复核每 30 秒一次 → ContainerDetailController::rebuildLists()
 * 修复前 DetailListModel::setEntries 无条件发 modelReset，QML 的 Repeater
 * 因此反复销毁重建 delegate（网络条目还是一个 FormLayout），
 * 而「布局正在算尺寸时条目被销毁」正是段错误的触发条件。
 */
void KcmWidgetChurnTest::silentRefreshesDoNotRecreateDetailEntries_data()
{
    QTest::addColumn<QString>("page");
    QTest::addColumn<QString>("signalName");
    QTest::addColumn<QString>("signalArgument");
    QTest::addColumn<QString>("objectName");

    QTest::newRow("container networks") << QStringLiteral("container") << QStringLiteral("containerActivated") << QStringLiteral("cid-0")
                                        << QStringLiteral("networkEntry");
    QTest::newRow("container mounts") << QStringLiteral("container") << QStringLiteral("containerActivated") << QStringLiteral("cid-0")
                                      << QStringLiteral("mountEntry");
    QTest::newRow("image used-by containers") << QStringLiteral("image") << QStringLiteral("imageActivated")
                                             << QStringLiteral("sha256:aaaa") << QStringLiteral("usedByEntry");
    QTest::newRow("image layers") << QStringLiteral("image") << QStringLiteral("imageActivated") << QStringLiteral("sha256:aaaa")
                                  << QStringLiteral("layerEntry");
}

void KcmWidgetChurnTest::silentRefreshesDoNotRecreateDetailEntries()
{
    QFETCH(QString, page);
    QFETCH(QString, signalName);
    QFETCH(QString, signalArgument);
    QFETCH(QString, objectName);

    // 固定一份数据：下面的刷新都是「数据没变」的静默刷新
    fillData(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml")));
    QTest::qWait(80);

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(root->width() > 0, 5000);

    // StackView 的 initialItem 需要一次布局之后才建立，因此这里轮询等待
    const QByteArray signalSignature = (signalName + QStringLiteral("(QString)")).toUtf8();
    const auto findMainPage = [root, &signalSignature]() -> QQuickItem * {
        QQuickItem *found = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item || found) {
                return;
            }
            if (item->metaObject()->indexOfSignal(signalSignature.constData()) >= 0) {
                found = item;
                return;
            }
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                walk(child);
            }
        };
        walk(root);
        return found;
    };

    QQuickItem *mainPage = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((mainPage = findMainPage()) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(mainPage, signalName.toUtf8().constData(), Q_ARG(QString, signalArgument)));
    // 详情页进入后会发起 inspect，必须交付这一轮结果，页面才有真正的数据
    m_backend->completeRefresh();

    QQuickItem *before = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((before = TestSupport::findItemByObjectName(root, objectName)) != nullptr, 5000);
    Q_UNUSED(page);

    // 静默刷新若干轮：数据完全没变，但两条重建路径都要走到
    //   - 容器列表刷新 → ImageDetailController::rebuildUsedBy()
    //   - inspect 复核   → ContainerDetailController::rebuildLists()
    for (int round = 0; round < 5; ++round) {
        controller->refresh();
        controller->containerDetail()->refresh();
        controller->imageDetail()->refresh();
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    QQuickItem *after = TestSupport::findItemByObjectName(root, objectName);
    QVERIFY2(after, qPrintable(objectName + QStringLiteral(" disappeared")));
    QVERIFY2(after == before,
             qPrintable(QStringLiteral("%1 was recreated by a silent refresh: the detail lists must not reset "
                                       "the model when the data is unchanged (ARCH_V2 §32/§34)")
                            .arg(objectName)));

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

/*!
 * 崩溃路径的精确回归（ARCH_V3 附录 A.1g）：
 * **资源采样的趋势柱不得被重建**。
 *
 * 容器详情页开着时，MetricsModel 每 5 秒产生一次新的 *History（QVariantList）。
 * 如果趋势柱的 Repeater 直接以该数组为 model，每 5 秒就会销毁重建最多 60 个柱子；
 * 这些柱子位于 Kirigami.FormLayout（GridLayout）的条目里，
 * 正是真实会话中「布局算尺寸时条目被销毁 → qmlAttachedPropertiesObject 段错误」的形状。
 * 这也是「点进详情页看一会儿才崩」的原因。
 */
void KcmWidgetChurnTest::statsSamplesDoNotRecreateTrendBars()
{
    fillData(0); // 容器状态为 Running，才会启动资源采样
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml")));
    QTest::qWait(60);

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(root->width() > 0, 5000);

    // 进入容器详情
    const QByteArray signalSignature("containerActivated(QString)");
    const auto findMainPage = [root, &signalSignature]() -> QQuickItem * {
        QQuickItem *found = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item || found) {
                return;
            }
            if (item->metaObject()->indexOfSignal(signalSignature.constData()) >= 0) {
                found = item;
                return;
            }
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                walk(child);
            }
        };
        walk(root);
        return found;
    };
    QQuickItem *mainPage = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((mainPage = findMainPage()) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(mainPage, "containerActivated", Q_ARG(QString, QStringLiteral("cid-0"))));
    m_backend->completeRefresh(); // 交付 inspect

    // 切到「资源」分区（下标 1）
    QQuickItem *detailTabs = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((detailTabs = TestSupport::findItemByObjectName(root, QStringLiteral("detailTabBar"))) != nullptr, 5000);
    detailTabs->setProperty("currentIndex", 1);
    QTest::qWait(50);

    // 先喂几次采样，让趋势线出现（MiniTrend 在样本数 > 1 时才可见）
    for (int sample = 0; sample < 3; ++sample) {
        m_backend->requestContainerStats(QStringLiteral("cid-0"));
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    // 收集全部趋势柱（3 条趋势线各有自己的柱子），断言**已有实例不被销毁**：
    // 断言"第一个实例不变"会被"新增柱子"误判，因此按集合判断。
    const auto collectBars = [root]() -> QList<QQuickItem *> {
        QList<QQuickItem *> bars;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                if (child->objectName() == QLatin1String("trendBar")) {
                    bars.append(child);
                }
                walk(child);
            }
        };
        walk(root);
        return bars;
    };

    QList<QQuickItem *> before;
    QTRY_VERIFY_WITH_TIMEOUT(!(before = collectBars()).isEmpty(), 5000);
    const int sampleCountBefore = controller->containerDetail()->metrics()->sampleCount();

    // 继续采样：每轮都是一次新的 *History 数组
    for (int sample = 0; sample < 6; ++sample) {
        m_backend->requestContainerStats(QStringLiteral("cid-0"));
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    const QList<QQuickItem *> after = collectBars();
    const int sampleCountAfter = controller->containerDetail()->metrics()->sampleCount();
    QVERIFY2(sampleCountAfter > sampleCountBefore, "stats samples were not delivered to the metrics model");

    QStringList lost;
    for (QQuickItem *bar : before) {
        if (!after.contains(bar)) {
            lost.append(QStringLiteral("%1").arg(reinterpret_cast<quintptr>(bar), 0, 16));
        }
    }
    QVERIFY2(lost.isEmpty(),
             qPrintable(QStringLiteral("%1 of %2 trend bars were destroyed by a stats sample "
                                       "(sampleCount %3 → %4): the Repeater model must be the array length, "
                                       "not the array itself (a new QVariantList arrives every 5 seconds)")
                            .arg(lost.size())
                            .arg(before.size())
                            .arg(sampleCountBefore)
                            .arg(sampleCountAfter)));

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

/*!
 * 更宽的不变量（ARCH_V3 附录 A.1g）：**只发生资源采样时，界面上不得有任何条目被销毁**。
 *
 * 容器详情页开着时，采样每 5 秒一次；只要有一次采样导致某个条目被销毁，
 * 就可能撞上「布局算尺寸时条目被析构」的崩溃路径。这里把「保留所有已有条目」
 * 直接断言出来，比逐个组件写断言更能覆盖到未来新加的界面元素。
 */
void KcmWidgetChurnTest::statsSamplesDoNotDestroyAnyItem()
{
    fillData(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml")));
    QTest::qWait(60);

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(root->width() > 0, 5000);

    const QByteArray signalSignature("containerActivated(QString)");
    QQuickItem *mainPage = nullptr;
    const auto findMainPage = [root, &signalSignature]() -> QQuickItem * {
        QQuickItem *found = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item || found) {
                return;
            }
            if (item->metaObject()->indexOfSignal(signalSignature.constData()) >= 0) {
                found = item;
                return;
            }
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                walk(child);
            }
        };
        walk(root);
        return found;
    };
    QTRY_VERIFY_WITH_TIMEOUT((mainPage = findMainPage()) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(mainPage, "containerActivated", Q_ARG(QString, QStringLiteral("cid-0"))));
    m_backend->completeRefresh();

    QQuickItem *detailTabs = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((detailTabs = TestSupport::findItemByObjectName(root, QStringLiteral("detailTabBar"))) != nullptr, 5000);
    detailTabs->setProperty("currentIndex", 1); // 资源分区
    QTest::qWait(50);

    const auto collectItems = [root]() -> QSet<QQuickItem *> {
        QSet<QQuickItem *> items;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                items.insert(child);
                walk(child);
            }
        };
        walk(root);
        return items;
    };

    // 先喂两次采样，让趋势线进入"有数据"的状态
    for (int sample = 0; sample < 2; ++sample) {
        m_backend->requestContainerStats(QStringLiteral("cid-0"));
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    const QSet<QQuickItem *> before = collectItems();
    QVERIFY(before.size() > 50);

    for (int sample = 0; sample < 6; ++sample) {
        m_backend->requestContainerStats(QStringLiteral("cid-0"));
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    const QSet<QQuickItem *> after = collectItems();
    QSet<QQuickItem *> destroyed = before;
    destroyed.subtract(after);
    QVERIFY2(destroyed.isEmpty(),
             qPrintable(QStringLiteral("%1 items were destroyed by stats samples; "
                                       "any item destroyed during layout polish can hit the crash path "
                                       "(see ARCH_V3 appendix A.1g)").arg(destroyed.size())));

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

QTEST_MAIN(KcmWidgetChurnTest)

#include "tst_kcm_widget_churn.moc"
