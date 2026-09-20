/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/qml_registration.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace Kontainer;

/*!
 * Refresh-churn stress test for the segfault seen in a real user session.
 *
 * Background: kcmshell6 crashed in a live session; the core dump stack was
 *
 *     qmlAttachedPropertiesObject ← QQuickLayoutAttached::sizeHint
 *     ← QGridLayoutEngine::fillRowData ← QQuickLayout::effectiveSizeHints_helper
 *     ← QQuickLayout::updatePolish ← QQuickWindowPrivate::polishItems
 *
 * i.e. a dangling pointer while reading attached properties of layout entries
 * during layout polish. The trigger is "entries are destroyed and recreated while
 * the layout recomputes its sizes" -- which a Repeater hits whenever its model is
 * a JS array re-evaluated on every refresh: each data change rebuilds all delegates.
 *
 * This test hammers the three pages with real data changes (counts, container list,
 * storage usage) + window resizes + section switches: a recurrence crashes the
 * process here instead of waiting for a user session to expose it.
 *
 * It asserts "all iterations ran without a crash and without QML runtime errors",
 * so a crash shows up as an abnormal test process exit captured by ctest.
 */
class RefreshChurnTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void mainPageSurvivesDataChurn();
    void containerDetailSurvivesDataChurn();
    void imageDetailSurvivesDataChurn();

private:
    static void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message);
    static QQuickItem *childByObjectName(QQuickItem *root, const QString &objectName);
    static QStringList takeQmlErrors();

    /*! Build one page of data; iteration drives the values so every refresh really changes them. */
    void fillData(int iteration);

    std::unique_ptr<MockDockerBackend> m_backend;
    std::unique_ptr<QmlStubKcm> m_stubKcm;
    std::unique_ptr<QQmlEngine> m_engine;
};

namespace
{
QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;
} // namespace

void RefreshChurnTest::captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(type)
    Q_UNUSED(context)
    g_messages.append(message);
}

QStringList RefreshChurnTest::takeQmlErrors()
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

QQuickItem *RefreshChurnTest::childByObjectName(QQuickItem *root, const QString &objectName)
{
    if (!root) {
        return nullptr;
    }
    const QList<QQuickItem *> items = root->findChildren<QQuickItem *>();
    for (QQuickItem *item : items) {
        if (item->objectName() == objectName) {
            return item;
        }
    }
    return nullptr;
}

void RefreshChurnTest::initTestCase()
{
    setupTranslationDomain();
    registerKontainerQmlTypes();
    g_previousHandler = qInstallMessageHandler(&RefreshChurnTest::captureMessages);
}

void RefreshChurnTest::init()
{
    m_backend = std::make_unique<MockDockerBackend>();
    m_stubKcm = std::make_unique<QmlStubKcm>(m_backend.get());
    m_engine = std::make_unique<QQmlEngine>();
    m_engine->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                      "function i18nc(context, text) { return text; }\n"
                                      "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                      "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    m_engine->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
    g_messages.clear();
}

void RefreshChurnTest::cleanup()
{
    if (g_previousHandler) {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }
    m_engine.reset();
    m_stubKcm.reset();
    m_backend.reset();
}

/*!
 * Every iteration changes the data for real: count changes → MainPage re-evaluates
 * its stat-tile array; a change in the container list count → the list model resets;
 * storage entries flip between available / unavailable → StorageBar re-evaluates
 * its segment array.
 */
void RefreshChurnTest::fillData(int iteration)
{
    EngineInfo engine;
    engine.available = true;
    engine.countsAvailable = (iteration % 5) != 4; // occasionally report the summary as unavailable
    engine.serverVersion = QStringLiteral("29.8.0");
    engine.apiVersion = QStringLiteral("1.56");
    engine.minApiVersion = QStringLiteral("1.24");
    engine.osType = QStringLiteral("linux");
    engine.architecture = QStringLiteral("x86_64");
    engine.containerTotal = 3 + (iteration % 4);
    engine.containersRunning = iteration % 5;
    engine.containersPaused = iteration % 3;
    engine.containersStopped = iteration % 2;
    engine.imageCount = 2 + (iteration % 6);
    engine.memoryTotalBytes = 38ll * 1024 * 1024 * 1024;
    m_backend->setEngineInfo(engine);

    QList<Container> containers;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int containerCount = 1 + (iteration % 6);
    for (int i = 0; i < containerCount; ++i) {
        Container container;
        container.id = QStringLiteral("%1%2").arg(QString(63, QLatin1Char('a')).left(60)).arg(i);
        container.name = QStringLiteral("container-%1").arg(i);
        container.image = QStringLiteral("alpine:latest");
        container.status = QStringLiteral("Up %1 minutes").arg(i);
        container.state = (i % 3 == 0) ? ContainerState::Running : ((i % 3 == 1) ? ContainerState::Paused : ContainerState::Exited);
        container.health = (i % 2 == 0) ? HealthState::Healthy : HealthState::Unhealthy;
        container.created = now.addSecs(-60ll * (i + 1));
        containers.append(container);
    }
    m_backend->setContainers(containers);

    QList<Image> images;
    for (int i = 0; i < 1 + (iteration % 5); ++i) {
        Image image;
        image.id = QStringLiteral("sha256:%1").arg(QString(64, QLatin1Char('b')).left(60)).append(QString::number(i));
        if (i % 3 != 2) {
            image.repoTags = {QStringLiteral("image-%1:latest").arg(i)};
        }
        image.sizeBytes = (i + 1) * 64ll * 1024 * 1024;
        image.created = now.addDays(-i - 1);
        image.containerCount = i;
        image.inUse = i > 0;
        images.append(image);
    }
    m_backend->setImages(images);

    StorageUsage storage;
    storage.valid = true;
    storage.buildCacheAvailable = (iteration % 3) != 2; // the build cache comes and goes
    storage.imagesBytes = (iteration % 4 == 3) ? -1 : (iteration + 1) * 100ll * 1024 * 1024;
    storage.containersBytes = (iteration + 1) * 20ll * 1024 * 1024;
    storage.volumesBytes = (iteration % 5 == 4) ? -1 : (iteration + 1) * 300ll * 1024 * 1024;
    storage.buildCacheBytes = (iteration + 1) * 10ll * 1024 * 1024;
    storage.imageCount = images.size();
    storage.containerCount = containers.size();
    storage.volumeCount = iteration % 4;
    storage.buildCacheCount = iteration % 7;
    m_backend->setStorageUsage(storage);
}

void RefreshChurnTest::mainPageSurvivesDataChurn()
{
    fillData(0);
    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(900, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(900);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));

    // Window width changes reflow the stat tiles between 5/3/2 columns, exactly when entries are added/removed
    const QList<int> widths = {900, 640, 420, 1100, 520};
    for (int iteration = 1; iteration <= 40; ++iteration) {
        fillData(iteration);
        controller->refresh();
        m_backend->completeRefresh();

        if (iteration % 3 == 0) {
            window.resize(widths.at(iteration % widths.size()), 700);
            page->setWidth(widths.at(iteration % widths.size()));
        }
        if (iteration % 4 == 0 && tabBar) {
            tabBar->setProperty("currentIndex", iteration % 3);
        }
        QCoreApplication::processEvents();
    }

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

void RefreshChurnTest::containerDetailSurvivesDataChurn()
{
    QScopedPointer<QQuickWindow> window;

    for (int iteration = 0; iteration <= 30; ++iteration) {
        fillData(iteration);

        ContainerDetail detail;
        detail.id = QStringLiteral("cid-1");
        detail.name = QStringLiteral("demo-%1").arg(iteration);
        detail.state = (iteration % 2 == 0) ? ContainerState::Running : ContainerState::Exited;
        detail.health = (iteration % 3 == 0) ? HealthState::Healthy : HealthState::None;
        detail.status = QStringLiteral("Up %1 minutes").arg(iteration);
        detail.created = QDateTime::currentDateTimeUtc();
        detail.ports = {{QStringLiteral("0.0.0.0"), 80, quint16(8000 + iteration), QStringLiteral("tcp")}};
        detail.networks = {{QStringLiteral("bridge"), QStringLiteral("id"), QStringLiteral("172.17.0.%1").arg(iteration % 250), {}, {}, QStringLiteral("172.17.0.1")}};
        detail.mounts = {{QStringLiteral("bind"), QString(), QStringLiteral("/srv/%1").arg(iteration), QStringLiteral("/data"), QStringLiteral("rw"), false}};
        detail.environment = {QStringLiteral("A=%1").arg(iteration), QStringLiteral("B=2")};
        detail.labels = {{QStringLiteral("k%1").arg(iteration), QStringLiteral("v")}};
        m_backend->setContainerDetail(detail);

        const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {
                {QStringLiteral("containerId"), QStringLiteral("cid-1")},
            },
            m_engine->rootContext()));
        QVERIFY(!object.isNull());
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);

        m_backend->completeRefresh();

        // A fresh window every round: the page is entered and left repeatedly, as in real detail-page use
        window.reset(new QQuickWindow);
        window->resize(900, 700);
        page->setParentItem(window->contentItem());
        page->setWidth(900);
        page->setHeight(700);
        window->show();

        QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
        if (tabBar) {
            // Cycle through the five sections and expand/collapse the collapsible area in the overview
            tabBar->setProperty("currentIndex", iteration % 5);
            QCoreApplication::processEvents();
            QQuickItem *environment = childByObjectName(page, QStringLiteral("environmentValues"));
            if (environment) {
                environment->setProperty("visible", iteration % 2 == 0);
            }
        }
        QCoreApplication::processEvents();
    }

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

void RefreshChurnTest::imageDetailSurvivesDataChurn()
{
    QScopedPointer<QQuickWindow> window;

    for (int iteration = 0; iteration <= 30; ++iteration) {
        fillData(iteration);

        ImageDetail detail;
        detail.id = QStringLiteral("sha256:aaaa");
        detail.repoTags = {QStringLiteral("image-%1:latest").arg(iteration), QStringLiteral("image-%1:v2").arg(iteration)};
        detail.sizeBytes = (iteration + 1) * 32ll * 1024 * 1024;
        detail.created = QDateTime::currentDateTimeUtc();
        detail.architecture = QStringLiteral("amd64");
        detail.os = QStringLiteral("linux");
        // Layer count jumps between 0 / 3 / 20: the default 5-layer collapse logic is retriggered every time
        const int layerCount = (iteration % 3 == 0) ? 0 : ((iteration % 3 == 1) ? 3 : 20);
        for (int layer = 0; layer < layerCount; ++layer) {
            detail.layers.append(QStringLiteral("sha256:layer%1").arg(layer));
        }
        detail.environment = {QStringLiteral("PATH=/usr/bin")};
        m_backend->setImageDetail(detail);

        const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ImageDetail.qml");
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {
                {QStringLiteral("imageId"), QStringLiteral("sha256:aaaa")},
            },
            m_engine->rootContext()));
        QVERIFY(!object.isNull());
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);

        m_backend->completeRefresh();
        if (iteration % 2 == 0) {
            QMetaObject::invokeMethod(page, "toggleLayers");
        }

        window.reset(new QQuickWindow);
        window->resize(900, 700);
        page->setParentItem(window->contentItem());
        page->setWidth(900);
        page->setHeight(700);
        window->show();
        QCoreApplication::processEvents();
    }

    const QStringList errors = takeQmlErrors();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

QTEST_MAIN(RefreshChurnTest)

#include "tst_refresh_churn.moc"
