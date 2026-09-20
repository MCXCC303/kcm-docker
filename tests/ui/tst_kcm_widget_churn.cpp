/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Refresh-churn test in the kcmshell6 host shape (reproduces the real-session segfault).
 *
 * Why a separate file: kcmshell6 puts the KCM's QML into a **QQuickWidget**
 * (the core dump stack shows libQt6QuickWidgets → QQuickRenderControl::polishItems
 * → QQuickWindowPrivate::polishItems → QQuickLayout::updatePolish).
 * QQuickWidget renders offscreen and polishes manually, unlike QQuickWindow, so the
 * QQuickWindow-only stress test (tst_refresh_churn) does not cover this path.
 *
 * Crash site (the user's actual core dump):
 *
 *     qmlAttachedPropertiesObject ← QQuickLayoutAttached::sizeHint
 *     ← QGridLayoutEngine::fillRowData ← QQuickLayout::effectiveSizeHints_helper
 *
 * i.e. a dangling pointer while reading attached properties of layout entries
 * during layout polish -- typically triggered when "entries are destroyed and
 * recreated while the layout computes its sizes".
 *
 * The test therefore does two things:
 *   1. hosts the real entry point main.qml (with StackView navigation) in a QQuickWidget;
 *   2. repeatedly enters and leaves overview ↔ container detail while the data keeps
 *      changing and the window is resized repeatedly.
 * Stepping on a dangling pointer again crashes the process right here instead of
 * waiting for a user session.
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
     * Values only, structure unchanged (always 4 storage segments, always 5 stat tiles):
     * asserts "a value refresh under the same structure must not recreate entries".
     */
    void fillDataStableStructure(int iteration);
    /*! Fixed 30 containers; only a **value** such as "uptime" changes (list scrollable, structure fixed). */
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
    // Walk the visual tree: Repeater delegates are not in the QObject tree (see that helper's header)
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
    // 4 segments throughout (the build cache is always present), every value valid → only values move
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
 * Host main.qml in a QQuickWidget, then repeatedly:
 *   - refresh data (stat tiles, list and storage area all change)
 *   - resize the window (stat tiles switch between 5/3/2 columns; the detail body toggles its width cap)
 *   - enter / leave container detail (page created and destroyed)
 *   - switch between detail sections
 */
void KcmWidgetChurnTest::widgetHostedKcmSurvivesNavigationChurn()
{
    fillData(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    QQuickWidget widget;
    widget.setResizeMode(QQuickWidget::SizeRootObjectToView);
    // QQuickWidget brings its own QQmlEngine: the i18n stub and the kcm context must go into that one
    // (at runtime KCMUtils' KLocalizedQmlContext provides i18n).
    widget.engine()->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                            "function i18nc(context, text) { return text; }\n"
                                            "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                            "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    widget.engine()->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    widget.resize(900, 700);
    widget.show();

    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml")));
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

        // Enter container detail: MainPage emits the navigation signal (wired to StackView.push in main.qml)
        QQuickItem *stack = childByObjectName(root, QStringLiteral("pageStack"));
        QVERIFY2(stack, "pageStack not found");
        // The visual tree again: main.qml's MainPage lives inside the StackView
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

        // Switch sections in the detail page (under QQuickWidget every frame must be polished manually)
        if (QQuickItem *detailTabs = childByObjectName(root, QStringLiteral("detailTabBar"))) {
            detailTabs->setProperty("currentIndex", iteration % 5);
            QCoreApplication::processEvents();
        }

        // Go back to the list page every two rounds (page destroyed), then re-enter
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
 * The user's core dump came from "entries inside a layout are destroyed and recreated
 * while the layout computes its sizes", so the invariant is asserted directly here:
 * **a data change must not recreate the stat tiles or storage legend entries**.
 *
 * If anyone again passes a JS array straight to a Repeater as its model (an array is
 * re-evaluated on every refresh), the entry instances change and the test fails at once
 * -- far earlier than "it crashes on a user's machine".
 */
/*!
 * A refresh must not pull the list back to the top (measured in real use).
 *
 * Scenario: many containers (30 here, scrollable) with the user scrolled to the middle;
 * a background refresh only changed **values** like "uptime/status text". The old
 * implementation called `beginResetModel()` on every refresh, and a model reset always
 * sends the ListView back to the top -- the user never reaches the target.
 *
 * Asserts two things: ① this refresh did **not** emit modelReset (the root cause);
 * ② contentY stays put (the user-visible result).
 * contentY alone is not enough: the page also has a fallback that "restores contentY
 * after a reset", and it gets clamped to 0 before the new content is laid out -- the
 * exact production symptom.
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
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QTest::qWait(50);

    QQuickItem *page = widget.rootObject();
    QVERIFY(page);
    QTRY_VERIFY_WITH_TIMEOUT(page->width() > 0 && page->height() > 0, 5000);

    QQuickItem *view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = TestSupport::findItemByObjectName(page, QStringLiteral("containerView"))) != nullptr, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(view->property("count").toInt(), 30, 5000);

    // A model reset is the root cause of "pulled back to the top": it must never occur in these refreshes
    QSignalSpy resetSpy(view->property("model").value<QObject *>(), SIGNAL(modelReset()));
    QVERIFY(resetSpy.isValid());

    // Scroll to the middle (only scrollable when the content is tall enough)
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
        // Only this changes: uptime/status text
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
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QTest::qWait(50);

    QQuickItem *page = widget.rootObject();
    QVERIFY(page);
    // The root item must get a real size first: a ScrollView contentItem of size 0 builds no content tree
    QTRY_VERIFY_WITH_TIMEOUT(page->width() > 0 && page->height() > 0, 5000);
    QTest::qWait(100);

    // The visual tree again: delegates created by a Repeater are not in the QObject tree
    const auto firstEntry = [page, &objectName]() -> QQuickItem * {
        return TestSupport::findItemByObjectName(page, objectName);
    };

    QQuickItem *before = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((before = firstEntry()) != nullptr, 5000);

    // Change **values** only: the structure (entry count) stays fixed, so the entry instances must be reused.
    // Rebuilding when the entry count changes is normal (covered by the navigation churn case).
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
 * Exact regression of the crash path: **a refresh that changes no data must not rebuild
 * the entries on the detail page**.
 *
 * The detail page lists are rebuilt periodically:
 *   - container list refreshed every 5 seconds → ImageDetailController::rebuildUsedBy()
 *   - inspect re-check every 30 seconds → ContainerDetailController::rebuildLists()
 * Before the fix DetailListModel::setEntries emitted modelReset unconditionally, so the
 * QML Repeater destroyed and recreated its delegates over and over (network entries are
 * even a FormLayout) -- and "an entry is destroyed while the layout computes its sizes"
 * is exactly the segfault trigger.
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

    // One fixed dataset: every refresh below is a "no data changed" silent refresh
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
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml")));
    QTest::qWait(80);

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(root->width() > 0, 5000);

    // StackView's initialItem only appears after one layout pass, so poll for it here
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
    // Entering the detail page issues an inspect; this round must be delivered for the page to hold real data
    m_backend->completeRefresh();

    QQuickItem *before = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((before = TestSupport::findItemByObjectName(root, objectName)) != nullptr, 5000);
    Q_UNUSED(page);

    // Several silent refresh rounds: the data is unchanged, but both rebuild paths must be exercised
    //   - container list refresh → ImageDetailController::rebuildUsedBy()
    //   - inspect re-check      → ContainerDetailController::rebuildLists()
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
 * Exact regression of the crash path (ARCH_V3 appendix A.1g):
 * **the trend bars of the stats samples must not be rebuilt**.
 *
 * While the container detail page is open, MetricsModel produces a new *History
 * (QVariantList) every 5 seconds. If the trend-bar Repeater took that array as its model
 * directly, up to 60 bars would be destroyed and recreated every 5 seconds; those bars sit
 * inside Kirigami.FormLayout (GridLayout) entries -- precisely the shape of "an entry is
 * destroyed while the layout computes its sizes → qmlAttachedPropertiesObject segfault"
 * from the real session. It is also why the crash came "a while after opening the detail
 * page".
 */
void KcmWidgetChurnTest::statsSamplesDoNotRecreateTrendBars()
{
    fillData(0); // the container must be Running for stats sampling to start
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
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml")));
    QTest::qWait(60);

    QQuickItem *root = widget.rootObject();
    QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(root->width() > 0, 5000);

    // Enter container detail
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
    m_backend->completeRefresh(); // deliver the inspect

    // Switch to the "stats" section (index 1)
    QQuickItem *detailTabs = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((detailTabs = TestSupport::findItemByObjectName(root, QStringLiteral("detailTabBar"))) != nullptr, 5000);
    detailTabs->setProperty("currentIndex", 1);
    QTest::qWait(50);

    // Feed a few samples first so the trend lines appear (MiniTrend is visible only with more than 1 sample)
    for (int sample = 0; sample < 3; ++sample) {
        m_backend->requestContainerStats(QStringLiteral("cid-0"));
        m_backend->completeRefresh();
        QTest::qWait(30);
    }

    // Collect every trend bar (each of the 3 trend lines has its own) and assert no existing instance dies:
    // asserting "the first instance is unchanged" would misjudge newly added bars, so compare as a set.
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

    // Keep sampling: every round is a new *History array
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
 * The broader invariant (ARCH_V3 appendix A.1g): **when only stats samples happen, no item
 * on screen may be destroyed**.
 *
 * While the container detail page is open, samples arrive every 5 seconds; a single sample
 * destroying an item can hit the "entry destructed while the layout computes its sizes"
 * crash path. Asserting "keep every existing item" directly covers future UI elements
 * better than per-component assertions.
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
    widget.setSource(QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml")));
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
    detailTabs->setProperty("currentIndex", 1); // stats section
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

    // Feed two samples first so the trend lines enter the "has data" state
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
