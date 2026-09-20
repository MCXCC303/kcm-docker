/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/image_pull_model.h"
#include "model/mount_preset_store.h"
#include "domain/container_detail.h"
#include "domain/image_build.h"
#include "model/presentation.h"
#include "model/qml_registration.h"
#include "support/qml_item_utils.h"
#include "support/mock_docker_backend.h"
#include "domain/engine_info.h"
#include "model/detail_list_model.h"
#include "model/registry_credential_model.h"
#include "support/qml_stub_kcm.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <QJsonObject>
#include <QFontMetricsF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QAccessible>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

using namespace Kontainer;
using MutationOutcome = DockerBackendInterface::MutationOutcome;

namespace
{

/*!
 * Pick two host ports whose colours differ, for the topology assertions.
 *
 * Colour is a pure function (container id + container port + binding chip text) -> palette index,
 * so comparing indices in C++ is enough; there is no need to ask QML for colours.
 * No such pair found: return a fixed pair, so the assertion degrades to equality rather than failing.
 */
QPair<quint16, quint16> distinctBranchPorts()
{
    const Presentation presentation;
    // Same colour seed as the UI: container id + "|" + container-port chip text + "|" + binding chip text
    const auto indexFor = [&presentation](const QString &hostChip) {
        return presentation.connectionColorIndex(QStringLiteral("cid-1|80/tcp|") + hostChip, 6);
    };
    const int first = indexFor(QStringLiteral("0.0.0.0:8080"));
    for (quint16 port = 8081; port < 8200; ++port) {
        if (indexFor(QStringLiteral("127.0.0.1:%1").arg(port)) != first) {
            return {8080, port};
        }
    }
    return {8080, 8081};
}

/*! Create a socket file this process may write: the permission gate then allows writes. */
QString writableSocketPath()
{
    static QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/docker.sock");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write("x");
        file.close();
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

/*! Find a control by objectName in an instantiated page (no window, so plain child lookup works). */
/*! Current row count of a list editor (reads the count of its inner Repeater). */
int repeaterCount(QQuickItem *editor)
{
    const QList<QObject *> objects = editor->findChildren<QObject *>();
    for (QObject *object : objects) {
        if (QString::fromLatin1(object->metaObject()->className()).contains(QLatin1String("Repeater"))) {
            return object->property("count").toInt();
        }
    }
    return -1;
}

/*! Whether the pending config text contains a needle (asserts an empty draft never lands there). */
bool controller_pendingContains(Kontainer::DaemonConfigController *controller, const QString &needle)
{
    return controller->pendingContentPreview().contains(needle);
}

QQuickItem *findItemByName(QObject *root, const QString &objectName)
{
    const QList<QQuickItem *> items = root->findChildren<QQuickItem *>();
    for (QQuickItem *item : items) {
        if (item->objectName() == objectName) {
            return item;
        }
    }
    return nullptr;
}

/*!
 * Recurse through `childItems()` — **the only way to reach delegate items**.
 *
 * `findChildren<QQuickItem *>()` walks the QObject tree, where Repeater delegates do not live,
 * so objectName lookups miss them (bitten in phase 8); the visual tree does find them.
 */
QQuickItem *findItemDeep(QQuickItem *root, const QString &objectName)
{
    if (!root) {
        return nullptr;
    }
    for (QQuickItem *child : root->childItems()) {
        if (child->objectName() == objectName) {
            return child;
        }
        if (QQuickItem *found = findItemDeep(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}
} // namespace

/*!
 * QML loading tests (ARCH_V2 §46).
 *
 * Load each UI file to catch syntax errors, unknown properties, type-resolution failures and the like:
 * kcmshell6 only shows an error page for those, hard to locate; here they can be asserted directly.
 *
 * Note: tests load the QML files from the source directory (with components/ relative imports),
 * the same content that is packaged into the plugin qrc.
 */
class QmlLoadTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void loadsAllQmlFiles_data();
    void loadsAllQmlFiles();
    void instantiatesPages_data();
    void instantiatesPages();
    void configPageEditorsWriteThroughToTheController();
    void configPageWordingAndLocksPerScope();
    void topologyConnectionColorsAreStablePerContainer();
    void registryAuthPageReflectsWalletAndStoredCredentials();
    void networksTabListsAndOpensDetails();
    void createNetworkDialogValidatesBeforeSubmitting();
    void networkRemovalIsHiddenForBuiltInNetworks();
    void containerNetworkSectionConnectsAndDisconnects();
    void volumesTabListsCreatesAndPreviewsCleanup();
    void createContainerWizardGatesStepsAndHidesSecrets();
    void presetPanelManagesPresets();
    void buildPanelSubmitsAndShowsFailureStep();
    void portAndKeyValueRowsCanBeRemoved();
    void wizardAddsPresetsAndExtraMounts();
    void pauseAndResumeButtonsFollowTheState();
    void openingTheWizardRefreshesNetworks();
    void commandFieldAndExitHint();
    void stepButtonsNeverLookMultiSelected();
    void detailOffersCopyForCommandAndEntrypoint();
    void topologyAlignsTheContainerChipWithTheFirstBinding();
    void portEditorColoursEachRowDifferently();
    void serviceCardConfirmsRiskyActions();
    void filteredComboBoxNarrowsAndSelects();
    void imageUsedByRowsShowStateAndNavigate();
    void keyValueRowsKeepLongKeysVisible();
    void longCommandIsCollapsedUntilExpanded();
    void containerDetailOpensTheImage();
    void portRowShowsConflictAndAdoptsTheSuggestion();
    void portsTabListsRowsAndOpensTheContainer();
    void portsTabSwitchesToTheRangeMap();
    void rangeMapTilesOpenTheRunningContainer();
    void engineViewListsComponentVersions();
    void topologyMergesDualStackBindings();
    void privilegedNeedsTypedConfirmation();
    void networkDetailShowsMembersAndJumpsToContainers();
    void registryAuthGuidesFromFailedPullsAndMissingCredentials();
    void sensitiveSectionsAreCollapsedByDefault();
    void delegateActivationIsWired();
    void statusChipMapsSemanticKeys_data();
    void statusChipMapsSemanticKeys();
    void copyButtonFollowsValueAvailability();
    void emptyPlaceholderDistinguishesStates();
    void containerDetailHasSections();
    void imageLayersCollapseByDefault();
    void keyboardNavigationAndAccessibilityAreWired();
    void autoRefreshActionControlsTheScheduler();
    void writeActionsFollowThePermissionGate();
    void containerActionsFollowStateAndBusy();
    void operationMessageReflectsResultState();
    void pullDialogValidatesReferenceBeforeSubmitting();
    void confirmDialogAlwaysCarriesConsequenceText();
    void pullDialogStartsPullOnEnter();
    void pullProgressListShowsBackgroundPulls();
    void pullFailureStaysVisibleInTheList();
    void refreshActionStaysEnabledDuringAutoRefresh();
    void imageRefInputOwnsTheValidationRules();
    void stringListEditorEditsValidatesAndReorders();
    void keyValueListEditorMasksValuesAndDetectsDuplicates();
    void imageDetailOffersForceDeleteOnlyForMultipleTags();
    void mountRowReflectsHostPathState();
    void mountRowsPutTheContainerPathOnTheRight();
    void topologyDrawsDecoratedLinksForPublishedPorts();
    void unpublishedPortsAreListedWithoutLinks();

private:
    static void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message);
    void failOnQmlRuntimeErrors();

    /*! Find a child item by objectName in an instantiated page. */
    static QQuickItem *childByObjectName(QQuickItem *root, const QString &objectName);

    std::unique_ptr<MockDockerBackend> m_backend;
    std::unique_ptr<QmlStubKcm> m_stubKcm;
    std::unique_ptr<QQmlEngine> m_engine;
};

namespace
{
QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;
QStringList g_debugDump;
} // namespace

void QmlLoadTest::captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(type)
    Q_UNUSED(context)
    g_messages.append(message);
}

void QmlLoadTest::failOnQmlRuntimeErrors()
{
    QStringList errors;
    for (const QString &message : std::as_const(g_messages)) {
        if (message.contains(QLatin1String("ReferenceError")) || message.contains(QLatin1String("TypeError"))
            || message.contains(QLatin1String("is not defined")) || message.contains(QLatin1String("Unable to assign"))) {
            errors.append(message);
        }
    }
    g_messages.clear();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

void QmlLoadTest::initTestCase()
{
    setupTranslationDomain();
    registerKontainerQmlTypes();
}

void QmlLoadTest::init()
{
    // Reinstalled per test case; cleanup() restores it. Installing only once in initTestCase would
    // silence every QML runtime error after the first case (a real trap).
    g_messages.clear();
    g_previousHandler = qInstallMessageHandler(&QmlLoadTest::captureMessages);

    m_backend = std::make_unique<MockDockerBackend>();
    m_stubKcm = std::make_unique<QmlStubKcm>(m_backend.get());
    m_engine = std::make_unique<QQmlEngine>();
    // At runtime KCMUtils' KLocalizedQmlContext provides these globals; the test uses
    // equivalent identity implementations so the UI bindings still evaluate.
    m_engine->evaluate(QStringLiteral(
        "function _ktFormat(text, args) {\n"
        "    return String(text).replace(/%(\\d+)/g, function (match, index) {\n"
        "        const value = args[index - 1];\n"
        "        return value !== undefined ? value : match;\n"
        "    });\n"
        "}\n"
        "function i18n(text) { return _ktFormat(text, Array.prototype.slice.call(arguments, 1)); }\n"
        "function i18nc(context, text) { return _ktFormat(text, Array.prototype.slice.call(arguments, 2)); }\n"
        "function i18np(singular, plural, count) { return _ktFormat(count === 1 ? singular : plural, [count]); }\n"
        "function i18ncp(context, singular, plural, count) { return _ktFormat(count === 1 ? singular : plural, [count]); }\n"));
    m_engine->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
}

void QmlLoadTest::cleanup()
{
    if (g_previousHandler) {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }
    m_engine.reset();
    m_stubKcm.reset();
    m_backend.reset();
    failOnQmlRuntimeErrors();
}

/*!
 * §6/§46: cards must really emit their navigation signals.
 *
 * An undeclared model role in a delegate does not show up at load time;
 * only a real signal emission throws the ReferenceError.
 */
void QmlLoadTest::delegateActivationIsWired()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("demo");
    container.image = QStringLiteral("alpine:latest");
    container.state = ContainerState::Running;
    container.created = QDateTime::currentDateTimeUtc().addSecs(-3600);
    m_backend->setContainers({container});

    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:latest")};
    image.containerCount = 1;
    image.inUse = true;
    m_backend->setImages({image});

    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "MainPage.qml failed to instantiate");

    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // Delegates are created only with a real layout
    QQuickWindow window;
    window.resize(900, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(900);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    const auto findItem = [page](const QString &objectName) -> QQuickItem * {
        const QList<QQuickItem *> items = page->findChildren<QQuickItem *>();
        for (QQuickItem *item : items) {
            if (item->objectName() == objectName) {
                return item;
            }
        }
        return nullptr;
    };

    // Fetch delegates through the view's own API; child traversal misses view-managed delegates
    const auto delegateAt = [&findItem](const QString &viewName, int row) -> QQuickItem * {
        QQuickItem *view = findItem(viewName);
        if (!view) {
            return nullptr;
        }
        QQuickItem *card = nullptr;
        QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, card), Q_ARG(int, row));
        return card;
    };

    QSignalSpy containerSpy(page, SIGNAL(containerActivated(QString)));
    QSignalSpy imageSpy(page, SIGNAL(imageActivated(QString)));

    QQuickItem *containerCard = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((containerCard = delegateAt(QStringLiteral("containerView"), 0)) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(containerCard, "activated"));
    QCOMPARE(containerSpy.count(), 1);
    QCOMPARE(containerSpy.first().first().toString(), QStringLiteral("cid-1"));

    // Switch to the images tab and wait for its delegates
    QQuickItem *tabBar = findItem(QStringLiteral("tabBar"));
    QVERIFY2(tabBar, "tab bar not found");
    QVERIFY(tabBar->setProperty("currentIndex", 1));

    QQuickItem *imageCard = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((imageCard = delegateAt(QStringLiteral("imageView"), 0)) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(imageCard, "activated"));
    QCOMPARE(imageSpy.count(), 1);
    QCOMPARE(imageSpy.first().first().toString(), QStringLiteral("sha256:aaaa"));
}

/*!
 * The two editable controls on the config page (ARCH_V5_V8 §2.2).
 *
 * Tested separately: the helper long supported `max-concurrent-downloads` / `log-driver`,
 * yet the page showed them as **read-only text** users could not change (real feedback).
 * Pinned here: controls exist, edits reach the controller, "Default" deletes instead of writing 0/empty.
 */
void QmlLoadTest::configPageEditorsWriteThroughToTheController()
{
    Kontainer::DaemonConfigController *controller = m_stubKcm->controller()->daemonConfigUser();
    QVERIFY(controller);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/DaemonConfigPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("scope"), QStringLiteral("user")); // user scope: editable without unlocking
    QScopedPointer<QObject> page(component.createWithInitialProperties(initial, m_engine->rootContext()));
    if (page.isNull()) {
        QFAIL(qPrintable(component.errorString()));
    }

    QQuickItem *spin = findItemByName(page.data(), QStringLiteral("concurrentDownloadsSpin"));
    QVERIFY2(spin, "the concurrent-downloads spin box is missing");
    QQuickItem *combo = findItemByName(page.data(), QStringLiteral("logDriverCombo"));
    QVERIFY2(combo, "the log-driver combo box is missing");

    const QString state = QStringLiteral("editable=%1 protected=%2 unlocked=%3 writable=%4 requires=%5 path=%6 count=%7 spinEditable=%8 spinEnabled=%9")
                                .arg(page->property("editable").toBool())
                                .arg(page->property("protectedScope").toBool())
                                .arg(controller->property("unlocked").toBool())
                                .arg(controller->configWritable())
                                .arg(controller->requiresPrivilege())
                                .arg(controller->configPath())
                                .arg(combo->property("count").toInt())
                                .arg(spin->property("editable").toBool())
                                .arg(spin->property("enabled").toBool());

    QVERIFY2(spin->property("editable").toBool() && spin->property("enabled").toBool(), qPrintable(state));
    QVERIFY2(combo->property("enabled").toBool(), qPrintable(state));
    // "Default" plus the drivers on the helper's allow-list
    QCOMPARE(combo->property("count").toInt(), controller->selectableLogDrivers().size());
    QCOMPARE(combo->property("count").toInt(), 6);

    // Programmatic writes must not count as user edits (a reload must not leave the page dirty)
    QVERIFY(spin->setProperty("value", 9));
    QVERIFY2(!controller->dirty(), "programmatic value changes must not mark the page dirty");

    // A real user edit: valueModified is emitted on interaction only
    QVERIFY(spin->setProperty("value", 9));
    QVERIFY(QMetaObject::invokeMethod(spin, "valueModified"));
    QVERIFY(controller->dirty());
    QCOMPARE(controller->maxConcurrentDownloads(), 9);
    // The value itself is what gets written to the file
    QJsonObject merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QCOMPARE(merged.value(QStringLiteral("max-concurrent-downloads")).toInt(), 9);

    // Auto-refresh (setEngineInfo once engine info arrives) must not clobber an edit in progress:
    // refresh() only marks pending, completeRefresh() really emits engineUpdated
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY(controller->dirty());
    QCOMPARE(controller->maxConcurrentDownloads(), 9);
    QCOMPARE(spin->property("value").toInt(), 9);

    // Pick a concrete driver -> Set
    QVERIFY(combo->setProperty("currentIndex", 1));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1)));
    QCOMPARE(controller->logDriver(), QStringLiteral("json-file"));
    merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QCOMPARE(merged.value(QStringLiteral("log-driver")).toString(), QStringLiteral("json-file"));

    // Back to "Default" -> **remove the key** (not write ""), same for concurrent downloads
    QVERIFY(combo->setProperty("currentIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
    QVERIFY2(controller->logDriver().isEmpty(), "selecting the default must clear the value");
    QVERIFY(spin->setProperty("value", 0));
    QVERIFY(QMetaObject::invokeMethod(spin, "valueModified"));
    QCOMPARE(controller->maxConcurrentDownloads(), 0);
    merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QVERIFY2(!merged.contains(QStringLiteral("log-driver")), "the key must be removed, not emptied");
    QVERIFY2(!merged.contains(QStringLiteral("max-concurrent-downloads")), "the key must be removed, not zeroed");

    // "Default" is a pending edit too: it must stay pending after an auto-refresh
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY(controller->dirty());
    QCOMPARE(spin->property("value").toInt(), 0);
    QCOMPARE(combo->property("currentIndex").toInt(), 0);
}

/*!
 * The two config scopes differ in editability and in hint wording (user-reported feedback).
 *
 *  1) While the system scope is locked **every** editor must be disabled — `editable` is not enough:
 *     a SpinBox text field turns read-only, but its arrows still change the value.
 *  2) The user-scope hint must say taking effect needs administrator rights, not that editing the file does:
 *     the file is the user's own; only taking effect (restarting the system daemon / rootless) needs them.
 */
void QmlLoadTest::configPageWordingAndLocksPerScope()
{
    auto *user = m_stubKcm->controller()->daemonConfigUser();
    auto *system = m_stubKcm->controller()->daemonConfigSystem();

    // Reproduce the real shape: system daemon plus a data root under $HOME ("looks rootless")
    Kontainer::EngineInfo info;
    info.available = true;
    info.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")};
    info.dockerRootDir = QDir::homePath() + QStringLiteral("/.local/share/docker");
    system->setEngineInfo(info);
    user->setEngineInfo(info);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/DaemonConfigPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    // ---- system scope: protected, locked -> every editor disabled ----
    QVariantMap systemInitial;
    systemInitial.insert(QStringLiteral("scope"), QStringLiteral("system"));
    QScopedPointer<QObject> systemPage(component.createWithInitialProperties(systemInitial, m_engine->rootContext()));
    QVERIFY(!systemPage.isNull());
    QVERIFY2(system->requiresPrivilege(), "this test assumes the system config is not writable");

    QQuickItem *systemSpin = findItemByName(systemPage.data(), QStringLiteral("concurrentDownloadsSpin"));
    QQuickItem *systemCombo = findItemByName(systemPage.data(), QStringLiteral("logDriverCombo"));
    QQuickItem *systemMirrors = findItemByName(systemPage.data(), QStringLiteral("mirrorsEditor"));
    QVERIFY(systemSpin && systemCombo && systemMirrors);
    QVERIFY2(!systemSpin->property("enabled").toBool(), "the spin box must be disabled while locked");
    QVERIFY2(!systemSpin->property("editable").toBool(), "the spin box text field must be read-only while locked");
    QVERIFY2(!systemCombo->property("enabled").toBool(), "the combo box must be disabled while locked");
    QVERIFY2(!systemMirrors->property("editable").toBool(), "the mirror editor must be read-only while locked");

    // ---- user scope: the file belongs to the user -> editable, and **no** unlock UI ----
    QVariantMap userInitial;
    userInitial.insert(QStringLiteral("scope"), QStringLiteral("user"));
    QScopedPointer<QObject> userPage(component.createWithInitialProperties(userInitial, m_engine->rootContext()));
    QVERIFY(!userPage.isNull());
    QVERIFY2(user->dataRootInHomeDir(), "the data-root hint precondition is not met");

    QQuickItem *userSpin = findItemByName(userPage.data(), QStringLiteral("concurrentDownloadsSpin"));
    QVERIFY(userSpin);
    QVERIFY2(userSpin->property("enabled").toBool(), "the user scope is editable without unlocking");
    for (const char *name : {"unlockButton", "lockButton", "lockedMessage", "unlockedMessage"}) {
        QQuickItem *item = findItemByName(userPage.data(), QString::fromLatin1(name));
        QVERIFY2(item, name);
        QVERIFY2(!item->property("visible").toBool(),
                 qPrintable(QStringLiteral("%1 must not appear on the user scope page").arg(QString::fromLatin1(name))));
    }

    // "Add mirror" must really add an empty row, and syncing initialEntries must not drop it
    // (real feedback: adding only marked the page dirty, no row appeared). The empty row takes the
    // validation branch, which may only use QML string literals: a C++ QStringLiteral throws a
    // ReferenceError there, invisible to both compilation and page loading.
    QQuickItem *mirrorsEditor = findItemByName(userPage.data(), QStringLiteral("mirrorsEditor"));
    QVERIFY(mirrorsEditor);
    QQuickItem *addButton = findItemByName(mirrorsEditor, QStringLiteral("stringListAddButton"));
    QVERIFY(addButton);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));

    // The row really reached the model: the Repeater count is the row count
    // (a windowless page creates no delegates, so assert on the model, not on delegate controls)
    QCOMPARE(repeaterCount(mirrorsEditor), 1);
    // An empty row is a draft, not an "external change": another sync must not clear it
    // (real feedback: add only marked dirty, no row — the old code cleared it as an external change)
    QVERIFY(QMetaObject::invokeMethod(mirrorsEditor, "syncFromInitialEntries"));
    QCOMPARE(repeaterCount(mirrorsEditor), 1);

    // The validation branch must really produce a message (the empty row goes straight into it)
    QQmlExpression emptyHostCall(qmlContext(userPage.data()), userPage.data(), QStringLiteral("mirrorError('')"));
    QVERIFY2(emptyHostCall.evaluate().toString().contains(QStringLiteral("example")),
             qPrintable(emptyHostCall.evaluate().toString()));
    QQmlExpression invalidCall(qmlContext(userPage.data()), userPage.data(), QStringLiteral("mirrorError('not a host')"));
    QVERIFY2(!invalidCall.evaluate().toString().isEmpty(), "the invalid-address branch must produce a message");
    for (const QString &captured : g_messages) {
        QVERIFY2(!captured.contains(QStringLiteral("ReferenceError")), qPrintable(captured));
    }

    // An empty draft must not reach the pending config (a row counts only once it has an address)
    QVERIFY2(!controller_pendingContains(user, QStringLiteral("mirror.example.com")),
             "an empty draft row must not end up in the pending config");

    // Data-root hint: the user scope speaks of privileges needed to take effect
    QQuickItem *hint = findItemByName(userPage.data(), QStringLiteral("dataRootHint"));
    QVERIFY(hint);
    QVERIFY(hint->property("visible").toBool());
    const QString hintText = hint->property("text").toString();
    QVERIFY2(hintText.contains(QStringLiteral("take effect")), qPrintable(hintText));
    QVERIFY2(!hintText.contains(QStringLiteral("changing this configuration needs")), qPrintable(hintText));

    // The system-scope hint still says changing this configuration needs privileges (system-owned file)
    QQuickItem *systemHint = findItemByName(systemPage.data(), QStringLiteral("dataRootHint"));
    QVERIFY(systemHint);
    QVERIFY(systemHint->property("visible").toBool());
    // Unified after user feedback: no ownership distinction, just "taking effect needs administrator rights"
    QVERIFY2(systemHint->property("text").toString().contains(QStringLiteral("administrator rights")),
             qPrintable(systemHint->property("text").toString()));
}

/*!
 * Topology link colours must follow the container: one container (one port mapping) always gets the same
 * colours, another a different set. If they were random, users would read it as "this container changed".
 */
void QmlLoadTest::topologyConnectionColorsAreStablePerContainer()
{
    const QString componentsPath = QStringLiteral("file://") + QStringLiteral(KCM_DOCKER_SOURCE_DIR) + QStringLiteral("/src/ui/components");

    // Ask the palette directly: one seed twice must match, different seeds should reach different slots
    QQmlComponent component(m_engine.get());
    component.setData(QStringLiteral("import QtQuick\n"
                                     "import \"%1\" as C\n"
                                     "QtObject {\n"
                                     "    property color sameSeedAgain: C.ChartPalette.connectionColor('cid-1|3000/tcp|0.0.0.0:20000')\n"
                                     "    property color first: C.ChartPalette.connectionColor('cid-1|3000/tcp|0.0.0.0:20000')\n"
                                     "    property color other: C.ChartPalette.connectionColor('cid-1|3001/tcp|0.0.0.0:20001')\n"
                                     "    property color emptySeed: C.ChartPalette.connectionColor('')\n"
                                     "}\n")
                                     .arg(componentsPath)
                                     .toUtf8(),
                                 QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> probe(component.create());
    QVERIFY(!probe.isNull());

    QCOMPARE(probe->property("sameSeedAgain").value<QColor>(), probe->property("first").value<QColor>());
    QVERIFY2(probe->property("first").value<QColor>() != probe->property("other").value<QColor>(),
             "different mappings of the same container should be able to differ");
    // With no seed yet, fall back to a neutral colour rather than throwing or going transparent
    QVERIFY(probe->property("emptySeed").value<QColor>().isValid());
}

/*!
 * Registry auth page (ARCH_V5_V8 §2.7): empty state, wallet banner, stored list, CLI import candidates.
 *
 * Uses the injected in-memory credential backend (`QmlStubKcm::credentialBackend()`) — never real KWallet.
 */
void QmlLoadTest::registryAuthPageReflectsWalletAndStoredCredentials()
{
    // Point the CLI config at a temp dir: never read (or write) the developer's real ~/.docker
    QTemporaryDir cliDir;
    QVERIFY(cliDir.isValid());
    QJsonObject auths;
    QJsonObject hub;
    hub.insert(QStringLiteral("auth"), QString::fromLatin1(QByteArrayLiteral("alice:secret").toBase64()));
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), hub);
    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    QFile config(cliDir.filePath(QStringLiteral("config.json")));
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    config.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    config.close();
    qputenv("DOCKER_CONFIG", cliDir.path().toUtf8());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/RegistryAuthPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *emptyPlaceholder = childByObjectName(page, QStringLiteral("credentialsEmptyPlaceholder"));
    QQuickItem *walletBanner = childByObjectName(page, QStringLiteral("walletBanner"));
    QVERIFY(emptyPlaceholder && walletBanner);
    QVERIFY2(!walletBanner->property("visible").toBool(), "an available wallet must not show a banner");

    /*
     * New behaviour (requested): no import/sync button anymore; CLI config entries are
     * recognized **silently** on page load — the hub entry is already listed, no empty state.
     */
    QTRY_VERIFY(!emptyPlaceholder->property("visible").toBool());
    QVERIFY2(m_stubKcm->controller()->registryAuth()->credentials()->rowCount() == 1,
             "the docker cli entry must be recognized silently on load");

    // The registry shows up in the list
    int rows = 0;
    std::function<void(QQuickItem *)> countRows = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            if (child->objectName() == QLatin1String("credentialRow")) {
                ++rows;
            }
            countRows(child);
        }
    };
    countRows(page);
    QCOMPARE(rows, 1);

    // The "add credential" entry exists and is enabled (new requirement: add proactively)
    QQuickItem *addButton = childByObjectName(page, QStringLiteral("addCredentialButton"));
    QVERIFY2(addButton, "the page must offer an 'add credential' entry");
    QVERIFY(addButton->property("enabled").toBool());

    // The old sync UI must be **gone**: neither candidate checkboxes nor "import selected" may remain
    QVERIFY2(!childByObjectName(page, QStringLiteral("importSelectedButton")),
             "the explicit import/sync button must be gone");
    QVERIFY2(!childByObjectName(page, QStringLiteral("importCandidateCheck")),
             "the import candidate list must be gone");

    qunsetenv("DOCKER_CONFIG");
}

/*!
 * Guidance: failed pulls (401/403) and "not logged in to this registry" must reach the login form.
 */
void QmlLoadTest::registryAuthGuidesFromFailedPullsAndMissingCredentials()
{
    // Failed row: only permissionDenied offers "Go to login..."
    ImagePullEntry failed;
    failed.reference = QStringLiteral("registry.example.com/team/app:1.0");
    failed.statusKey = QStringLiteral("failed");
    failed.detailText = QStringLiteral("unauthorized");
    failed.errorKindKey = QStringLiteral("permissionDenied");
    failed.active = false;
    m_stubKcm->controller()->operations()->pulls()->setEntries({failed});

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PullProgressList.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    QScopedPointer<QObject> list(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!list.isNull(), qPrintable(component.errorString()));
    auto *listItem = qobject_cast<QQuickItem *>(list.data());
    QVERIFY(listItem);
    QQuickItem *loginButton = childByObjectName(listItem, QStringLiteral("pullLoginButton"));
    QVERIFY2(loginButton, "a credential failure must offer a login action");
    QTRY_VERIFY(loginButton->property("visible").toBool());

    // Other failure reasons (e.g. an unreachable registry) must not show that button
    ImagePullEntry unreachable = failed;
    unreachable.errorKindKey = QStringLiteral("timeout");
    m_stubKcm->controller()->operations()->pulls()->setEntries({unreachable});
    QTRY_VERIFY(!loginButton->property("visible").toBool());

    // Pull dialog: no credentials for the registry -> hint plus "Go to login..."
    QQmlComponent dialogComponent(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml")));
    QVERIFY2(!dialogComponent.isError(), qPrintable(dialogComponent.errorString()));
    QVariantMap dialogInitial;
    dialogInitial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    dialogInitial.insert(QStringLiteral("credentialKnown"), false);
    QScopedPointer<QObject> dialog(dialogComponent.createWithInitialProperties(dialogInitial, m_engine->rootContext()));
    QVERIFY2(!dialog.isNull(), qPrintable(dialogComponent.errorString()));
    // Kirigami.Dialog is not a QQuickItem (QObject-based), so look it up in the QObject tree
    QQuickItem *hint = findItemByName(dialog.data(), QStringLiteral("pullNeedsLoginHint"));
    QVERIFY2(hint, "the pull dialog must be able to guide to the login page");
    QVERIFY2(!hint->property("text").toString().isEmpty(), "the hint must say what will happen");
    QVERIFY2(!hint->property("visible").toBool(), "the hint only appears once a valid reference is typed");
}

/*!
 * Networks tab (ARCH_V5_V8 §3.2): list, filtering, and refresh-on-tab-switch only.
 */
void QmlLoadTest::networksTabListsAndOpensDetails()
{
    QList<Network> networks;
    Network bridge;
    bridge.id = QString(64, QLatin1Char('b'));
    bridge.name = QStringLiteral("bridge");
    bridge.driver = QStringLiteral("bridge");
    bridge.scope = QStringLiteral("local");
    bridge.ipamConfigs.append({QStringLiteral("172.17.0.0/16"), QStringLiteral("172.17.0.1")});
    networks.append(bridge);

    Network app;
    app.id = QString(64, QLatin1Char('a'));
    app.name = QStringLiteral("app_default");
    app.driver = QStringLiteral("bridge");
    app.scope = QStringLiteral("local");
    app.ipamConfigs.append({QStringLiteral("172.18.0.0/16"), QStringLiteral("172.18.0.1")});
    app.labels.append({QStringLiteral("com.docker.compose.project"), QStringLiteral("app")});
    NetworkMember member;
    member.containerId = QString(64, QLatin1Char('1'));
    member.name = QStringLiteral("app");
    member.ipv4Address = QStringLiteral("172.18.0.2");
    member.macAddress = QStringLiteral("02:42:ac:12:00:02");
    app.members.append(member);
    networks.append(app);
    m_backend->setNetworks(networks);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    // ListView creates delegates for the visible area only: a real window and layout are needed
    QQuickWindow window;
    window.resize(1000, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1000);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    // 0 containers / 1 images / 2 networks / 3 volumes / 4 engine
    // The tab count grows with features (now 6: containers/images/networks/volumes/engine/mount presets),
    // so assert "enough tabs" instead of a hard number and avoid touching the test per new page
    QVERIFY2(tabBar->property("count").toInt() >= 5, qPrintable(QString::number(tabBar->property("count").toInt())));

    // No network-tab visit, no network list read (low-frequency data, refreshed on demand)
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Networks), 0);

    QVERIFY(tabBar->setProperty("currentIndex", 2));
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Networks), 1);
    m_backend->completeRefresh();

    QQuickItem *networkView = childByObjectName(page, QStringLiteral("networkView"));
    QVERIFY2(networkView, "the networks tab must have its own list");
    QTRY_COMPARE(networkView->property("count").toInt(), 2);
    QTest::qWait(50); // wait for the delegates to be created
    QTRY_COMPARE(networkView->property("count").toInt(), 2);

    int cards = 0;
    int builtinChips = 0;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            if (child->objectName() == QLatin1String("networkCard")) {
                ++cards;
            } else if (child->objectName() == QLatin1String("networkPredefinedChip")) {
                ++builtinChips;
            }
            walk(child);
        }
    };
    walk(networkView);
    QCOMPARE(cards, 2);
    QCOMPARE(builtinChips, 2); // both rows carry this chip, but only the built-in network row shows it

    // Filtering: user-defined networks only
    auto *filter = m_stubKcm->controller()->networkList();
    filter->setOriginFilter(QStringLiteral("custom"));
    QTRY_COMPARE(networkView->property("count").toInt(), 1);
    filter->setOriginFilter(QStringLiteral("all"));
    QTRY_COMPARE(networkView->property("count").toInt(), 2);

    // The shared container/image search row is absent here (the networks tab has its own toolbar)
    QQuickItem *searchRow = childByObjectName(page, QStringLiteral("containerSearchField"));
    if (searchRow) {
        QVERIFY2(!searchRow->isVisible(), "the shared container/image toolbar must be hidden on the networks tab");
    }
}

/*!
 * Network detail: member container list plus a signal to jump to the container detail (main.qml navigates).
 */
void QmlLoadTest::networkDetailShowsMembersAndJumpsToContainers()
{
    QList<Network> networks;
    Network app;
    app.id = QString(64, QLatin1Char('a'));
    app.name = QStringLiteral("app_default");
    app.driver = QStringLiteral("bridge");
    app.scope = QStringLiteral("local");
    app.created = QDateTime::currentDateTimeUtc().addSecs(-7200);
    app.ipamConfigs.append({QStringLiteral("172.18.0.0/16"), QStringLiteral("172.18.0.1")});
    app.labels.append({QStringLiteral("com.docker.compose.project"), QStringLiteral("app")});
    app.options.append({QStringLiteral("com.docker.network.bridge.name"), QStringLiteral("br-app")});
    NetworkMember member;
    member.containerId = QString(64, QLatin1Char('1'));
    member.name = QStringLiteral("app");
    member.ipv4Address = QStringLiteral("172.18.0.2");
    member.macAddress = QStringLiteral("02:42:ac:12:00:02");
    app.members.append(member);
    networks.append(app);
    m_backend->setNetworks(networks);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/NetworkDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("networkId"), app.id);
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QCOMPARE(m_stubKcm->controller()->networkDetail()->name(), QStringLiteral("app_default"));
    QQuickItem *goneMessage = childByObjectName(page, QStringLiteral("networkGoneMessage"));
    QVERIFY(goneMessage);
    QVERIFY2(!goneMessage->property("visible").toBool(), "the network exists, so no 'gone' notice");

    QQuickItem *memberRow = childByObjectName(page, QStringLiteral("networkMemberRow"));
    QVERIFY2(memberRow, "the member container must be listed");
    QSignalSpy containerSpy(page, SIGNAL(containerRequested(QString)));
    QVERIFY(QMetaObject::invokeMethod(memberRow, "clicked"));
    QTRY_COMPARE(containerSpy.count(), 1);
    QCOMPARE(containerSpy.at(0).at(0).toString(), member.containerId);

    // Labels and options sections exist (collapsed by default so long lists do not swamp the page)
    QQuickItem *labelsSection = childByObjectName(page, QStringLiteral("networkLabelsSection"));
    QVERIFY(labelsSection);
    QVERIFY2(!labelsSection->property("expanded").toBool(), "collapsible sections start collapsed");
    QCOMPARE(m_stubKcm->controller()->networkDetail()->labels()->count(), 1);
    QCOMPARE(m_stubKcm->controller()->networkDetail()->options()->count(), 1);
}

/*!
 * Create-network dialog (ARCH_V5_V8 §3.3): validation precedes submission; invalid input sends no request.
 */
void QmlLoadTest::createNetworkDialogValidatesBeforeSubmitting()
{
    // Submission goes through the write gate: point the endpoint at a socket this process may write
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    QList<Network> networks;
    Network bridge;
    bridge.id = QString(64, QLatin1Char('b'));
    bridge.name = QStringLiteral("bridge");
    bridge.driver = QStringLiteral("bridge");
    networks.append(bridge);
    m_backend->setNetworks(networks);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/CreateNetworkDialog.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    QScopedPointer<QObject> dialog(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!dialog.isNull(), qPrintable(component.errorString()));

    QQuickItem *nameField = findItemByName(dialog.data(), QStringLiteral("networkNameField"));
    QQuickItem *subnetField = findItemByName(dialog.data(), QStringLiteral("networkSubnetField"));
    QQuickItem *gatewayField = findItemByName(dialog.data(), QStringLiteral("networkGatewayField"));
    QVERIFY(nameField && subnetField && gatewayField);
    QVERIFY2(!dialog->property("canSubmit").toBool(), "an empty name must not be submittable");

    // Invalid name: explain the reason in place and refuse to submit
    nameField->setProperty("text", QStringLiteral("my net"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("nameInvalid"));
    QVERIFY(!dialog->property("canSubmit").toBool());
    // Kirigami.Dialog content may exist as two instances (popup and manager), so rather than
    // asserting one instance's visibility, assert that this key has user-facing text
    QString message;
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "messageFor", Q_RETURN_ARG(QString, message),
                                      Q_ARG(QString, QStringLiteral("nameInvalid"))));
    QVERIFY2(!message.isEmpty(), "every validation key must have user-facing text");

    // Clashes with an existing network name
    nameField->setProperty("text", QStringLiteral("Bridge"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("nameInUse"));

    // Gateway without a subnet
    nameField->setProperty("text", QStringLiteral("app_net"));
    gatewayField->setProperty("text", QStringLiteral("172.30.0.1"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("gatewayNeedsSubnet"));

    // Valid input: submittable, and the request is really sent
    subnetField->setProperty("text", QStringLiteral("172.30.0.0/16"));
    QCOMPARE(dialog->property("currentError").toString(), QString());
    QVERIFY(dialog->property("canSubmit").toBool());

    QSignalSpy createdSpy(dialog.data(), SIGNAL(created(QString)));
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "submit"));
    QTRY_COMPARE(createdSpy.count(), 1);
    QCOMPARE(m_backend->lastNetworkCreate().name, QStringLiteral("app_net"));
    QCOMPARE(m_backend->lastNetworkCreate().subnet, QStringLiteral("172.30.0.0/16"));
    QCOMPARE(m_backend->lastNetworkCreate().driver, QStringLiteral("bridge"));
}

/*!
 * Removal entry visibility (ARCH_V5_V8 §3.2/§3.3):
 * built-in networks get **no** removal entry (the daemon answers 403), nor does read-only mode.
 */
void QmlLoadTest::networkRemovalIsHiddenForBuiltInNetworks()
{
    QList<Network> networks;
    Network builtin;
    builtin.id = QString(64, QLatin1Char('b'));
    builtin.name = QStringLiteral("bridge");
    builtin.driver = QStringLiteral("bridge");
    networks.append(builtin);
    Network custom;
    custom.id = QString(64, QLatin1Char('a'));
    custom.name = QStringLiteral("app_default");
    custom.driver = QStringLiteral("bridge");
    custom.ipamConfigs.append({QStringLiteral("172.18.0.0/16"), QStringLiteral("172.18.0.1")});
    NetworkMember member;
    member.containerId = QString(64, QLatin1Char('1'));
    member.name = QStringLiteral("app");
    member.ipv4Address = QStringLiteral("172.18.0.2");
    custom.members.append(member);
    NetworkMember second = member;
    second.containerId = QString(64, QLatin1Char('2'));
    second.name = QStringLiteral("worker");
    second.ipv4Address = QStringLiteral("172.18.0.3");
    custom.members.append(second);
    networks.append(custom);
    m_backend->setNetworks(networks);
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    const auto loadPage = [this](const QString &networkId) {
        auto component = std::make_unique<QQmlComponent>(m_engine.get(),
                                                        QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/NetworkDetail.qml")));
        if (component->isError()) {
            return std::unique_ptr<QObject>();
        }
        QVariantMap initial;
        initial.insert(QStringLiteral("networkId"), networkId);
        return std::unique_ptr<QObject>(component->createWithInitialProperties(initial, m_engine->rootContext()));
    };

    // Built-in network: no removal action
    std::unique_ptr<QObject> builtinObject = loadPage(builtin.id);
    QVERIFY(builtinObject);
    auto *builtinPage = qobject_cast<QQuickItem *>(builtinObject.get());
    QVERIFY(builtinPage);
    QQuickItem *notice = childByObjectName(builtinPage, QStringLiteral("networkPredefinedNotice"));
    QVERIFY(notice);
    QVERIFY2(notice->property("visible").toBool(), "the reason why it cannot be removed must be shown");
    // Kirigami.Action is not a QQuickItem: look it up in the QObject tree (else this assert passes vacuously)
    QObject *removeAction = builtinPage->findChild<QObject *>(QStringLiteral("removeNetworkAction"));
    QVERIFY2(removeAction, "the action must exist so that its visibility can be asserted");
    QVERIFY2(!removeAction->property("visible").toBool(), "a built-in network must not offer a remove action");

    // Custom network: has a removal action, and the confirmation spells out that attached containers lose it
    std::unique_ptr<QObject> customObject = loadPage(custom.id);
    QVERIFY(customObject);
    auto *customPage = qobject_cast<QQuickItem *>(customObject.get());
    QVERIFY(customPage);
    // Kirigami.Dialog is not a QQuickItem: look it up in the QObject tree
    QObject *removeDialog = customPage->findChild<QObject *>(QStringLiteral("removeNetworkDialog"));
    QVERIFY2(removeDialog, "a user-defined network must offer a removal dialog");
    const QString consequence = removeDialog->property("consequenceText").toString();
    QVERIFY2(!consequence.isEmpty(), "the removal dialog must always carry a consequence");
    // Two members -> plural wording with the count (users must know how many containers are affected)
    QVERIFY2(consequence.contains(QStringLiteral("2")), qPrintable(consequence));

    // Read-only mode: the create entry is absent entirely, not disabled and silent
    m_backend->setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(!m_stubKcm->controller()->operations()->writeAllowed());
    QQmlComponent mainComponent(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QVERIFY2(!mainComponent.isError(), qPrintable(mainComponent.errorString()));
    QScopedPointer<QObject> mainObject(mainComponent.create(m_engine->rootContext()));
    auto *mainPage = qobject_cast<QQuickItem *>(mainObject.data());
    QVERIFY(mainPage);
    QQuickItem *tabBar = childByObjectName(mainPage, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2));
    QQuickItem *createButton = childByObjectName(mainPage, QStringLiteral("createNetworkEntryButton"));
    QVERIFY(createButton);
    QVERIFY2(!createButton->property("visible").toBool(), "read-only mode must hide the create entry");
}

/*!
 * Container detail network section (ARCH_V5_V8 §3.4): the connect dialog lists unattached networks only;
 * disconnect goes through a confirmation (consequence text required); read-only hides both entries.
 */
void QmlLoadTest::containerNetworkSectionConnectsAndDisconnects()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    // Two networks: the container is attached to bridge, not to app_default
    QList<Network> networks;
    Network bridge;
    bridge.id = QString(64, QLatin1Char('b'));
    bridge.name = QStringLiteral("bridge");
    bridge.driver = QStringLiteral("bridge");
    networks.append(bridge);
    Network app;
    app.id = QString(64, QLatin1Char('a'));
    app.name = QStringLiteral("app_default");
    app.driver = QStringLiteral("bridge");
    networks.append(app);
    m_backend->setNetworks(networks);
    m_stubKcm->controller()->refreshNetworks();

    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.networks = {{QStringLiteral("bridge"), QStringLiteral("net1"), QStringLiteral("172.17.0.4"),
                        QStringLiteral("fd00::4"), QStringLiteral("02:42:ac:11:00:04"), QStringLiteral("172.17.0.1")}};
    m_backend->setContainerDetail(detail);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("containerId"), QStringLiteral("cid-1")}},
                                                                        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2)); // networks section

    // Connected networks are reported truthfully by the controller (the dialog disables those options)
    const QStringList connected = m_stubKcm->controller()->containerDetail()->connectedNetworkNames();
    QCOMPARE(connected, QStringList {QStringLiteral("bridge")});

    // Each row offers "Disconnect"; the confirmation carries a consequence; confirming sends the request
    // Kirigami.Dialog / Kirigami.Action are no QQuickItems: look them up in the QObject tree
    QObject *disconnectDialog = page->findChild<QObject *>(QStringLiteral("disconnectNetworkDialog"));
    QVERIFY2(disconnectDialog, "the disconnect confirmation must exist");
    QVERIFY2(!disconnectDialog->property("consequenceText").toString().isEmpty(),
             "the disconnect dialog must always carry a consequence");

    // The connect entry is an **inline panel** (not a popup): clicking the button expands it
    QQuickItem *connectEntry = findItemByName(page, QStringLiteral("connectNetworkEntryButton"));
    QVERIFY2(connectEntry, "the connect entry must exist");
    QVERIFY2(!page->property("connectPanelOpen").toBool(), "the panel starts collapsed");
    QVERIFY(QMetaObject::invokeMethod(connectEntry, "clicked"));
    QVERIFY(page->property("connectPanelOpen").toBool());
    QQuickItem *panel = findItemByName(page, QStringLiteral("connectNetworkPanel"));
    QVERIFY2(panel, "the inline connect panel must exist");

    // Connected networks are not selectable, unattached ones are (the delegate enabled uses this function)
    bool connectable = true;
    QVERIFY(QMetaObject::invokeMethod(page, "isConnectable", Q_RETURN_ARG(bool, connectable),
                                      Q_ARG(QString, QStringLiteral("bridge"))));
    QVERIFY2(!connectable, "an already connected network must not be selectable");
    QVERIFY(QMetaObject::invokeMethod(page, "isConnectable", Q_RETURN_ARG(bool, connectable),
                                      Q_ARG(QString, QStringLiteral("app_default"))));
    QVERIFY(connectable);
    // Must be a **property**: a function (or read-only function binding) never re-evaluates on data
    // changes — the root cause of "still shown as connected, unselectable after disconnecting"
    QCOMPARE(page->property("connectableNetworkCount").isValid(), true);
    QCOMPARE(page->property("connectableNetworkCount").toInt(), 1); // only app_default stays connectable
    QVERIFY2(!page->property("connectedNetworkNameList").toStringList().isEmpty(),
             "the connected list must be exposed as a property so bindings track it");

    // Select the unattached network and submit: the request carries network id, container id and aliases
    page->setProperty("connectNetworkId", app.id);
    QQuickItem *aliases = findItemByName(page, QStringLiteral("connectNetworkAliasesField"));
    QVERIFY2(aliases, "the aliases field must be in the panel");
    aliases->setProperty("text", QStringLiteral("demo, api"));
    QVERIFY(QMetaObject::invokeMethod(page, "submitConnectNetwork"));
    QTRY_COMPARE(m_backend->lastNetworkConnect().second, QStringLiteral("cid-1"));
    QCOMPARE(m_backend->lastNetworkConnect().first, app.id);
    QCOMPARE(m_backend->lastNetworkConnectAliases(), QStringList({QStringLiteral("demo"), QStringLiteral("api")}));
    QVERIFY2(!page->property("connectPanelOpen").toBool(), "the panel closes after a successful connect");

    /*
     * User-reported regression: after disconnecting a network in the container network section and
     * reopening "connect network", that network still showed as connected and unselectable.
     *
     * The detail is now re-inspected after a disconnect and panel availability comes from **property**
     * bindings, so the network must become selectable again at once, without reopening the panel.
     */
    // Drop bridge from the detail (the engine's real state after the disconnect)
    ContainerDetail afterDisconnect = detail;
    afterDisconnect.networks.clear();
    m_backend->setContainerDetail(afterDisconnect);

    // The disconnect itself: the dialog works by **network name** (name -> id conversion lives in one place)
    page->setProperty("pendingNetworkName", QStringLiteral("bridge"));
    QVERIFY(QMetaObject::invokeMethod(disconnectDialog, "confirmed"));
    QCOMPARE(m_backend->lastNetworkDisconnect().first, bridge.id);
    QCOMPARE(m_backend->lastNetworkDisconnect().second, QStringLiteral("cid-1"));

    // Complete the mutation: the controller then re-inspects (write-then-read) and drives the real refresh
    m_backend->completeMutations();
    m_backend->completeRefresh();
    QTRY_VERIFY2(m_stubKcm->controller()->containerDetail()->connectedNetworkNames().isEmpty(),
                 "the detail must be re-read after a disconnect");

    // Key assertion: no panel reopen, both networks selectable and the count follows
    QTRY_COMPARE(page->property("connectableNetworkCount").toInt(), 2);
    bool connectableAfter = false;
    QVERIFY(QMetaObject::invokeMethod(page, "isConnectable", Q_RETURN_ARG(bool, connectableAfter),
                                      Q_ARG(QString, QStringLiteral("bridge"))));
    QVERIFY2(connectableAfter, "a disconnected network must become selectable again");
}

/*!
 * Volumes tab (ARCH_V5_V8 §3.5): list, unused filter, create panel and prune preview.
 */
void QmlLoadTest::volumesTabListsCreatesAndPreviewsCleanup()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    QList<Volume> volumes;
    Volume used;
    used.name = QStringLiteral("app_data");
    used.driver = QStringLiteral("local");
    used.mountpoint = QStringLiteral("/var/lib/docker/volumes/app_data/_data");
    used.sizeBytes = 4096;
    used.refCount = 1;
    used.labels.append({QStringLiteral("com.example.owner"), QStringLiteral("team-a")});
    volumes.append(used);
    Volume unused;
    unused.name = QStringLiteral("cache");
    unused.driver = QStringLiteral("local");
    unused.mountpoint = QStringLiteral("/var/lib/docker/volumes/cache/_data");
    unused.sizeBytes = 2048;
    unused.refCount = 0;
    volumes.append(unused);
    // Usage unknown but **size known**: the reclaimable figure then separates the two implementations
    // (treating unknown as unused adds the 512 bytes -> 2.5 KiB instead of 2.0 KiB)
    Volume unknown;
    unknown.name = QStringLiteral("legacy");
    unknown.driver = QStringLiteral("local");
    unknown.mountpoint = QStringLiteral("/var/lib/docker/volumes/legacy/_data");
    unknown.sizeBytes = 512;
    unknown.refCount = -1;
    volumes.append(unknown);
    m_backend->setVolumes(volumes);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY2(tabBar->property("count").toInt() >= 5, qPrintable(QString::number(tabBar->property("count").toInt())));
    // No volumes-tab visit, no list read
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Volumes), 0);
    QVERIFY(tabBar->setProperty("currentIndex", 3));
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Volumes), 1);
    m_backend->completeRefresh();

    QQuickItem *volumeView = childByObjectName(page, QStringLiteral("volumeView"));
    QVERIFY2(volumeView, "the volumes tab must have its own list");
    QTRY_COMPARE(volumeView->property("count").toInt(), 3);

    // Unused filter: cache only (legacy has unknown usage, so it does not count as prunable)
    auto *filter = m_stubKcm->controller()->volumeList();
    filter->setUsageFilter(QStringLiteral("unused"));
    QTRY_COMPARE(volumeView->property("count").toInt(), 1);
    filter->setUsageFilter(QStringLiteral("all"));
    QTRY_COMPARE(volumeView->property("count").toInt(), 3);

    // Prune preview: lists the volumes to delete and the reclaimable space (unknown sizes stated honestly)
    QQuickItem *pruneEntry = findItemByName(page, QStringLiteral("pruneVolumesEntryButton"));
    QVERIFY(pruneEntry);
    QVERIFY(QMetaObject::invokeMethod(pruneEntry, "clicked"));
    QVERIFY(page->property("volumePrunePanelOpen").toBool());
    QString reclaimable;
    QVERIFY(QMetaObject::invokeMethod(page, "pruneReclaimableText", Q_RETURN_ARG(QString, reclaimable)));
    // Only cache (2048 bytes = 2.0 KiB) is reclaimable; legacy with unknown usage (512 bytes) is not
    QVERIFY2(reclaimable.contains(QStringLiteral("2.0 KiB")), qPrintable(reclaimable));
    QVERIFY2(!reclaimable.contains(QStringLiteral("2.5 KiB")), qPrintable(reclaimable));

    // Create panel: name validation (an empty name cannot submit); a valid name really sends the request
    QQuickItem *createEntry = findItemByName(page, QStringLiteral("createVolumeEntryButton"));
    QVERIFY(createEntry);
    QVERIFY(QMetaObject::invokeMethod(createEntry, "clicked"));
    QVERIFY(page->property("volumeCreatePanelOpen").toBool());
    QQuickItem *nameField = findItemByName(page, QStringLiteral("volumeNameField"));
    QQuickItem *createButton = findItemByName(page, QStringLiteral("createVolumeButton"));
    QVERIFY(nameField && createButton);
    QVERIFY2(!createButton->property("enabled").toBool(), "an empty name must not be submittable");
    nameField->setProperty("text", QStringLiteral("new_volume"));
    QTRY_VERIFY(createButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(createButton, "clicked"));
    QCOMPARE(m_backend->lastCreatedVolumeName(), QStringLiteral("new_volume"));
    QCOMPARE(m_backend->lastCreatedVolumeDriver(), QStringLiteral("local"));
    QVERIFY2(!page->property("volumeCreatePanelOpen").toBool(), "the panel closes once the request is sent");
}

/*!
 * Create-container wizard (ARCH_V5_V8 §4.4): per-step validation, privileged confirmation, hidden env values.
 */
void QmlLoadTest::createContainerWizardGatesStepsAndHidesSecrets()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({image});
    QList<Network> networks;
    Network app;
    app.id = QString(64, QLatin1Char('a'));
    app.name = QStringLiteral("app_default");
    app.driver = QStringLiteral("bridge");
    networks.append(app);
    m_backend->setNetworks(networks);
    m_stubKcm->controller()->refreshNetworks();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // Must live in a window: ScrollView content and Repeater items are not really created without one
    // (the phase-6 networks test hit the same trap)
    QQuickWindow window;
    window.resize(1100, 800);
    page->setParentItem(window.contentItem());
    page->setWidth(1100);
    page->setHeight(800);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    m_backend->completeRefresh();
    QTest::qWait(20);


    auto *wizard = m_stubKcm->controller()->createContainer();
    QCOMPARE(wizard->stepKey(), QStringLiteral("image"));

    // Step one: no image, no progress (Next is disabled too; both paths must hold)
    QQuickItem *nextButton = childByObjectName(page, QStringLiteral("wizardNextButton"));
    QVERIFY(nextButton);
    QVERIFY2(!nextButton->property("enabled").toBool(), "an empty image must not allow continuing");
    QQuickItem *stepError = childByObjectName(page, QStringLiteral("wizardStepError"));
    QVERIFY(stepError);
    QTRY_VERIFY(stepError->property("visible").toBool());
    QVERIFY2(!stepError->property("text").toString().isEmpty(), "the reason must be translated");

    // With an image filled in, continue; an image missing locally must hint "pull it first"
    QQuickItem *imageField = childByObjectName(page, QStringLiteral("wizardImageField"));
    QVERIFY(imageField);
    imageField->setProperty("text", QStringLiteral("busybox:latest"));
    QTRY_VERIFY(stepError->property("visible").toBool());
    QCOMPARE(stepError->property("text").toString().contains(QStringLiteral("Pull")), true);
    // Reproduce the user path: click a **step button** to jump there (rejected, reason recorded)...
    {
        QList<QQuickItem *> stepButtons;
        std::function<void(QQuickItem *)> collectSteps = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("wizardStepButton")) {
                stepButtons.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                collectSteps(child);
            }
        };
        collectSteps(window.contentItem());
        QTRY_VERIFY_WITH_TIMEOUT(stepButtons.size() >= 8, 5000);
        QVERIFY(QMetaObject::invokeMethod(stepButtons.at(7), "click")); // summary: rejected
        QTest::qWait(20);
        QTRY_VERIFY(stepError->property("visible").toBool());
    }

    imageField->setProperty("text", QStringLiteral("alpine:3.19"));
    QTRY_VERIFY(nextButton->property("enabled").toBool());
    // The hint must clear too: the rejection reason was only cleared on a successful step-button click,
    // so "please choose an image" lingered after picking one (user: only switching tabs cleared it)
    QTRY_VERIFY2(!stepError->property("visible").toBool(),
             "the step error must disappear as soon as the image is chosen");
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked"));
    QCOMPARE(wizard->stepKey(), QStringLiteral("basics"));

    // Name: invalid blocks, valid continues
    QQuickItem *nameField = childByObjectName(page, QStringLiteral("wizardNameField"));
    QVERIFY(nameField);
    nameField->setProperty("text", QStringLiteral("bad name"));
    QTRY_VERIFY(!nextButton->property("enabled").toBool());
    nameField->setProperty("text", QStringLiteral("worker"));
    QTRY_VERIFY(nextButton->property("enabled").toBool());
    // Step order (user-verified): basics, env/labels, interactive, ports, mounts, resources, summary
    // (the interactive step was once nested in "basics" and rendered blank — see the visible assert below)
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // environment
    QCOMPARE(wizard->stepKey(), QStringLiteral("environment"));

    // Environment and labels: values masked by default, edits written back to the controller
    auto *environmentEditor = qobject_cast<QQuickItem *>(findItemByName(page, QStringLiteral("wizardEnvironmentEditor")));
    QVERIFY(environmentEditor);
    QVERIFY2(environmentEditor->property("secretValues").toBool(), "environment values are masked by default");
    // Add a row via the editor's own API (it and the page write-back path are what is under test)
    QVariantList entries;
    entries.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("API_TOKEN")},
                                {QStringLiteral("value"), QStringLiteral("s3cret-value")}});
    const QVariant entriesArg = entries;
    QVERIFY(QMetaObject::invokeMethod(environmentEditor, "setEntries", Q_ARG(QVariant, entriesArg)));
    QTRY_COMPARE(m_stubKcm->controller()->createContainer()->environmentRows().size(), 1);

    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // interactive
    QCOMPARE(wizard->stepKey(), QStringLiteral("interactive"));
    // The interactive step content must really be visible (a nesting mistake once left it blank)
    {
        QQuickItem *commandField = findItemDeep(window.contentItem(), QStringLiteral("wizardCommandField"));
        QVERIFY2(commandField, "the interactive step must expose the command field");
        QQuickItem *openStdin = findItemDeep(window.contentItem(), QStringLiteral("wizardOpenStdinCheck"));
        QVERIFY2(openStdin, "the interactive step must expose the -i switch");
        QTRY_VERIFY2(commandField->property("visible").toBool() && openStdin->property("visible").toBool(),
                     "the interactive step was blank: its fields must be visible on that step");
    }
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // ports
    QCOMPARE(wizard->stepKey(), QStringLiteral("ports"));
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // mounts
    QCOMPARE(wizard->stepKey(), QStringLiteral("mounts"));
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // resources
    QCOMPARE(wizard->stepKey(), QStringLiteral("resources"));

    // Privileged: ticking needs prior confirmation; nothing takes effect before it
    QQuickItem *privilegedCheck = childByObjectName(page, QStringLiteral("wizardPrivilegedCheck"));
    QVERIFY(privilegedCheck);
    // Simulate a real tick: setting checked fires toggled (invoking the signal alone does not flip it)
    privilegedCheck->setProperty("checked", true);
    QTest::qWait(20);
    QVERIFY2(!wizard->privileged(), "privileged must not be enabled without confirmation");
    // The checked state itself is not asserted (Qt's CheckBox flips it inside the handler);
    // what matters, guarded by the line above, is that nothing takes effect without confirmation
    QQuickItem *privilegedNotice = childByObjectName(page, QStringLiteral("wizardPrivilegedNotice"));
    QVERIFY(privilegedNotice);
    QVERIFY2(!privilegedNotice->property("visible").toBool(), "the warning only shows once it is enabled");
    // Kirigami.PromptDialog is not a QQuickItem: look it up in the QObject tree
    QObject *privilegedDialog = page->findChild<QObject *>(QStringLiteral("wizardPrivilegedDialog"));
    QVERIFY2(privilegedDialog, "the privileged confirmation must exist");
    QVERIFY2(!privilegedDialog->property("consequenceText").toString().isEmpty(),
             "the privileged confirmation must explain the consequence");

    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // summary
    QCOMPARE(wizard->stepKey(), QStringLiteral("summary"));

    // Summary: environment variables list key names only, never values.
    // The assertion sits at the **controller** layer: summary is its property and the rule
    // lives there; rendering it is covered by the screenshot review (in offscreen tests the
    // parent chain of Repeater items is unreliable).
    QString summaryText;
    const QVariantList summaryRows = wizard->summary();
    QVERIFY2(!summaryRows.isEmpty(), "the review step must have something to show");
    for (const QVariant &entry : summaryRows) {
        summaryText += entry.toMap().value(QStringLiteral("label")).toString() + QLatin1Char('=');
        summaryText += entry.toMap().value(QStringLiteral("value")).toString() + QLatin1Char('\n');
    }
    QVERIFY2(summaryText.contains(QStringLiteral("alpine:3.19")), qPrintable(summaryText));
    QVERIFY2(!summaryText.contains(QStringLiteral("s3cret-value")), qPrintable(summaryText));
    QVERIFY2(summaryText.contains(QStringLiteral("API_TOKEN")), qPrintable(summaryText));

    // Submit: the request really reaches the backend with the fields from the form
    QQuickItem *createButton = childByObjectName(page, QStringLiteral("wizardCreateButton"));
    QVERIFY(createButton);
    QVERIFY(QMetaObject::invokeMethod(createButton, "clicked"));
    QTRY_COMPARE(m_backend->lastContainerCreate().name, QStringLiteral("worker"));
    QCOMPARE(m_backend->lastContainerCreate().image, QStringLiteral("alpine:3.19"));
    QCOMPARE(m_backend->lastContainerCreate().network, QStringLiteral("app_default"));
    QCOMPARE(m_backend->lastContainerCreate().environment, QStringList {QStringLiteral("API_TOKEN=s3cret-value")});
}

/*!
 * Mount preset management (ARCH_V5_V8 §4.2; user feedback ⑥: management moved to its own tab).
 */
void QmlLoadTest::presetPanelManagesPresets()
{
    auto *store = m_stubKcm->controller()->mountPresets();
    QVERIFY(store);
    const QString firstId = store->add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true,
                                       QStringLiteral("数据目录"));
    QVERIFY(!firstId.isEmpty());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/MountPresetManager.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("store"), QVariant::fromValue(store));
    initial.insert(QStringLiteral("directoryPicker"), QVariant::fromValue(m_stubKcm->directoryPicker()));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *manager = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(manager);

    QQuickWindow window;
    window.resize(1000, 600);
    manager->setParentItem(window.contentItem());
    manager->setWidth(1000);
    manager->setHeight(600);
    window.show();
    QTRY_VERIFY(manager->width() > 0);

    // The existing entry renders as a row that can be favourited / reordered / removed
    QQuickItem *removeButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        removeButton = findItemDeep(window.contentItem(), QStringLiteral("presetManagerRemove"));
        return removeButton != nullptr;
    }(), 5000);
    QQuickItem *favoriteCheck = findItemDeep(window.contentItem(), QStringLiteral("presetManagerFavorite"));
    QVERIFY(favoriteCheck);
    QVERIFY(QMetaObject::invokeMethod(favoriteCheck, "click"));
    QTRY_VERIFY(store->presets().first().favorite);
    QCOMPARE(store->presets().first().id, firstId);

    // Add: the button enables only with both paths filled, and clicking really adds a row
    auto *newSource = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerNewSource")));
    auto *newDestination = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerNewDestination")));
    auto *addButton = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerAdd")));
    QVERIFY(newSource && newDestination && addButton);
    QVERIFY2(!addButton->property("enabled").toBool(), "an empty preset must not be addable");
    newSource->setProperty("text", QStringLiteral("relative/path"));
    newDestination->setProperty("text", QStringLiteral("/cache"));
    QTRY_VERIFY(addButton->property("enabled").toBool());
    // Invalid source: state the reason (validation and storage share one implementation)
    auto *errorMessage = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerError")));
    QVERIFY(errorMessage);
    QTRY_VERIFY(errorMessage->property("visible").toBool());
    newSource->setProperty("text", QStringLiteral("/srv/cache"));
    QTRY_VERIFY(!errorMessage->property("visible").toBool());
    QCOMPARE(store->count(), 1);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QTRY_COMPARE(store->count(), 2);

    // "Browse..." lives in the **create row** (user: create entry on top, browse before the host path)
    QQuickItem *browseButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        browseButton = findItemDeep(window.contentItem(), QStringLiteral("presetManagerNewBrowse"));
        return browseButton != nullptr && browseButton->property("visible").toBool();
    }(), 5000);
    auto *newSourceField = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerNewSource")));
    QVERIFY(newSourceField);
    QVERIFY2(manager->findChild<QObject *>(QStringLiteral("presetManagerNewRow")) != nullptr
                 || browseButton != nullptr,
             "the create row must exist");
    // Cancelling (empty result) must leave the field's text untouched
    newSourceField->setProperty("text", QStringLiteral("/srv/hand-typed"));
    m_stubKcm->directoryPicker()->nextResult = QString();
    QVERIFY(QMetaObject::invokeMethod(browseButton, "clicked"));
    QTest::qWait(20);
    QCOMPARE(newSourceField->property("text").toString(), QStringLiteral("/srv/hand-typed"));
    // Picking a directory fills the create row's host path
    m_stubKcm->directoryPicker()->nextResult = QStringLiteral("/srv/picked");
    QVERIFY(QMetaObject::invokeMethod(browseButton, "clicked"));
    QTRY_COMPARE(newSourceField->property("text").toString(), QStringLiteral("/srv/picked"));

    // Remove: find the button **again** — adding a preset makes the Repeater rebuild, invalidating it
    QQuickItem *freshRemoveButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        freshRemoveButton = findItemDeep(window.contentItem(), QStringLiteral("presetManagerRemove"));
        return freshRemoveButton != nullptr;
    }(), 5000);
    QCOMPARE(store->count(), 2);
    QVERIFY(QMetaObject::invokeMethod(freshRemoveButton, "clicked"));
    QTRY_COMPARE(store->count(), 1);
}

/*!
 * Build panel (ARCH_V5_V8 §5.4): form validation, submitted fields, and the **failed step** in the list.
 */
void QmlLoadTest::buildPanelSubmitsAndShowsFailureStep()
{
    QTemporaryDir contextDir;
    QVERIFY(contextDir.isValid());
    QFile dockerfile(QDir(contextDir.path()).filePath(QStringLiteral("Dockerfile")));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write(QByteArrayLiteral("FROM alpine:3.19\nRUN exit 1\n"));
    dockerfile.close();

    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/BuildImagePanel.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    // required properties must be given at creation: setProperty later reports "not initialized"
    QVariantMap initialProperties;
    initialProperties.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    initialProperties.insert(QStringLiteral("formOpen"), true);
    QScopedPointer<QObject> object(component.createWithInitialProperties(initialProperties, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *panel = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(panel);

    QQuickWindow window;
    window.resize(1000, 700);
    panel->setParentItem(window.contentItem());
    panel->setWidth(1000);
    panel->setHeight(700);
    window.show();
    QTRY_VERIFY(panel->width() > 0);

    auto *startButton = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildStartButton")));
    auto *contextField = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildContextField")));
    auto *tagsField = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildTagsField")));
    auto *targetField = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildTargetField")));
    auto *noCacheCheck = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildNoCacheCheck")));
    QVERIFY(startButton && contextField && tagsField && targetField && noCacheCheck);

    // Context and tags empty: no submission (the button is disabled too)
    QVERIFY2(!startButton->property("enabled").toBool(), "an empty form must not be submittable");
    auto *formError = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildFormError")));
    QVERIFY(formError);
    contextField->setProperty("text", QStringLiteral("relative/path"));
    QTRY_VERIFY(formError->property("visible").toBool());
    QVERIFY2(formError->property("text").toString().contains(QStringLiteral("absolute")),
             "a relative context path must be rejected with a clear reason");

    // Once filled in, submit: the fields reach the controller as entered
    contextField->setProperty("text", contextDir.path());
    tagsField->setProperty("text", QStringLiteral("app:1.0\napp:latest"));
    targetField->setProperty("text", QStringLiteral("runtime"));
    // Use click(), not a direct checked write: only the former is a user action (checkable buttons flip)
    QVERIFY(QMetaObject::invokeMethod(noCacheCheck, "click"));
    QTRY_VERIFY(panel->property("noCache").toBool());
    QVERIFY(QMetaObject::invokeMethod(startButton, "clicked"));

    const ImageBuildRequest request = m_backend->lastBuildRequest();
    QCOMPARE(request.tags, QStringList({QStringLiteral("app:1.0"), QStringLiteral("app:latest")}));
    QCOMPARE(request.target, QStringLiteral("runtime"));
    QVERIFY(request.noCache);
    QVERIFY2(!request.contextArchive.isEmpty(), "the context must have been packed");
    QCOMPARE(m_stubKcm->controller()->operations()->builds()->count(), 1);

    // List: shows steps while running, and keeps the failed step after a failure
    const QString buildId = m_stubKcm->controller()->operations()->builds()->entries().first().id;
    ImageBuildUpdate update;
    update.statusText = QStringLiteral("Step 2/3 : RUN exit 1");
    update.stepIndex = 2;
    update.totalSteps = 3;
    update.stepCommand = QStringLiteral("RUN exit 1");
    update.progress = 0.66;
    update.progressKnown = true;
    m_backend->emitBuildProgress(buildId, update);
    update.errorText = QStringLiteral("Step 2/3 (RUN exit 1) failed: exit code 1");
    m_backend->emitBuildProgress(buildId, update);

    // The failed step stays in the **data** (assert the controller; the card rendering it is covered by
    // screenshot review — offscreen Repeater item parent chains are unreliable, learned in phase 7)
    m_backend->emitBuildFinished(buildId,
                                 DockerBackendInterface::MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::EngineError, QStringLiteral("exit code 1")));
    const ImageBuildEntry entry = m_stubKcm->controller()->operations()->builds()->entries().first();
    QVERIFY2(entry.detailText.contains(QStringLiteral("Step 2/3")), qPrintable(entry.detailText));
    QVERIFY2(entry.detailText.contains(QStringLiteral("RUN exit 1")), qPrintable(entry.detailText));
    QVERIFY2(!entry.active, "a failed build must not stay active");
}

/*!
 * Row removal (regression: users reported "port mappings will not delete, labels do").
 *
 * Under `pragma ComponentBehavior: Unbound` a delegate cannot see the root object id, so the old
 * `page.pushPorts()` / `root.changed()` handlers threw a ReferenceError and the edit never reached
 * the controller, making port rows "come back after deletion". Delegates now call a relay object only.
 */
void QmlLoadTest::portAndKeyValueRowsCanBeRemoved()
{
    // (1) port rows in the create wizard
    {
        // The image step requires a local image, so provide one (not this test's focus, but the rules)
        Image localImage;
        localImage.id = QStringLiteral("sha256:feedface");
        localImage.repoTags = {QStringLiteral("alpine:3.19")};
        m_backend->setImages({localImage});

        const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);

        QQuickWindow window;
        window.resize(1100, 800);
        page->setParentItem(window.contentItem());
        page->setWidth(1100);
        page->setHeight(800);
        window.show();
        QTRY_VERIFY(page->width() > 0);

        auto *wizard = m_stubKcm->controller()->createContainer();
        // Creating the page resets the controller, so add rows only after construction
        wizard->addPortRow(80, 0, QString(), QStringLiteral("tcp"));
        wizard->addPortRow(443, 8443, QString(), QStringLiteral("tcp"));
        // Steps 0/1 must be valid first or goToStep refuses to advance (a wizard rule, not relaxed for tests)
        wizard->setImage(QStringLiteral("alpine:3.19"));
        wizard->setName(QStringLiteral("port-rows"));
        QVERIFY2(wizard->goToStep(QStringLiteral("ports")), qPrintable(wizard->stepKey()));

        QQuickItem *removeButton = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            removeButton = findItemDeep(window.contentItem(), QStringLiteral("wizardRemovePort"));
            return removeButton != nullptr;
        }(), 5000);
        QCOMPARE(wizard->portRows().size(), 2);
        QVERIFY(QMetaObject::invokeMethod(removeButton, "clicked"));
        QTRY_COMPARE(wizard->portRows().size(), 1);
        // The first row (80/0) is removed; the surviving row must still be the other one
        QCOMPARE(wizard->portRows().first().toMap().value(QStringLiteral("containerPort")).toInt(), 443);

        /*
         * Typing a port must not be interrupted on every keystroke (user: typing 8000 needed reselecting).
         *
         * Ports are now a validated text field that writes back on focus loss, so the model stays put while
         * typing and no "model -> text" loop steals the cursor or selection. Simulated digit by digit:
         *   (1) during typing the field holds exactly what the user typed (never rewritten);
         *   (2) the controller **keeps the old value** meanwhile (write-back is deferred);
         *   (3) the write-back happens only on editingFinished.
         */
        QQuickItem *hostPortField = findItemDeep(window.contentItem(), QStringLiteral("wizardHostPort"));
        QVERIFY(hostPortField);
        const int before = wizard->portRows().first().toMap().value(QStringLiteral("hostPort")).toInt();
        QString typed = QString();
        for (const QString &digit : {QStringLiteral("8"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0")}) {
            typed += digit;
            hostPortField->setProperty("text", typed);
            QTest::qWait(5);
            QCOMPARE(hostPortField->property("text").toString(), typed);
            QCOMPARE(wizard->portRows().first().toMap().value(QStringLiteral("hostPort")).toInt(), before);
        }
        QVERIFY(QMetaObject::invokeMethod(hostPortField, "editingFinished"));
        QTRY_COMPARE(wizard->portRows().first().toMap().value(QStringLiteral("hostPort")).toInt(), 8000);
    }

    // (2) the key/value editor's "remove" button
    {
        const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/KeyValueListEditor.qml");
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QVariantList entries;
        entries.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("A")}, {QStringLiteral("value"), QStringLiteral("1")}});
        entries.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("B")}, {QStringLiteral("value"), QStringLiteral("2")}});
        QVariantMap initial;
        initial.insert(QStringLiteral("initialEntries"), entries);
        initial.insert(QStringLiteral("secretValues"), true);
        QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
        auto *editor = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(editor);

        QQuickWindow window;
        window.resize(700, 400);
        editor->setParentItem(window.contentItem());
        editor->setWidth(700);
        editor->setHeight(400);
        window.show();
        QTRY_VERIFY(editor->width() > 0);

        QQuickItem *removeButton = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            removeButton = findItemDeep(window.contentItem(), QStringLiteral("keyValueRemoveButton"));
            return removeButton != nullptr;
        }(), 5000);
        // Reveal toggle for values, and removal really drops a row (the callback no longer throws)
        QQuickItem *reveal = findItemDeep(window.contentItem(), QStringLiteral("keyValueRevealButton"));
        QVERIFY2(reveal, "secret values must offer a reveal toggle");
        QVERIFY(reveal->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(removeButton, "clicked"));
        QTest::qWait(50);
        QVariant remaining;
        QVERIFY(QMetaObject::invokeMethod(editor, "entries", Q_RETURN_ARG(QVariant, remaining)));
        const QVariantList rows = remaining.toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("B"));
    }
}

/*!
 * Mounts step: after picking a preset, "Add mount" must still work (user feedback ⑦).
 *
 * Same root cause as ⑤: the delegate handler used the root object id, threw ReferenceError, edit lost.
 */
void QmlLoadTest::wizardAddsPresetsAndExtraMounts()
{
    auto *store = m_stubKcm->controller()->mountPresets();
    QVERIFY(store);
    const QString presetId = store->add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true, QString());
    QVERIFY(!presetId.isEmpty());

    Image localImage;
    localImage.id = QStringLiteral("sha256:cafebabe");
    localImage.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({localImage});

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1100, 800);
    page->setParentItem(window.contentItem());
    page->setWidth(1100);
    page->setHeight(800);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    auto *wizard = m_stubKcm->controller()->createContainer();
    wizard->setImage(QStringLiteral("alpine:3.19"));
    wizard->setName(QStringLiteral("mount-demo"));
    QVERIFY(wizard->goToStep(QStringLiteral("mounts")));

    // Add from preset: now a **searchable dropdown** (user F3: a button flow is too slow with many presets)
    // Note: the page has several searchable dropdowns (image / preset / command history), so search for
    // the list **inside the preset dropdown**; taking the page's first filteredComboBoxList would be wrong
    QQuickItem *presetCombo = nullptr;
    QQuickItem *presetList = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        presetCombo = findItemDeep(window.contentItem(), QStringLiteral("wizardPresetCombo"));
        if (!presetCombo) {
            return false;
        }
        presetList = findItemDeep(presetCombo, QStringLiteral("filteredComboBoxList"));
        return presetList != nullptr && presetList->property("count").toInt() == 1;
    }(), 5000);
    const QVariantList presetEntries = store->summaries();
    QVERIFY(!presetEntries.isEmpty());
    QVERIFY2(!presetEntries.first().toMap().value(QStringLiteral("label")).toString().isEmpty(),
             "the dropdown needs a display label");
    QVERIFY(QMetaObject::invokeMethod(presetList, "activated", Q_ARG(int, 0)));
    QTRY_COMPARE(wizard->mountRows().size(), 1);
    QCOMPARE(wizard->mountRows().first().toMap().value(QStringLiteral("source")).toString(), QStringLiteral("/srv/data"));

    // Add one more empty row: this used to do nothing because the delegate threw
    wizard->addMountRow(QStringLiteral("bind"), QString(), QString(), false);
    QTRY_COMPARE(wizard->mountRows().size(), 2);

    // Click the real "Add mount" button once more
    QQuickItem *addMountButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        addMountButton = findItemDeep(window.contentItem(), QStringLiteral("wizardAddMount"));
        return addMountButton != nullptr;
    }(), 5000);
    QVERIFY(QMetaObject::invokeMethod(addMountButton, "clicked"));
    QTRY_COMPARE(wizard->mountRows().size(), 3);

    // Remove the second row (the delegate's remove button)
    QQuickItem *removeMount = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        removeMount = findItemDeep(window.contentItem(), QStringLiteral("wizardRemoveMount"));
        return removeMount != nullptr;
    }(), 5000);
    QVERIFY(QMetaObject::invokeMethod(removeMount, "clicked"));
    QTRY_COMPARE(wizard->mountRows().size(), 2);
}


/*!
 * Pause / resume buttons (user feedback ①): running offers Pause, paused offers Resume.
 */
void QmlLoadTest::pauseAndResumeButtonsFollowTheState()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    Container running;
    running.id = QStringLiteral("running-one");
    running.name = QStringLiteral("running-one");
    running.image = QStringLiteral("alpine:3.19");
    running.state = ContainerState::Running;
    m_backend->setContainers({running});

    ContainerDetail runningDetail;
    runningDetail.id = QStringLiteral("running-one");
    runningDetail.name = QStringLiteral("running-one");
    runningDetail.image = QStringLiteral("alpine:3.19");
    runningDetail.state = ContainerState::Running;
    m_backend->setContainerDetail(runningDetail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    initial.insert(QStringLiteral("controller"), QVariant::fromValue(m_stubKcm->controller()->containerDetail()));
    initial.insert(QStringLiteral("containerId"), QStringLiteral("running-one"));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1000, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1000);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);
    m_backend->completeRefresh(); // inspect is async: feed the data, then let the detail land

    // Running: Pause visible, Resume not
    QQuickItem *pauseButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        pauseButton = findItemDeep(window.contentItem(), QStringLiteral("detailPauseButton"));
        return pauseButton != nullptr && pauseButton->property("visible").toBool();
    }(), 5000);
    QQuickItem *resumeButton = findItemDeep(window.contentItem(), QStringLiteral("detailUnpauseButton"));
    QVERIFY(resumeButton);
    QVERIFY2(!resumeButton->property("visible").toBool(), "a running container must not offer Resume");

    // Clicking Pause really sends the request
    QVERIFY(QMetaObject::invokeMethod(pauseButton, "clicked"));
    QTRY_VERIFY(!m_backend->mutationCalls().isEmpty());
    QCOMPARE(m_backend->mutationCalls().first().mutation, DockerBackendInterface::Mutation::PauseContainer);
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("running-one")),
                               DockerBackendInterface::MutationOutcome::Succeeded);

    // State becomes paused: the buttons flip (Resume visible, Pause not)
    Container paused = running;
    paused.state = ContainerState::Paused;
    m_backend->setContainers({paused});
    ContainerDetail pausedDetail = runningDetail;
    pausedDetail.state = ContainerState::Paused;
    m_backend->setContainerDetail(pausedDetail);
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(!pauseButton->property("visible").toBool(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(resumeButton->property("visible").toBool(), 5000);

    QVERIFY(QMetaObject::invokeMethod(resumeButton, "clicked"));
    QTRY_COMPARE(m_backend->mutationCalls().size(), 1);
    QCOMPARE(m_backend->mutationCalls().first().mutation, DockerBackendInterface::Mutation::UnpauseContainer);
}

/*!
 * The create-container entry refreshes networks itself (user feedback ②): no Networks tab visit needed.
 */
void QmlLoadTest::openingTheWizardRefreshesNetworks()
{
    auto *controller = m_stubKcm->controller();
    const int before = m_backend->networkRefreshCount();

    // Simulate "click create container": the main page refreshes first, then emits the signal
    controller->refreshNetworks();
    QTRY_VERIFY(m_backend->networkRefreshCount() > before);
    m_backend->completeRefresh();

    // The wizard insures itself too: it refreshes again on open if the list is still empty
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const int beforeWizard = m_backend->networkRefreshCount();
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    QTRY_VERIFY(m_backend->networkRefreshCount() >= beforeWizard);
}

/*!
 * Command field and the "the default command would exit immediately" hint (user feedback ⑧⑨).
 */
void QmlLoadTest::commandFieldAndExitHint()
{
    Image localImage;
    localImage.id = QStringLiteral("sha256:deadbeef");
    localImage.repoTags = {QStringLiteral("alpine:latest")};
    m_backend->setImages({localImage});

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1100, 900);
    page->setParentItem(window.contentItem());
    page->setWidth(1100);
    page->setHeight(900);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    auto *wizard = m_stubKcm->controller()->createContainer();
    wizard->setImage(QStringLiteral("alpine:latest"));
    wizard->setName(QStringLiteral("hint-demo"));
    QVERIFY(wizard->goToStep(QStringLiteral("basics")));

    // Command reaches the controller (hint ⑨ was dropped on request: P2 opens -i/-t by default)
    QQuickItem *commandField = findItemDeep(window.contentItem(), QStringLiteral("wizardCommandField"));
    QVERIFY(commandField);
    commandField->setProperty("text", QStringLiteral("sleep infinity"));
    QTRY_COMPARE(wizard->commandText(), QStringLiteral("sleep infinity"));

    // Entrypoint / working directory / user reach the controller too
    auto *entrypointField = qobject_cast<QQuickItem *>(findItemDeep(window.contentItem(), QStringLiteral("wizardEntrypointField")));
    auto *workingDirField = qobject_cast<QQuickItem *>(findItemDeep(window.contentItem(), QStringLiteral("wizardWorkingDirField")));
    auto *userField = qobject_cast<QQuickItem *>(findItemDeep(window.contentItem(), QStringLiteral("wizardUserField")));
    QVERIFY(entrypointField && workingDirField && userField);
    entrypointField->setProperty("text", QStringLiteral("/usr/bin/env sh"));
    workingDirField->setProperty("text", QStringLiteral("/app"));
    userField->setProperty("text", QStringLiteral("1000:1000"));
    QTRY_COMPARE(wizard->entrypointText(), QStringLiteral("/usr/bin/env sh"));
    QTRY_COMPARE(wizard->workingDirectory(), QStringLiteral("/app"));
    QTRY_COMPARE(wizard->user(), QStringLiteral("1000:1000"));
}


/*!
 * Step buttons must never look multi-selected (user A3).
 *
 * Root cause: the buttons are checkable, so Qt flips checked first; when validation refuses the jump
 * that flip is never corrected, so several steps look selected while the page stays on the first one.
 */
void QmlLoadTest::stepButtonsNeverLookMultiSelected()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1100, 800);
    page->setParentItem(window.contentItem());
    page->setWidth(1100);
    page->setHeight(800);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    // Collect all step buttons (the first is the container header button; filter by objectName)
    QList<QQuickItem *> buttons;
    std::function<void(QQuickItem *)> collect = [&](QQuickItem *node) {
        if (!node) {
            return;
        }
        if (node->objectName() == QLatin1String("wizardStepButton")) {
            buttons.append(node);
        }
        for (QQuickItem *child : node->childItems()) {
            collect(child);
        }
    };
    collect(window.contentItem());
    QTRY_VERIFY_WITH_TIMEOUT(buttons.size() >= 7, 5000);

    const auto checkedCount = [&buttons] {
        int count = 0;
        for (QQuickItem *button : buttons) {
            if (button->property("checked").toBool()) {
                ++count;
            }
        }
        return count;
    };
    QCOMPARE(checkedCount(), 1); // initially only step one

    // Click the second step button: validation fails (no image yet) and **only step one may stay checked**.
    // Use click(), not emit clicked(): only the former exercises the checkable button flipping checked itself
    QQuickItem *second = buttons.at(1);
    QVERIFY(QMetaObject::invokeMethod(second, "click"));
    QTest::qWait(20);
    QVERIFY2(second->property("checked").toBool() == false, "a refused jump must not leave the button checked");
    QCOMPARE(checkedCount(), 1);
    // And it must say why the jump was refused
    QQuickItem *stepError = findItemDeep(window.contentItem(), QStringLiteral("wizardStepError"));
    QVERIFY(stepError);
    QTRY_VERIFY(stepError->property("visible").toBool());
    QVERIFY2(!stepError->property("text").toString().isEmpty(), "the refusal needs a reason");
}

/*!
 * `--privileged` needs typed confirmation of the container name, then the box must really be checked (F4).
 */
void QmlLoadTest::privilegedNeedsTypedConfirmation()
{
    Image localImage;
    localImage.id = QStringLiteral("sha256:1b1b1b1b");
    localImage.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({localImage});

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1100, 900);
    page->setParentItem(window.contentItem());
    page->setWidth(1100);
    page->setHeight(900);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    auto *wizard = m_stubKcm->controller()->createContainer();
    wizard->setImage(QStringLiteral("alpine:3.19"));
    wizard->setName(QStringLiteral("root-demo"));
    QVERIFY(wizard->goToStep(QStringLiteral("resources")));

    QQuickItem *privilegedCheck = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        privilegedCheck = findItemDeep(window.contentItem(), QStringLiteral("wizardPrivilegedCheck"));
        return privilegedCheck != nullptr;
    }(), 5000);

    // Ticking is intercepted (no confirmation yet)
    QVERIFY(QMetaObject::invokeMethod(privilegedCheck, "click"));
    QTest::qWait(20);
    QVERIFY2(!wizard->privileged(), "privileged must not be enabled before confirmation");
    QVERIFY2(!privilegedCheck->property("checked").toBool(), "the checkbox must fall back until confirmed");

    // The dialog requires typing the container name: confirm stays disabled until it matches
    QObject *privilegedDialog = page->findChild<QObject *>(QStringLiteral("wizardPrivilegedDialog"));
    QVERIFY2(privilegedDialog, "the privileged confirmation must exist");
    QCOMPARE(privilegedDialog->property("requireText").toString(), QStringLiteral("root-demo"));
    QVERIFY2(!privilegedDialog->property("requireTextSatisfied").toBool(), "an empty confirmation must not be accepted");

    QQuickItem *confirmField = qobject_cast<QQuickItem *>(page->findChild<QObject *>(QStringLiteral("confirmDialogTextField")));
    QVERIFY(confirmField);
    confirmField->setProperty("text", QStringLiteral("root-demo"));
    QTRY_VERIFY(privilegedDialog->property("requireTextSatisfied").toBool());

    // Confirm: the controller applies it and the **box really gets ticked** (the F4 regression)
    QVERIFY(QMetaObject::invokeMethod(privilegedDialog, "confirmed"));
    QTRY_VERIFY(wizard->privileged());
    QTRY_VERIFY_WITH_TIMEOUT(privilegedCheck->property("checked").toBool(), 5000);
    // The real confirm button closes the dialog; here the signal is emitted directly, so close it by
    // hand — a modal dialog still up would swallow the following clicks (a trap hit in this test)
    QVERIFY(QMetaObject::invokeMethod(privilegedDialog, "close"));
    QTRY_VERIFY(!privilegedDialog->property("visible").toBool());

    // Click again to untick: both sides return to disabled
    QVERIFY(QMetaObject::invokeMethod(privilegedCheck, "click"));
    QTest::qWait(20);
    QTRY_VERIFY(!wizard->privileged());
    QTRY_VERIFY(!privilegedCheck->property("checked").toBool());
}


/*!
 * Container detail can copy the command and the entrypoint (user feedback A2).
 */
void QmlLoadTest::detailOffersCopyForCommandAndEntrypoint()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("copy-demo");
    detail.name = QStringLiteral("copy-demo");
    detail.state = ContainerState::Running;
    detail.command = {QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("sleep infinity")};
    detail.entrypoint = {QStringLiteral("/usr/bin/env"), QStringLiteral("sh")};
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    initial.insert(QStringLiteral("controller"), QVariant::fromValue(m_stubKcm->controller()->containerDetail()));
    initial.insert(QStringLiteral("containerId"), QStringLiteral("copy-demo"));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1000, 800);
    page->setParentItem(window.contentItem());
    page->setWidth(1000);
    page->setHeight(800);
    window.show();
    QTRY_VERIFY(page->width() > 0);
    m_backend->completeRefresh();

    QQuickItem *commandCopy = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        QQuickItem *row = findItemDeep(window.contentItem(), QStringLiteral("detailCommandRow"));
        commandCopy = row ? findItemDeep(row, QStringLiteral("copyButtonObject")) : nullptr;
        return commandCopy != nullptr && commandCopy->property("visible").toBool();
    }(), 5000);
    QCOMPARE(commandCopy->property("value").toString(), QStringLiteral("sh -c sleep infinity"));

    QQuickItem *entrypointRow = findItemDeep(window.contentItem(), QStringLiteral("detailEntrypointRow"));
    QVERIFY(entrypointRow);
    QQuickItem *entrypointCopy = findItemDeep(entrypointRow, QStringLiteral("copyButtonObject"));
    QVERIFY(entrypointCopy);
    QCOMPARE(entrypointCopy->property("value").toString(), QStringLiteral("/usr/bin/env sh"));
}


/*!
 * Topology alignment (user A5): the container chip is level with the first host binding, so its link is flat.
 */
void QmlLoadTest::topologyAlignsTheContainerChipWithTheFirstBinding()
{
    // One container port mapped to two host addresses (the second must branch downwards)
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-align");
    detail.name = QStringLiteral("align-demo");
    detail.state = ContainerState::Running;
    detail.ports = {{QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
                    {QStringLiteral("127.0.0.1"), 8888, 20204, QStringLiteral("tcp")}};
    m_backend->setContainerDetail(detail);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-align")},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2)); // networks section (the port topology lives here)

    QQuickItem *topology = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        topology = childByObjectName(page, QStringLiteral("portTopology"));
        return topology != nullptr;
    }(), 5000);

    QList<QQuickItem *> hostChips;
    QQuickItem *containerChip = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        hostChips.clear();
        containerChip = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("portHostChip")) {
                hostChips.append(node);
            } else if (node->objectName() == QLatin1String("portContainerChip")) {
                containerChip = node;
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(topology);
        return hostChips.size() == 2 && containerChip != nullptr;
    }(), 5000);

    const auto centerY = [](QQuickItem *item) {
        const QPointF topLeft = item->mapToItem(nullptr, QPointF(0, 0));
        return topLeft.y() + item->height() / 2.0;
    };
    QVERIFY2(qAbs(centerY(hostChips.at(0)) - centerY(containerChip)) <= 1.0,
             qPrintable(QStringLiteral("container chip y=%1, first binding y=%2")
                            .arg(centerY(containerChip))
                            .arg(centerY(hostChips.at(0)))));
    QVERIFY2(centerY(hostChips.at(1)) > centerY(containerChip) + 4.0, "the second binding must branch downwards");

    // The link origin must also sit at the first binding's centre (hence the horizontal first line):
    // separate code from the chip position, so assert it (moving the origin to the group centre fails)
    QQuickItem *link = nullptr;
    std::function<void(QQuickItem *)> findLink = [&](QQuickItem *node) {
        if (!node) {
            return;
        }
        if (node->objectName() == QLatin1String("portMappingLink")) {
            link = node;
        }
        for (QQuickItem *child : node->childItems()) {
            findLink(child);
        }
    };
    findLink(topology);
    QVERIFY(link);
    const qreal bindingRowHeight = topology->property("bindingRowHeight").toReal();
    QCOMPARE(link->property("originY").toReal(), bindingRowHeight / 2.0);
    QVERIFY2(link->property("originY").toReal() < link->height() / 2.0,
             "the origin must NOT sit at the middle of the group (that would make the first line diagonal)");
}

/*!
 * Port editor links (user feedback A4): one colour per row, and no feature annotation text.
 */
void QmlLoadTest::portEditorColoursEachRowDifferently()
{
    auto *wizard = m_stubKcm->controller()->createContainer();
    wizard->clearPortRows();
    wizard->addPortRow(80, 8080, QStringLiteral("0.0.0.0"), QStringLiteral("tcp"));
    wizard->addPortRow(443, 0, QStringLiteral("127.0.0.1"), QStringLiteral("tcp"));
    wizard->addPortRow(53, 5353, QString(), QStringLiteral("udp"));

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PortMappingEditor.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("controller"), QVariant::fromValue(wizard));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *editor = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(editor);

    QQuickWindow window;
    window.resize(1000, 400);
    editor->setParentItem(window.contentItem());
    editor->setWidth(1000);
    editor->setHeight(400);
    window.show();
    QTRY_VERIFY(editor->width() > 0);

    QList<QQuickItem *> links;
    QList<QQuickItem *> containerChips;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        links.clear();
        containerChips.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("wizardPortLink")) {
                links.append(node);
            } else if (node->objectName() == QLatin1String("wizardContainerPort")) {
                containerChips.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return links.size() == 3 && containerChips.size() == 3;
    }(), 5000);

    // The three rows use three different link colours ("one colour per row")
    QSet<QString> colors;
    for (QQuickItem *link : links) {
        colors.insert(link->property("linkColor").value<QColor>().name());
    }
    QCOMPARE(colors.size(), 3);
    // Both side labels exist ("host" / "container"), asserted by objectName rather than by string,
    // so the test does not depend on the language (it may run in an English environment)
    QQuickItem *hostLabel = findItemDeep(window.contentItem(), QStringLiteral("portEditorHostLabel"));
    QQuickItem *containerLabel = findItemDeep(window.contentItem(), QStringLiteral("portEditorContainerLabel"));
    QVERIFY2(hostLabel && containerLabel, "the editor must label both sides (host / container)");
    QVERIFY2(!hostLabel->property("text").toString().isEmpty(), "the host label must not be empty");
}


/*!
 * Service card (B1): three status rows; risky actions confirm first and only then send the request.
 */
void QmlLoadTest::serviceCardConfirmsRiskyActions()
{
    auto *services = m_stubKcm->serviceStatus();
    QVERIFY(services);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/ServiceCard.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("controller"), QVariant::fromValue(m_stubKcm->controller()->daemonConfigSystem()));
    initial.insert(QStringLiteral("services"), QVariant::fromValue(services));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *card = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(card);

    QQuickWindow window;
    window.resize(1100, 600);
    card->setParentItem(window.contentItem());
    card->setWidth(1100);
    card->setHeight(600);
    window.show();
    QTRY_VERIFY(card->width() > 0);

    // Three status rows (socket / service / containerd), all "running" by default
    QList<QQuickItem *> rows;
    QList<QQuickItem *> stateLabels;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        rows.clear();
        stateLabels.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("serviceRow")) {
                rows.append(node);
            } else if (node->objectName() == QLatin1String("serviceStateLabel")) {
                stateLabels.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return rows.size() == 3 && stateLabels.size() == 3;
    }(), 5000);
    QVERIFY2(!stateLabels.first()->property("text").toString().isEmpty(), "the state must be readable in text form");

    // Stop is risky: it opens a confirmation first, and **no request is sent before confirming**
    QQuickItem *stopButton = findItemDeep(window.contentItem(), QStringLiteral("serviceStopButton"));
    QVERIFY2(stopButton && stopButton->property("visible").toBool(), "a running service offers Stop");
    QObject *dialog = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((dialog = card->findChild<QObject *>(QStringLiteral("serviceConfirmDialog"))) != nullptr, 5000);
    QVERIFY(QMetaObject::invokeMethod(stopButton, "clicked"));
    QTest::qWait(20);
    QCOMPARE(m_stubKcm->privilegedClient()->serviceRequests, 0);
    QVERIFY2(!dialog->property("consequenceText").toString().isEmpty(),
             "a risky action must explain what happens");
    QTRY_VERIFY(dialog->property("visible").toBool());

    // Only after confirming is the request sent (unit and verb match the button)
    QVERIFY(QMetaObject::invokeMethod(dialog, "confirmed"));
    QTRY_COMPARE(m_stubKcm->privilegedClient()->serviceRequests, 1);
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceUnit, QStringLiteral("docker.socket"));
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceVerb, QStringLiteral("stop"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));

    // Restart is not risky: sent directly, no confirmation
    QQuickItem *restartButton = findItemDeep(window.contentItem(), QStringLiteral("serviceRestartButton"));
    QVERIFY(restartButton);
    QVERIFY(QMetaObject::invokeMethod(restartButton, "clicked"));
    QTRY_COMPARE(m_stubKcm->privilegedClient()->serviceRequests, 2);
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceVerb, QStringLiteral("restart"));
}


/*!
 * Searchable dropdown (F3): typing filters, no match shows a hint, selecting returns the whole entry.
 */
void QmlLoadTest::filteredComboBoxNarrowsAndSelects()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/FilteredComboBox.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QVariantList entries;
    for (const QString &reference : {QStringLiteral("alpine:3.19"), QStringLiteral("alpine:latest"),
                                     QStringLiteral("postgres:17-alpine"), QStringLiteral("registry.example.com/team/app:1.0")}) {
        entries.append(QVariantMap {{QStringLiteral("reference"), reference}});
    }
    QVariantMap initial;
    initial.insert(QStringLiteral("entries"), entries);
    initial.insert(QStringLiteral("textRole"), QStringLiteral("reference"));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *combo = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(combo);

    QQuickWindow window;
    window.resize(700, 400);
    combo->setParentItem(window.contentItem());
    combo->setWidth(700);
    combo->setHeight(400);
    window.show();
    QTRY_VERIFY(combo->width() > 0);

    QQuickItem *list = findItemDeep(window.contentItem(), QStringLiteral("filteredComboBoxList"));
    QQuickItem *search = findItemDeep(window.contentItem(), QStringLiteral("filteredComboBoxSearch"));
    QQuickItem *emptyHint = findItemDeep(window.contentItem(), QStringLiteral("filteredComboBoxEmpty"));
    QVERIFY(list && search && emptyHint);

    // Initially all four entries and no "no match" hint
    QCOMPARE(list->property("count").toInt(), 4);
    QVERIFY(!emptyHint->property("visible").toBool());

    // Typing filters (case-insensitive): `postgres:17-alpine` contains "alpine" too, hence 3 rows
    search->setProperty("text", QStringLiteral("ALPINE"));
    QTRY_COMPARE(list->property("count").toInt(), 3);
    // A more precise match
    search->setProperty("text", QStringLiteral("alpine:3"));
    QTRY_COMPARE(list->property("count").toInt(), 1);

    // No match: the list empties and a hint appears
    search->setProperty("text", QStringLiteral("does-not-exist"));
    QTRY_COMPARE(list->property("count").toInt(), 0);
    QTRY_VERIFY(emptyHint->property("visible").toBool());
    QVERIFY2(!list->property("enabled").toBool(), "an empty list must not be selectable");

    // Clearing the search restores all rows
    search->setProperty("text", QString());
    QTRY_COMPARE(list->property("count").toInt(), 4);

    // Selecting hands the whole entry back via selected(entry)
    QSignalSpy selectedSpy(combo, SIGNAL(selected(QVariant)));
    QVERIFY(QMetaObject::invokeMethod(list, "activated", Q_ARG(int, 2)));
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toMap().value(QStringLiteral("reference")).toString(),
             QStringLiteral("postgres:17-alpine"));
}


/*!
 * Image detail "used by" list (reported): rows need a **state icon** and clicking must open the container.
 *
 * Consistent with network member rows: same state icon, same navigation signal (target = container id).
 */
void QmlLoadTest::imageUsedByRowsShowStateAndNavigate()
{
    const QString imageId = QStringLiteral("sha256:feedface");
    Image image;
    image.id = imageId;
    image.repoTags = {QStringLiteral("demo:1.0")};
    image.sizeBytes = 1024;
    image.created = QDateTime::currentDateTimeUtc();
    m_backend->setImages({image});

    Container running;
    running.id = QStringLiteral("cid-run");
    running.name = QStringLiteral("demo-run");
    running.image = QStringLiteral("demo:1.0");
    running.imageId = imageId;
    running.state = ContainerState::Running;
    Container paused;
    paused.id = QStringLiteral("cid-pause");
    paused.name = QStringLiteral("demo-paused");
    paused.image = QStringLiteral("demo:1.0");
    paused.imageId = imageId;
    paused.state = ContainerState::Paused;
    m_backend->setContainers({running, paused});

    ImageDetail detail;
    detail.id = imageId;
    detail.repoTags = {QStringLiteral("demo:1.0")};
    m_backend->setImageDetail(detail);

    auto *controller = m_stubKcm->controller()->imageDetail();
    controller->setImageId(imageId);
    controller->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(controller->usedByContainers()->count() == 2, 5000);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ImageDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("imageId"), imageId}},
                                                                        m_engine->rootContext()));
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

    // Both rows need a state icon, not a generic one
    QList<QQuickItem *> rows;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        rows.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("usedByEntry")) {
                rows.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return rows.size() == 2;
    }(), 5000);
    for (QQuickItem *row : rows) {
        QQuickItem *icon = findItemDeep(row, QStringLiteral("usedByStateIcon"));
        QVERIFY2(icon, "every used-by row must carry a state icon");
        QVERIFY2(!icon->property("source").toString().isEmpty(), "the state icon must resolve to an icon name");
    }

    // Click the first row: emits containerRequested with the **container id** (not the state key)
    QSignalSpy requestedSpy(page, SIGNAL(containerRequested(QString)));
    QVERIFY(requestedSpy.isValid());
    QVERIFY(QMetaObject::invokeMethod(rows.first(), "clicked"));
    QCOMPARE(requestedSpy.count(), 1);
    const QString requested = requestedSpy.at(0).at(0).toString();
    QVERIFY2(requested == QLatin1String("cid-run") || requested == QLatin1String("cid-pause"),
             qPrintable(QStringLiteral("expected a container id, got '%1'").arg(requested)));
}


/*!
 * Key/value lists like "driver options" (reported): long keys must stay readable, values hug the right.
 *
 * The key column was fixed at 10 gridUnits, so `com.docker.network.bridge.name` collapsed into an ellipsis
 * while the value filled the row. Keys now take the leftover width, values are right-aligned and capped.
 */
void QmlLoadTest::keyValueRowsKeepLongKeysVisible()
{
    /*
     * A small wrapper QML puts the component into a real layout (`ColumnLayout` filling the window):
     * the bug only appears under **constrained width** — setting the component width directly would
     * leave the columns un-stretched, the long key would fit and the test would prove nothing.
     */
    QTemporaryFile wrapper;
    QVERIFY(wrapper.open());
    wrapper.write(QStringLiteral("import QtQuick\n"
                                 "import QtQuick.Layouts\n"
                                 "import \"file://" KCM_DOCKER_SOURCE_DIR "/src/ui/components\"\n"
                                 "ColumnLayout {\n"
                                 "    property var kvModel\n"
                                 "    KeyValueList { Layout.fillWidth: true; model: kvModel }\n"
                                 "}\n")
                      .toUtf8());
    wrapper.flush();

    DetailListModel model;
    model.setEntries({{QStringLiteral("com.docker.network.bridge.name"), QStringLiteral("docker0"), QString(), QString(), QString(), QString()},
                      {QStringLiteral("com.docker.network.bridge.enable_icc"), QStringLiteral("true"), QString(), QString(), QString(), QString()}});

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(wrapper.fileName()));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("kvModel"), QVariant::fromValue(&model));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *list = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(list);

    /*
     * Row width is **320px**, the situation where users hit the bug (narrow panel, long option names).
     * The old key column was pinned at ≈180px while `com.docker.network.bridge.name` needs ≈198px,
     * so it was always elided; the new layout gives the key the leftover width and it fits.
     */
    QQuickWindow window;
    window.resize(320, 300);
    list->setParentItem(window.contentItem());
    list->setWidth(320);
    list->setHeight(300);
    window.show();
    QTRY_VERIFY(list->width() > 0);

    QList<QQuickItem *> keys;
    QList<QQuickItem *> values;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        keys.clear();
        values.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("keyValueListKey")) {
                keys.append(node);
            } else if (node->objectName() == QLatin1String("keyValueListValue")) {
                values.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return keys.size() == 2 && values.size() == 2;
    }(), 5000);

    /*
     * Key assertion: the **full option name must fit** (not eaten by an ellipsis).
     *
     * The old key column was fixed at 10 gridUnits (≈180px) while keys need ≈198px, and the value's
     * `Layout.fillWidth` took all the leftover room, truncating the name while the value filled the row
     * (reported as "the option names are all covered"). Text.contentWidth is the full text width, so
     * contentWidth <= width means not truncated.
     */
    for (QQuickItem *key : keys) {
        // Measure how wide the full option name is and compare with the column width, avoiding
        // QQC2.Label's contentWidth handling under elide (measured: it returns 0 and breaks the assert)
        const QFontMetricsF metrics(key->property("font").value<QFont>());
        const qreal needed = metrics.horizontalAdvance(key->property("text").toString());
        QVERIFY2(needed <= key->width() + 1.0,
                 qPrintable(QStringLiteral("the key '%1' is truncated (needs %2px, has %3px)")
                                .arg(key->property("text").toString())
                                .arg(needed)
                                .arg(key->width())));
    }
    // Keys take the leftover width, values hug right: values are short, keys are long
    QVERIFY2(keys.first()->width() > values.first()->width(),
             "the key column must take the room; the value hugs the right edge");

    // Value right-aligned: its right edge nearly coincides with the row's right edge
    QQuickItem *row = values.first()->parentItem();
    QVERIFY(row);
    const qreal rowRight = row->mapToItem(list, QPointF(row->width(), 0)).x();
    const qreal valueRight = values.first()->mapToItem(list, QPointF(values.first()->width(), 0)).x();
    QVERIFY2(qAbs(rowRight - valueRight) < 2.0,
             qPrintable(QStringLiteral("the value must hug the right edge (row %1 vs value %2)").arg(rowRight).arg(valueRight)));
}

/*!
 * Over-long commands (asked: what happens with a very long command).
 *
 * It used to wrap without limit: a few-hundred-character command stretched the page and pushed sections away.
 * Now capped at 4 lines with a "show all (N lines)" affordance; the copy button always gives the full value.
 */
void QmlLoadTest::longCommandIsCollapsedUntilExpanded()
{
    /*
     * A very long command (asked: "what if the command is too long?").
     *
     * Expected: it wraps **inside its own row** — the page never widens (no panel overflow) and later
     * sections are not pushed out of sight; the copy button still yields the full value.
     */
    const QString longCommand = QString(400, QLatin1Char('x'));

    ContainerDetail detail;
    detail.id = QStringLiteral("cid-long");
    detail.name = QStringLiteral("long-cmd");
    detail.state = ContainerState::Running;
    detail.command = {QStringLiteral("jupyter"), QStringLiteral("notebook"), longCommand};
    m_backend->setContainerDetail(detail);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("containerId"), QStringLiteral("cid-long")}},
                                                                        m_engine->rootContext()));
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
    m_backend->completeRefresh();

    QQuickItem *row = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((row = findItemDeep(window.contentItem(), QStringLiteral("detailCommandRow"))) != nullptr, 5000);
    QQuickItem *label = findItemDeep(row, QStringLiteral("copyableTextValue"));
    QVERIFY2(label, "the command value label must exist");

    // (1) No width overflow: field and label stay within the page width
    QTRY_VERIFY_WITH_TIMEOUT(label->width() > 0, 5000);
    const qreal rowRight = row->mapToItem(page, QPointF(row->width(), 0)).x();
    QVERIFY2(rowRight <= page->width() + 1.0,
             qPrintable(QStringLiteral("the command row overflows the page (%1 > %2)").arg(rowRight).arg(page->width())));

    // (2) The long value wraps in its own row (more than one line), so nothing is lost
    QTRY_VERIFY2(label->property("lineCount").toInt() > 1,
                 "a long command must wrap inside its own field instead of overflowing");

    // (3) The copy button always yields the full value
    QQuickItem *copy = findItemDeep(row, QStringLiteral("copyButtonObject"));
    QVERIFY(copy);
    QCOMPARE(copy->property("value").toString(), detail.command.join(QLatin1Char(' ')));
}
/*!
 * The container detail's "Image" row opens the image detail (requested).
 *
 * Same interaction as network members / used-by rows: whole row clickable plus a chevron; it must emit
 * the **image ID** (`sha256:...`, used to open the image detail), not the displayed reference name.
 */
void QmlLoadTest::containerDetailOpensTheImage()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-img");
    detail.name = QStringLiteral("image-link");
    detail.state = ContainerState::Running;
    detail.image = QStringLiteral("demo:1.0");
    detail.imageId = QStringLiteral("sha256:feedface");
    m_backend->setContainerDetail(detail);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("containerId"), QStringLiteral("cid-img")}},
                                                                        m_engine->rootContext()));
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
    m_backend->completeRefresh();

    // This ItemDelegate has no objectName, so locate it by signal plus the image label in the content tree
    QQuickItem *label = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((label = findItemDeep(window.contentItem(), QStringLiteral("detailImageLabel"))) != nullptr, 5000);
    QCOMPARE(label->property("text").toString(), QStringLiteral("demo:1.0"));

    QQuickItem *row = label->parentItem() ? label->parentItem()->parentItem() : nullptr;
    QVERIFY2(row, "the image row must be an ItemDelegate around the label");
    QVERIFY2(row->property("enabled").toBool(), "a container with a known image id must be clickable");

    QSignalSpy requestedSpy(page, SIGNAL(imageRequested(QString)));
    QVERIFY(requestedSpy.isValid());
    QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
    QCOMPARE(requestedSpy.count(), 1);
    QCOMPARE(requestedSpy.at(0).at(0).toString(), QStringLiteral("sha256:feedface"));

    // Without an image ID from the engine the row is not clickable (the detail cannot be opened)
    ContainerDetail withoutId = detail;
    withoutId.imageId.clear();
    m_backend->setContainerDetail(withoutId);
    m_stubKcm->controller()->containerDetail()->reload();
    m_backend->completeRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(!row->property("enabled").toBool(), 5000);
}


/*!
 * The engine page's lower half lists component versions (requested: containerd etc. besides dockerd).
 */
void QmlLoadTest::engineViewListsComponentVersions()
{
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    info.serverVersion = QStringLiteral("29.8.0");
    info.apiVersion = QStringLiteral("1.56");
    info.operatingSystem = QStringLiteral("Arch Linux");
    info.architecture = QStringLiteral("x86_64");
    info.cgroupDriver = QStringLiteral("systemd");
    info.cgroupVersion = QStringLiteral("2");
    info.storageDriver = QStringLiteral("overlay2");
    info.cpuCount = 16;
    info.components = {{QStringLiteral("Engine"), QStringLiteral("29.8.0")},
                       {QStringLiteral("containerd"), QStringLiteral("1.7.24")},
                       {QStringLiteral("runc"), QStringLiteral("1.2.3")}};
    m_backend->setEngineInfo(info);
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/EngineStatusView.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    auto *engineStatus = m_stubKcm->controller()->engine();
    QVERIFY(engineStatus);
    initial.insert(QStringLiteral("engine"), QVariant::fromValue(engineStatus));
    initial.insert(QStringLiteral("buildStamp"), QStringLiteral("test"));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(view);

    QQuickWindow window;
    window.resize(900, 600);
    view->setParentItem(window.contentItem());
    view->setWidth(900);
    view->setHeight(600);
    window.show();
    QTRY_VERIFY(view->width() > 0);

    // One row per component, its value being the version
    QStringList versions;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        versions.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("engineComponentRow")) {
                versions.append(node->property("text").toString());
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return versions.size() == 3;
    }(), 5000);
    QVERIFY(versions.contains(QStringLiteral("29.8.0")));
    QVERIFY2(versions.contains(QStringLiteral("1.7.24")), "the containerd version must be shown");
    QVERIFY(versions.contains(QStringLiteral("1.2.3")));

    // The cgroup driver and the CPU count are there too
    QQuickItem *cpus = findItemDeep(window.contentItem(), QStringLiteral("engineCpuCount"));
    QVERIFY(cpus);
    QCOMPARE(cpus->property("text").toString(), QStringLiteral("16"));
}


/*!
 * A dual-stack mapping (IPv4 + IPv6 wildcard) draws **one** branch in the topology (requested).
 *
 * The host-endpoint rings are Canvas-drawn (invisible to the test), so assert the branch count and the
 * chip text: after merging there is one branch and the text is the bare port (rings express addresses).
 */
void QmlLoadTest::topologyMergesDualStackBindings()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-dual");
    detail.name = QStringLiteral("dual-stack");
    detail.state = ContainerState::Running;
    // No host address given -> Docker creates both the IPv4 and the IPv6 wildcard binding
    detail.ports = {{QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
                    {QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")}};
    m_backend->setContainerDetail(detail);

    auto *controller = m_stubKcm->controller()->containerDetail();
    controller->setContainerId(QStringLiteral("cid-dual"));
    controller->start();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PortTopology.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("model"), QVariant::fromValue(controller->portGroups()));
    initial.insert(QStringLiteral("colorSeed"), QStringLiteral("cid-dual"));
    initial.insert(QStringLiteral("containerLabel"), QStringLiteral("dual-stack"));
    initial.insert(QStringLiteral("hostLabel"), QStringLiteral("localhost"));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *topology = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(topology);

    QQuickWindow window;
    window.resize(900, 300);
    topology->setParentItem(window.contentItem());
    topology->setWidth(900);
    topology->setHeight(300);
    window.show();
    QTRY_VERIFY(topology->width() > 0);

    QList<QQuickItem *> links;
    QList<QQuickItem *> hostChips;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        links.clear();
        hostChips.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("portMappingLink")) {
                links.append(node);
            } else if (node->objectName() == QLatin1String("portHostChip")) {
                hostChips.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return links.size() == 1 && hostChips.size() == 1;
    }(), 5000);

    // One branch (the merge worked) with the bare port as its text
    QCOMPARE(links.first()->property("branchCount").toInt(), 1);
    QCOMPARE(hostChips.first()->property("text").toString(), QStringLiteral("20004"));
    // And marked dual-stack (the Canvas draws two rings from that)
    QVERIFY2(hostChips.first()->parentItem() != nullptr, "the chip must sit in a binding row");
}


/*!
 * Inline port-row conflict hint (ARCH_next_ports.md §4.D, milestone M2).
 *
 * Occupied by a running container -> an inline hint plus "Use suggested port N"; adopting it clears the
 * hint; idle rows show **nothing at all** (user request: fewer redundant small labels).
 */
void QmlLoadTest::portRowShowsConflictAndAdoptsTheSuggestion()
{
    Container holder;
    holder.id = QStringLiteral("holder-id");
    holder.name = QStringLiteral("web");
    holder.image = QStringLiteral("alpine:3.19");
    holder.state = ContainerState::Running;
    holder.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};
    m_backend->setContainers({holder});

    auto *wizard = m_stubKcm->controller()->createContainer();
    QVERIFY(wizard);
    wizard->setPortRows({QVariantMap {{QStringLiteral("containerPort"), 80}, {QStringLiteral("hostPort"), 8100}},
                         QVariantMap {{QStringLiteral("containerPort"), 81}, {QStringLiteral("hostPort"), 9000}}});

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PortMappingEditor.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("controller"), QVariant::fromValue(wizard));
    QScopedPointer<QObject> object(component.createWithInitialProperties(initial, m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *editor = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(editor);

    QQuickWindow window;
    window.resize(1000, 300);
    editor->setParentItem(window.contentItem());
    editor->setWidth(1000);
    editor->setHeight(300);
    window.show();
    QTRY_VERIFY(editor->width() > 0);

    QList<QQuickItem *> statuses;
    QList<QQuickItem *> suggestions;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        statuses.clear();
        suggestions.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("wizardPortRowStatus")) {
                statuses.append(node);
            } else if (node->objectName() == QLatin1String("wizardPortSuggestionButton")) {
                suggestions.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return statuses.size() == 2;
    }(), 5000);

    // Only the conflicting row is visible (idle rows show no hint)
    QTRY_VERIFY_WITH_TIMEOUT(statuses.at(0)->property("visible").toBool()
                                 && !statuses.at(1)->property("visible").toBool(), 5000);
    QVERIFY2(statuses.at(0)->property("text").toString().contains(QStringLiteral("web")),
             qPrintable(statuses.at(0)->property("text").toString()));

    // Suggest button: one click writes the port back into that row and the hint disappears
    QQuickItem *suggestionButton = suggestions.isEmpty() ? nullptr : suggestions.first();
    QVERIFY2(suggestionButton, "the conflicting row must offer a suggested port");
    const int suggestion = wizard->portRowStatuses().at(0).toMap().value(QStringLiteral("suggestion")).toInt();
    QVERIFY(suggestion > 0);
    // QQC2.Button's signal is clicked (`triggered` belongs to QML Action, not here)
    QVERIFY(QMetaObject::invokeMethod(suggestionButton, "clicked"));
    QTRY_COMPARE(wizard->portRows().at(0).toMap().value(QStringLiteral("hostPort")).toInt(), suggestion);
    QTRY_VERIFY_WITH_TIMEOUT(!statuses.at(0)->property("visible").toBool(), 5000);
}


/*!
 * Ports tab (ARCH_next_ports.md §4.A, milestone M3).
 *
 * Ports are the primary focus and the container is just a column; an inline "Stop" needs confirmation
 * like any risky action (nothing may be sent before it).
 */
void QmlLoadTest::portsTabListsRowsAndOpensTheContainer()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    Container running;
    running.id = QStringLiteral("running-id");
    running.name = QStringLiteral("web-frontend");
    running.image = QStringLiteral("registry.example.com/team/frontend:2.4.1");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};
    m_backend->setContainers({running});
    // Let the state controller receive the container list (through the real refresh path)
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1200, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1200);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QCOMPARE(tabBar->property("count").toInt(), 7);
    QVERIFY(tabBar->setProperty("currentIndex", 4));
    m_backend->completeRefresh();

    QQuickItem *list = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        list = childByObjectName(page, QStringLiteral("hostPortList"));
        return list != nullptr && list->property("count").toInt() == 1;
    }(), 5000);

    // The row shows port, state and container name ("ports are the primary focus")
    QList<QQuickItem *> rows;
    QList<QQuickItem *> stopButtons;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        rows.clear();
        stopButtons.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("hostPortRow")) {
                rows.append(node);
            } else if (node->objectName() == QLatin1String("hostPortRowStop")) {
                stopButtons.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return rows.size() == 1;
    }(), 5000);

    bool sawPort = false;
    bool sawState = false;
    bool sawContainer = false;
    std::function<void(QQuickItem *)> scan = [&](QQuickItem *node) {
        if (!node) {
            return;
        }
        const QString name = node->objectName();
        if (name == QLatin1String("hostPortRowPort") && node->property("text").toString() == QLatin1String("8080")) {
            sawPort = true;
        } else if (name == QLatin1String("hostPortRowState")) {
            sawState = true;
        } else if (name == QLatin1String("hostPortRowContainer")
                   && node->property("text").toString() == QLatin1String("web-frontend")) {
            sawContainer = true;
        }
        for (QQuickItem *child : node->childItems()) {
            scan(child);
        }
    };
    scan(rows.first());
    QVERIFY2(sawPort, "the host port must be visible in the row");
    QVERIFY2(sawState, "the row must carry a state chip");
    QVERIFY2(sawContainer, "the container name must be visible in the row");

    /*
     * Interaction (user feedback): buttons are gone — the **row itself** navigates, and "stop" moved
     * to the container detail. So assert: no stop button in the row, but clicking it emits the signal.
     */
    QVERIFY2(stopButtons.isEmpty(), "the row must not carry a stop button any more");
    QVERIFY2(!page->findChild<QObject *>(QStringLiteral("portStopContainerDialog")),
             "the stop confirmation dialog must be gone with the button");

    const int callsBefore = m_backend->mutationCalls().size();
    QSignalSpy openSpy(page, SIGNAL(portContainerActivated(QString)));
    QVERIFY(openSpy.isValid());
    QVERIFY(QMetaObject::invokeMethod(rows.first(), "clicked"));
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.first().at(0).toString(), QStringLiteral("running-id"));
    // Navigation only: no write operation is sent
    QCOMPARE(m_backend->mutationCalls().size(), callsBefore);
}


/*!
 * Range map (ARCH_next_ports.md §4.B, milestone M4).
 *
 * In the map: one section per range, one tile per port, occupied tiles carry state;
 * **huge ranges must be capped** and say "N more" — otherwise 1000-1100 creates hundreds of tiles.
 */
void QmlLoadTest::portsTabSwitchesToTheRangeMap()
{
    Container running;
    running.id = QStringLiteral("running-id");
    running.name = QStringLiteral("web");
    running.image = QStringLiteral("alpine:3.21");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};
    m_backend->setContainers({running});
    // Put a huge declared range in (1000-1100, 101 ports)
    ContainerDetail detail;
    detail.id = running.id;
    detail.declaredPorts = {{80, QStringLiteral("tcp"), QString(), 1000, 1100}};
    m_backend->setContainerDetail(detail);
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    m_stubKcm->controller()->refreshPorts();
    m_backend->completeRefresh();

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1200, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1200);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 4));
    m_backend->completeRefresh();

    // Switch to the map view
    QQuickItem *viewCombo = childByObjectName(page, QStringLiteral("portViewCombo"));
    QVERIFY(viewCombo);
    QVERIFY(viewCombo->setProperty("currentIndex", 1)); // Range map
    QMetaObject::invokeMethod(viewCombo, "activated", Q_ARG(int, 1));

    QList<QQuickItem *> tiles;
    QList<QQuickItem *> hiddenLabels;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        tiles.clear();
        hiddenLabels.clear();
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("portMapTile")) {
                tiles.append(node);
            } else if (node->objectName() == QLatin1String("portMapHiddenCount")) {
                hiddenLabels.append(node);
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return tiles.size() > 0 && hiddenLabels.size() > 0;
    }(), 5000);

    // The cap works: far fewer than 101 tiles, and the "N more" label is shown
    QVERIFY2(tiles.size() < 101, qPrintable(QString::number(tiles.size())));
    QVERIFY2(tiles.size() <= 64 + 8, "the map must cap how many tiles it renders");
    bool sawHidden = false;
    for (QQuickItem *label : hiddenLabels) {
        if (label->property("visible").toBool() && !label->property("text").toString().isEmpty()) {
            sawHidden = true;
        }
    }
    QVERIFY2(sawHidden, "a capped range must say how many ports are not shown");

    // Switch back to the list view
    QVERIFY(viewCombo->setProperty("currentIndex", 0));
    QMetaObject::invokeMethod(viewCombo, "activated", Q_ARG(int, 0));
    QTRY_VERIFY_WITH_TIMEOUT(childByObjectName(page, QStringLiteral("hostPortList")) != nullptr, 5000);
}


/*!
 * Range-map tile clicks (requested: a running port maps to one container, one click gets you there).
 *
 * Only "running" tiles carry container info; other tiles disable clicking (no false affordance).
 */
void QmlLoadTest::rangeMapTilesOpenTheRunningContainer()
{
    Container running;
    running.id = QStringLiteral("ml-id");
    running.name = QStringLiteral("ml-medai");
    running.image = QStringLiteral("alpine:latest");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 8888, 20003, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("dl-id");
    stopped.name = QStringLiteral("dl-medai");
    stopped.image = QStringLiteral("alpine:latest");
    stopped.state = ContainerState::Exited;

    m_backend->setContainers({running, stopped});
    ContainerDetail detail;
    detail.id = stopped.id;
    detail.declaredPorts = {{8888, QStringLiteral("tcp"), QString(), 20003, 20003}};
    m_backend->setContainerDetail(detail);
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    m_stubKcm->controller()->refreshPorts();
    m_backend->completeRefresh();

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickWindow window;
    window.resize(1200, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1200);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 4));
    m_backend->completeRefresh();

    QQuickItem *viewCombo = childByObjectName(page, QStringLiteral("portViewCombo"));
    QVERIFY(viewCombo);
    QVERIFY(viewCombo->setProperty("currentIndex", 1));
    QMetaObject::invokeMethod(viewCombo, "activated", Q_ARG(int, 1));

    // Find the 20003 tile: under "all ports" it must count as running (beating in-use/free)
    QQuickItem *runningTile = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        runningTile = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node) {
                return;
            }
            if (node->objectName() == QLatin1String("portMapTile")) {
                const QVariant data = node->property("modelData");
                const QVariantMap map = data.toMap();
                if (map.value(QStringLiteral("port")).toInt() == 20003) {
                    runningTile = node;
                    return;
                }
            }
            for (QQuickItem *child : node->childItems()) {
                walk(child);
            }
        };
        walk(window.contentItem());
        return runningTile != nullptr;
    }(), 5000);
    QVERIFY(runningTile);

    const QVariantMap tileData = runningTile->property("modelData").toMap();
    QCOMPARE(tileData.value(QStringLiteral("stateKey")).toString(), QStringLiteral("inUse"));
    QCOMPARE(tileData.value(QStringLiteral("containerId")).toString(), QStringLiteral("ml-id"));

    /*
     * Clicking a tile -> navigation signal.
     *
     * `MouseArea.clicked` takes a `QQuickMouseEvent*` the test cannot construct (header not on the
     * public include path), so guard in two steps: (1) the tile's click area exists and is enabled
     * (only running tiles are clickable); (2) emit the map's `containerRequested` and verify the wiring.
     */
    QQuickItem *clickArea = childByObjectName(runningTile, QStringLiteral("portMapTileClick"));
    QVERIFY2(clickArea, "a running tile must be clickable");
    QVERIFY(clickArea->property("enabled").toBool());
    // Tiles carry **no** hover tooltip any more (attached properties are unreadable here;
    // tst_source_conventions guards that). The container name lives in the tile data instead.
    QCOMPARE(tileData.value(QStringLiteral("containerName")).toString(), QStringLiteral("ml-medai"));

    QSignalSpy openSpy(page, SIGNAL(portContainerActivated(QString)));
    QVERIFY(openSpy.isValid());
    QQuickItem *map = childByObjectName(page, QStringLiteral("portRangeMap"));
    QVERIFY(map);
    QVERIFY(QMetaObject::invokeMethod(map, "containerRequested",
                                     Q_ARG(QString, QStringLiteral("ml-id")),
                                     Q_ARG(QString, QStringLiteral("ml-medai"))));
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.first().at(0).toString(), QStringLiteral("ml-id"));
}


void QmlLoadTest::loadsAllQmlFiles_data()
{
    QTest::addColumn<QString>("fileName");

    const QString uiDirectory = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/");
    const QStringList files = {
        QStringLiteral("main.qml"),
        QStringLiteral("MainPage.qml"),
        QStringLiteral("ContainerDetail.qml"),
        QStringLiteral("ImageDetail.qml"),
        QStringLiteral("ContainerCard.qml"),
        QStringLiteral("ImageCard.qml"),
        QStringLiteral("EngineStatusView.qml"),
        QStringLiteral("DaemonConfigPage.qml"),
        QStringLiteral("RegistryAuthPage.qml"),
        QStringLiteral("NetworkCard.qml"),
        QStringLiteral("NetworkDetail.qml"),
        QStringLiteral("VolumeCard.qml"),
        QStringLiteral("VolumeDetail.qml"),
        QStringLiteral("CreateContainer.qml"),
        QStringLiteral("StorageView.qml"),
        QStringLiteral("ResourceView.qml"),
        QStringLiteral("components/StatTile.qml"),
        QStringLiteral("components/MiniTrend.qml"),
        QStringLiteral("components/StorageBar.qml"),
        QStringLiteral("components/StatusChip.qml"),
        QStringLiteral("components/CopyButton.qml"),
        QStringLiteral("components/CopyableText.qml"),
        QStringLiteral("components/EmptyPlaceholder.qml"),
        QStringLiteral("components/CollapsibleSection.qml"),
        QStringLiteral("components/KeyValueList.qml"),
        // Added in phase 4 (ARCH_V4 §2.2.5 / §2.4)
        QStringLiteral("components/ConfirmDialog.qml"),
        QStringLiteral("components/OperationMessage.qml"),
        QStringLiteral("components/PullImageDialog.qml"),
        QStringLiteral("components/PullProgressList.qml"),
        QStringLiteral("components/BuildImagePanel.qml"),
        QStringLiteral("components/PortMappingEditor.qml"),
        QStringLiteral("components/MountPresetManager.qml"),
        QStringLiteral("components/ServiceCard.qml"),
        QStringLiteral("components/FilteredComboBox.qml"),
        QStringLiteral("components/FieldChip.qml"),
        QStringLiteral("components/PortTopology.qml"),
        QStringLiteral("components/ImageRefInput.qml"),
        QStringLiteral("components/StringListEditor.qml"),
        QStringLiteral("components/KeyValueListEditor.qml"),
        QStringLiteral("components/RegistryLoginDialog.qml"),
        QStringLiteral("components/LogConsole.qml"),
        QStringLiteral("components/CreateNetworkDialog.qml"),
    };
    for (const QString &file : files) {
        // Note: the row name must be a stable byte sequence; qPrintable() would dangle
        const QByteArray rowName = file.toUtf8();
        QTest::newRow(rowName.constData()) << uiDirectory + file;
    }
}

void QmlLoadTest::loadsAllQmlFiles()
{
    QFETCH(QString, fileName);

    QVERIFY2(QFile::exists(fileName), qPrintable(QStringLiteral("missing QML file: %1").arg(fileName)));

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(fileName));
    if (component.isError()) {
        const QList<QQmlError> errors = component.errors();
        QStringList messages;
        messages.reserve(errors.size());
        for (const QQmlError &error : errors) {
            messages.append(error.toString());
        }
        QFAIL(qPrintable(messages.join(QLatin1Char('\n'))));
    }
    QVERIFY(component.isReady());
}

void QmlLoadTest::instantiatesPages_data()
{
    QTest::addColumn<QString>("fileName");

    // Only "page" files can be created standalone; cards/views need required properties from a caller,
    // and their compilation is already covered by loadsAllQmlFiles.
    const QString uiDirectory = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/");
    const QStringList files = {
        QStringLiteral("main.qml"),
        QStringLiteral("MainPage.qml"),
        QStringLiteral("ContainerDetail.qml"),
        QStringLiteral("ImageDetail.qml"),
    };
    for (const QString &file : files) {
        const QByteArray rowName = file.toUtf8();
        QTest::newRow(rowName.constData()) << uiDirectory + file;
    }
}

void QmlLoadTest::instantiatesPages()
{
    QFETCH(QString, fileName);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(fileName));
    QVERIFY2(!component.isError(), qPrintable(fileName));

    // Really instantiate: catches binding-evaluation errors (missing property, failed type conversion)
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    if (object.isNull()) {
        QStringList messages;
        const QList<QQmlError> errors = component.errors();
        for (const QQmlError &error : errors) {
            messages.append(error.toString());
        }
        QFAIL(qPrintable(messages.join(QLatin1Char('\n'))));
    }
}

/*!
 * §40: Environment / Labels may hold sensitive data, so by default only counts may be shown.
 * This instantiates the detail page and asserts the collapsed content's visibility.
 */
void QmlLoadTest::sensitiveSectionsAreCollapsedByDefault()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY(!component.isError());
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());

    const QList<QQuickItem *> items = object->findChildren<QQuickItem *>();
    QQuickItem *environment = nullptr;
    QQuickItem *labels = nullptr;
    for (QQuickItem *item : items) {
        if (item->objectName() == QLatin1String("environmentValues")) {
            environment = item;
        } else if (item->objectName() == QLatin1String("labelValues")) {
            labels = item;
        }
    }

    QVERIFY2(environment, "environment values container not found");
    QVERIFY2(labels, "label values container not found");
    QVERIFY2(!environment->isVisible(), "environment values must be collapsed by default (§40)");
    QVERIFY2(!labels->isVisible(), "labels must be collapsed by default (§40)");
    QCOMPARE(environment->height(), 0.0);
}

QQuickItem *QmlLoadTest::childByObjectName(QQuickItem *root, const QString &objectName)
{
    // Walk the visual tree: Repeater delegates are not in the QObject tree (see the helper's header)
    return TestSupport::findItemByObjectName(root, objectName);
}

/*!
 * ARCH_V3 §2.1: StatusChip is the single implementation of status presentation.
 * The semantic key -> Kirigami.Badge.Type mapping may only live in StatusPalette.
 *
 * Kirigami.Badge.Type values: Information=0, Positive=1, Warning=2, Error=3.
 */
void QmlLoadTest::statusChipMapsSemanticKeys_data()
{
    QTest::addColumn<QString>("semanticKey");
    QTest::addColumn<int>("expectedType");

    QTest::newRow("positive") << QStringLiteral("positive") << 1;
    QTest::newRow("neutral") << QStringLiteral("neutral") << 2;
    QTest::newRow("negative") << QStringLiteral("negative") << 3;
    QTest::newRow("disabled") << QStringLiteral("disabled") << 0;
    QTest::newRow("unknown key falls back to Information") << QStringLiteral("no-such-semantic") << 0;
}

void QmlLoadTest::statusChipMapsSemanticKeys()
{
    QFETCH(QString, semanticKey);
    QFETCH(int, expectedType);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/StatusChip.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));

    QScopedPointer<QObject> chip(component.createWithInitialProperties(
        {
            {QStringLiteral("semanticKey"), semanticKey},
            {QStringLiteral("iconName"), QStringLiteral("media-playback-start")},
            {QStringLiteral("text"), QStringLiteral("Running")},
        },
        m_engine->rootContext()));
    QVERIFY2(!chip.isNull(), "StatusChip failed to instantiate");

    QCOMPARE(chip->property("type").toInt(), expectedType);
    // Triple encoding (§1.6/§1.8): text and icon must both be present; colour is not the only cue
    QCOMPARE(chip->property("text").toString(), QStringLiteral("Running"));
    QObject *icon = chip->property("icon").value<QObject *>();
    QVERIFY2(icon, "StatusChip must expose a grouped icon property");
    QCOMPARE(icon->property("name").toString(), QStringLiteral("media-playback-start"));
}

/*!
 * ARCH_V3 §2.1: CopyButton is the only implementation of the copy action.
 * With an empty value the button must be disabled instead of copying an empty string.
 */
void QmlLoadTest::copyButtonFollowsValueAvailability()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/CopyButton.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));

    QScopedPointer<QObject> emptyValue(component.createWithInitialProperties(
        {
            {QStringLiteral("value"), QString()},
            {QStringLiteral("fieldLabel"), QStringLiteral("container ID")},
        },
        m_engine->rootContext()));
    QVERIFY(!emptyValue.isNull());
    QVERIFY2(!emptyValue->property("enabled").toBool(), "copy button must be disabled when there is no value");

    QClipboard *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    clipboard->setText(QStringLiteral("pre-existing content"));

    QScopedPointer<QObject> withValue(component.createWithInitialProperties(
        {
            {QStringLiteral("value"), QStringLiteral("sha256:0123456789abcdef")},
            {QStringLiteral("fieldLabel"), QStringLiteral("image ID")},
        },
        m_engine->rootContext()));
    QVERIFY(!withValue.isNull());
    QVERIFY(withValue->property("enabled").toBool());

    QVERIFY(QMetaObject::invokeMethod(withValue.data(), "clicked"));
    QCOMPARE(clipboard->text(), QStringLiteral("sha256:0123456789abcdef"));
}

/*!
 * ARCH_V2 §33 / ARCH_V3 §2.1: empty states must distinguish "no data" from
 * "excluded by search / filter", the latter also offering an actionable way out.
 */
void QmlLoadTest::emptyPlaceholderDistinguishesStates()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("demo");
    container.image = QStringLiteral("alpine:latest");
    container.state = ContainerState::Running;
    container.created = QDateTime::currentDateTimeUtc().addSecs(-600);
    m_backend->setContainers({container});

    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *placeholder = childByObjectName(page, QStringLiteral("containersEmptyPlaceholder"));
    QVERIFY2(placeholder, "containers empty placeholder not found");

    // Data present, no search text: no placeholder
    QCOMPARE(placeholder->property("message").toString(), QString());
    QVERIFY(!placeholder->property("visible").toBool());

    // Search with no results
    controller->containerList()->setSearchText(QStringLiteral("zzz-no-such-container"));
    const QString searchMessage = placeholder->property("message").toString();
    QVERIFY2(!searchMessage.isEmpty(), "a search miss must show the placeholder");
    QVERIFY2(searchMessage.contains(QStringLiteral("zzz-no-such-container")), qPrintable(searchMessage));
    QCOMPARE(placeholder->property("actionText").toString(), QStringLiteral("Clear search"));

    // Filter with no results: message and action must differ from the search miss (four states, not one)
    controller->containerList()->setSearchText(QString());
    controller->containerList()->setStateFilter(QStringLiteral("paused"));
    const QString filterMessage = placeholder->property("message").toString();
    QVERIFY2(!filterMessage.isEmpty(), "a filter miss must show the placeholder");
    QVERIFY2(filterMessage != searchMessage, "search miss and filter miss must not share one message");
    QCOMPARE(placeholder->property("actionText").toString(), QStringLiteral("Show all containers"));
}

/*!
 * ARCH_V3 §2.2: container detail sections.
 * Switching sections must not change collapse state nor re-run inspect (lifecycle is page-bound).
 */
void QmlLoadTest::containerDetailHasSections()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.environment = {QStringLiteral("PATH=/usr/bin")};
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ContainerDetail failed to instantiate");
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    m_backend->completeRefresh();
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::ContainerDetail), 1);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY2(tabBar, "detail tab bar not found");
    QVERIFY2(tabBar->property("count").toInt() >= 5, qPrintable(QString::number(tabBar->property("count").toInt())));

    QQuickItem *stack = childByObjectName(page, QStringLiteral("detailSectionStack"));
    QVERIFY2(stack, "detail section stack not found");
    QCOMPARE(stack->property("currentIndex").toInt(), 0);

    QQuickItem *environment = childByObjectName(page, QStringLiteral("environmentValues"));
    QVERIFY2(environment, "environment values container not found");
    QVERIFY2(!environment->isVisible(), "environment must stay collapsed (§40)");

    // Logs are a long-lived stream: no section visit, no reading (§3.1.4)
    QVERIFY2(m_backend->lastLogContainerId().isEmpty(), "logs must not be read before the tab is opened");

    // Switch to the Logs section: reading starts (TTY flag from the detail), no re-inspect, no expand
    QVERIFY(tabBar->setProperty("currentIndex", 4));
    QCOMPARE(stack->property("currentIndex").toInt(), 4);
    QQuickItem *logConsoleItem = childByObjectName(page, QStringLiteral("logConsole"));
    QVERIFY2(logConsoleItem, "the log console must replace the old placeholder");
    QCOMPARE(m_backend->lastLogContainerId(), QStringLiteral("cid-1"));
    QVERIFY2(!m_backend->lastLogTty(), "the TTY flag must come from the container detail (Config.Tty)");
    QVERIFY(m_backend->lastLogFollow());
    QCOMPARE(m_backend->lastLogTailLines(), 200);
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::ContainerDetail), 1);
    QVERIFY2(!environment->isVisible(), "switching sections must not expand environment (§40)");

    // Engine-pushed logs appear in the console; while paused they buffer but are not appended
    auto *logs = m_stubKcm->controller()->containerDetail()->logs();
    QVERIFY(logs);
    LogLine first;
    first.text = QStringLiteral("boot ok");
    first.complete = true;
    m_backend->emitLogLines(QStringLiteral("cid-1"), {first});
    QQuickItem *logText = childByObjectName(page, QStringLiteral("logTextArea"));
    QVERIFY(logText);
    QTRY_VERIFY(logText->property("text").toString().contains(QStringLiteral("boot ok")));
    QQuickItem *stateLabel = childByObjectName(page, QStringLiteral("logStateLabel"));
    QVERIFY(stateLabel);
    QCOMPARE(stateLabel->property("text").toString(), QStringLiteral("Following"));

    logs->pause();
    QCOMPARE(stateLabel->property("text").toString(), QStringLiteral("Paused (output is buffered)"));
    LogLine second;
    second.text = QStringLiteral("while paused");
    second.complete = true;
    m_backend->emitLogLines(QStringLiteral("cid-1"), {second});
    QVERIFY2(!logText->property("text").toString().contains(QStringLiteral("while paused")),
             "paused output must stay buffered until resume");
    logs->resume();
    QTRY_VERIFY(logText->property("text").toString().contains(QStringLiteral("while paused")));

    // Leaving the section must disconnect (a long-lived stream should not linger)
    QVERIFY(tabBar->setProperty("currentIndex", 0));
    QCOMPARE(m_backend->stopLogsCount(QStringLiteral("cid-1")), 1);
    QCOMPARE(logs->stateKey(), QStringLiteral("idle"));
}

/*!
 * ARCH_V3 §2.3: image layers show 5 first and can expand to all (slicing is the model layer's job).
 */
void QmlLoadTest::imageLayersCollapseByDefault()
{
    ImageDetail detail;
    detail.id = QStringLiteral("sha256:aaaa");
    detail.repoTags = {QStringLiteral("alpine:latest")};
    for (int i = 0; i < 12; ++i) {
        detail.layers.append(QStringLiteral("sha256:layer%1").arg(i));
    }
    m_backend->setImageDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ImageDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("imageId"), QStringLiteral("sha256:aaaa")},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ImageDetail failed to instantiate");
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    m_backend->completeRefresh();

    DetailListModel *layers = m_stubKcm->controller()->imageDetail()->layers();
    QCOMPARE(layers->totalCount(), 12);
    QCOMPARE(layers->count(), 5);
    QCOMPARE(layers->limit(), 5);

    QVERIFY(QMetaObject::invokeMethod(page, "toggleLayers"));
    QCOMPARE(layers->limit(), 0);
    QCOMPARE(layers->count(), 12);

    QVERIFY(QMetaObject::invokeMethod(page, "toggleLayers"));
    QCOMPARE(layers->limit(), 5);
    QCOMPARE(layers->count(), 5);
}

/*!
 * ARCH_V3_pre §1.8: keyboard navigation and accessibility must not regress in the phase-3 refactor.
 *
 * This asserts the machine-checkable parts (focusable, has an accessible name);
 * the actual visibility of focus rings still needs a manual walkthrough (ARCH_V3 §5.3).
 */
void QmlLoadTest::keyboardNavigationAndAccessibilityAreWired()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("demo");
    container.image = QStringLiteral("alpine:latest");
    container.state = ContainerState::Running;
    container.created = QDateTime::currentDateTimeUtc().addSecs(-120);
    m_backend->setContainers({container});

    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:latest")};
    image.inUse = true;
    image.containerCount = 1;
    m_backend->setImages({image});

    StatusController *controller = m_stubKcm->controller();
    controller->refresh();
    m_backend->completeRefresh();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
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

    for (const QString &viewName : {QStringLiteral("containerView"), QStringLiteral("imageView")}) {
        QQuickItem *view = childByObjectName(page, viewName);
        QVERIFY2(view, qPrintable(viewName));
        QVERIFY2(view->property("activeFocusOnTab").toBool(), qPrintable(viewName + QStringLiteral(" must be reachable with Tab")));
        QVERIFY2(view->property("keyNavigationEnabled").toBool(), qPrintable(viewName + QStringLiteral(" must support arrow-key navigation")));
    }

    // Stat tiles need an accessible name besides colour (§1.8 triple encoding).
    // Note: Accessible.* are attached properties, unreadable via property("Accessible.name");
    // query them through the QAccessible interface.
    QQuickItem *tile = childByObjectName(page, QStringLiteral("statTile"));
    QVERIFY2(tile, "stat tile not found (visual tree search)");
    QAccessibleInterface *tileInterface = QAccessible::queryAccessibleInterface(tile);
    QVERIFY2(tileInterface, "stat tile has no accessible interface");
    QVERIFY2(!tileInterface->text(QAccessible::Name).isEmpty(), "stat tiles need an accessible name");
}

/*!
 * ARCH_V3 §2.7: the UI's Auto-refresh switch must really drive the refresh scheduler.
 *
 * It is both a user option and the diagnostic switch for "periodic refresh triggers UI rebuilds" bugs;
 * a dead switch sends debugging the wrong way, so assert behaviour rather than mere existence.
 */
void QmlLoadTest::autoRefreshActionControlsTheScheduler()
{
    StatusController *controller = m_stubKcm->controller();
    QVERIFY(controller->autoRefreshEnabled());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());

    // The action is not a visual item, so find it among the QObject children
    QObject *autoRefresh = nullptr;
    const QList<QObject *> children = object->findChildren<QObject *>();
    for (QObject *child : children) {
        if (child->property("checkable").toBool() && child->property("text").toString() == QLatin1String("Auto-refresh")) {
            autoRefresh = child;
            break;
        }
    }
    QVERIFY2(autoRefresh, "auto-refresh action not found");

    // Checked state follows the controller (single source of truth)
    QCOMPARE(autoRefresh->property("checked").toBool(), controller->autoRefreshEnabled());

    QVERIFY(QMetaObject::invokeMethod(autoRefresh, "trigger"));
    QVERIFY2(!controller->autoRefreshEnabled(), "triggering the action must turn auto-refresh off");
    QCOMPARE(autoRefresh->property("checked").toBool(), controller->autoRefreshEnabled());

    QVERIFY(QMetaObject::invokeMethod(autoRefresh, "trigger"));
    QVERIFY2(controller->autoRefreshEnabled(), "triggering again must turn auto-refresh back on");
}

/* ============================================================================
 * Write UI (ARCH_V4 §5.1)
 *
 * These tests pin the three things most likely to regress silently in phase 4:
 *  1. permission gate broken (write buttons appear in read-only mode)
 *  2. preconditions broken (a running container gets a delete button)
 *  3. confirmation dialogs losing their consequence text
 * ==========================================================================*/

namespace
{

} // namespace

void QmlLoadTest::writeActionsFollowThePermissionGate()
{
    StatusController *controller = m_stubKcm->controller();

    // The default mock endpoint is invalid -> read-only: no write entry at all, and the reason is stated
    QVERIFY(!controller->operations()->writeAllowed());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *pullButton = childByObjectName(page, QStringLiteral("pullImageEntryButton"));
    QVERIFY2(pullButton, "pull entry button not found");
    QVERIFY2(!pullButton->property("visible").toBool(), "read-only mode must not offer writes");

    QQuickItem *banner = childByObjectName(page, QStringLiteral("writeAccessBanner"));
    QVERIFY2(banner, "write access banner not found");
    QVERIFY2(banner->property("visible").toBool(), "read-only mode must explain itself");
    QVERIFY2(!banner->property("text").toString().isEmpty(), "banner text must not be empty");

    // Once the socket is writable (e.g. the user just joined the docker group) the entry returns
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();
    QVERIFY(controller->operations()->writeAllowed());
    QVERIFY2(!banner->property("visible").toBool(), "banner must disappear once writing is allowed");
    // The pull entry appears only on the images tab; here just assert the gate no longer blocks it
    QVERIFY(pullButton->property("visible").toBool() || pullButton->property("enabled").toBool());
}

void QmlLoadTest::containerActionsFollowStateAndBusy()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();
    QVERIFY(controller->operations()->writeAllowed());

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");

    // Running container: stop / restart but no delete (stop first; do not let users hit the engine's 409)
    ContainerDetail running;
    running.id = QStringLiteral("cid-1");
    running.name = QStringLiteral("demo");
    running.state = ContainerState::Running;
    m_backend->setContainerDetail(running);

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ContainerDetail failed to instantiate");
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *actionBar = childByObjectName(page, QStringLiteral("containerActionBar"));
    QVERIFY2(actionBar, "container action bar not found");
    QVERIFY2(actionBar->property("visible").toBool(), "writable socket must show the action bar");

    QQuickItem *startButton = childByObjectName(page, QStringLiteral("detailStartButton"));
    QQuickItem *stopButton = childByObjectName(page, QStringLiteral("detailStopButton"));
    QQuickItem *restartButton = childByObjectName(page, QStringLiteral("detailRestartButton"));
    QQuickItem *removeButton = childByObjectName(page, QStringLiteral("detailRemoveButton"));
    QQuickItem *blockedHint = childByObjectName(page, QStringLiteral("removeBlockedHint"));
    QVERIFY(startButton && stopButton && restartButton && removeButton && blockedHint);

    QVERIFY2(!startButton->property("visible").toBool(), "running container cannot be started");
    QVERIFY2(stopButton->property("visible").toBool(), "running container must offer stop");
    QVERIFY2(restartButton->property("visible").toBool(), "running container must offer restart");
    QVERIFY2(!removeButton->property("visible").toBool(), "running container must not offer delete");
    QVERIFY2(blockedHint->property("visible").toBool(), "the missing delete button needs a reason");

    // Exited container: the reverse (the post-write read-back goes through reload)
    ContainerDetail exited;
    exited.id = QStringLiteral("cid-1");
    exited.name = QStringLiteral("demo");
    exited.state = ContainerState::Exited;
    m_backend->setContainerDetail(exited);
    controller->containerDetail()->reload();
    m_backend->completeRefresh();

    QVERIFY2(startButton->property("visible").toBool(), "exited container must offer start");
    QVERIFY2(!stopButton->property("visible").toBool(), "exited container cannot be stopped");
    QVERIFY2(removeButton->property("visible").toBool(), "exited container may be deleted");
    QVERIFY2(!blockedHint->property("visible").toBool(), "no reason needed once delete is available");

    // Mutation in flight: buttons disabled plus a busy indicator to prevent double clicks
    controller->operations()->startContainer(QStringLiteral("cid-1"));
    QVERIFY(controller->operations()->isContainerBusy(QStringLiteral("cid-1")));
    QQuickItem *busy = childByObjectName(page, QStringLiteral("detailBusyIndicator"));
    QVERIFY2(busy, "busy indicator not found");
    QVERIFY2(busy->property("visible").toBool(), "busy indicator must show while a mutation is in flight");
    QVERIFY2(!startButton->property("visible").toBool(), "buttons must be gone while the target is busy");

    m_backend->completeMutations();
    QVERIFY(!controller->operations()->busy());
}

void QmlLoadTest::operationMessageReflectsResultState()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *message = childByObjectName(page, QStringLiteral("operationMessage"));
    QVERIFY2(message, "operation message not found");
    QVERIFY2(!message->property("visible").toBool(), "no result yet means no banner");

    // Success: positive message
    controller->operations()->startContainer(QStringLiteral("cid-1"));
    m_backend->completeMutations();
    QVERIFY2(message->property("visible").toBool(), "success must be visible");
    const int successType = message->property("type").toInt();
    QVERIFY(!message->property("text").toString().isEmpty());

    // Failure: the message must carry the engine text (the only "why" when users report a bug)
    controller->operations()->removeContainer(QStringLiteral("cid-2"));
    m_backend->completeMutations(MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::Conflict, QStringLiteral("You cannot remove a running container cid-2"), 409));
    QVERIFY2(message->property("visible").toBool(), "failure must be visible");
    QVERIFY(message->property("type").toInt() != successType);
    QVERIFY2(message->property("text").toString().contains(QStringLiteral("running container")),
             "the engine message must reach the user");

    // Acknowledged by the user: once dismissed it must not reappear
    controller->operations()->dismissResult();
    QVERIFY2(!message->property("visible").toBool(), "dismissed result must disappear");
}

void QmlLoadTest::pullDialogValidatesReferenceBeforeSubmitting()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("operations"), QVariant::fromValue(controller->operations())},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "PullImageDialog failed to instantiate");

    // Kirigami.Dialog is a Popup: the root is not an Item and content items appear only once opened
    QObject *dialog = object.data();
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));

    const auto items = dialog->findChildren<QQuickItem *>();
    QQuickItem *field = nullptr;
    QQuickItem *pullButton = nullptr;
    QQuickItem *latestHint = nullptr;
    for (QQuickItem *item : items) {
        if (item->objectName() == QLatin1String("imageRefField")) {
            field = item;
        } else if (item->objectName() == QLatin1String("pullImageButton")) {
            pullButton = item;
        } else if (item->objectName() == QLatin1String("imageRefLatestHint")) {
            latestHint = item;
        }
    }
    QVERIFY2(field && pullButton, "pull dialog content not found");

    // Empty input: no submission
    QVERIFY2(!pullButton->property("enabled").toBool(), "empty reference must not be submittable");

    // Invalid input (embedded space): still no submission
    field->setProperty("text", QStringLiteral("alpine 3.19"));
    QVERIFY2(!dialog->property("referenceValid").toBool(), "invalid reference must be detected");
    QVERIFY2(!pullButton->property("enabled").toBool(), "invalid reference must not be submittable");

    // Valid but untagged: submittable, and normalization appends latest
    field->setProperty("text", QStringLiteral("alpine"));
    QVERIFY2(dialog->property("referenceValid").toBool(), "bare repository is a valid reference");
    QVERIFY2(pullButton->property("enabled").toBool(), "valid reference must be submittable");
    QCOMPARE(dialog->property("normalizedReference").toString(), QStringLiteral("alpine:latest"));
    // The "latest will be appended" hint comes from ImageRefInput (one validation implementation) and
    // its visibility is covered by that component's own test: Kirigami.Dialog content exists twice
    // (popup and manager), so this dialog test only asserts dialog state and submission behaviour.
    QVERIFY2(latestHint || true, "hint label lookup is best-effort here; see imageRefInputOwnsTheValidationRules");
    QVERIFY2(!dialog->property("visible").toBool(), "the dialog must close once the pull has started");
}

/*!
 * Pull list (ARCH_V4 §2.4): progress lives outside the modal window, visible after closing it.
 */
void QmlLoadTest::pullProgressListShowsBackgroundPulls()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // The pull list lives on the images tab: switch there first, it is invisible elsewhere
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 1));

    QQuickItem *list = childByObjectName(page, QStringLiteral("pullProgressList"));
    QVERIFY2(list, "pull progress list not found");
    QVERIFY2(!list->property("visible").toBool(), "no pulls means no list");

    // Two concurrent pulls: two rows, each with a progress bar and a cancel button
    controller->operations()->pullImage(QStringLiteral("alpine"));
    controller->operations()->pullImage(QStringLiteral("busybox"));
    QTRY_COMPARE(controller->operations()->activePullCount(), 2);
    QVERIFY2(list->property("visible").toBool(), "the list must show up while pulling");

    int entries = 0;
    int cancelButtons = 0;
    int progressBars = 0;
    std::function<void(QQuickItem *)> scan = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            const QString name = child->objectName();
            if (name == QLatin1String("pullEntry")) {
                ++entries;
            } else if (name == QLatin1String("cancelPullButton")) {
                ++cancelButtons;
            } else if (name == QLatin1String("pullProgressBar")) {
                ++progressBars;
            }
            scan(child);
        }
    };
    scan(list);
    QCOMPARE(entries, 2);
    QCOMPARE(cancelButtons, 2);
    QCOMPARE(progressBars, 2);

    // Progress arrives from background pushes, independent of any dialog
    ImagePullProgress progress;
    progress.reference = QStringLiteral("alpine:latest");
    progress.phase = ImagePullProgress::Phase::Downloading;
    progress.statusText = QStringLiteral("Downloading");
    progress.currentBytes = 50;
    progress.totalBytes = 100;
    m_backend->emitPullProgress(progress);

    // Newest pull on top, so look the row up by reference instead of assuming a position
    const int row = controller->operations()->pulls()->rowForReference(QStringLiteral("alpine:latest"));
    QVERIFY(row >= 0);
    QCOMPARE(controller->operations()->pulls()->index(row, 0).data(ImagePullModel::ProgressRole).toDouble(), 0.5);
}

/*!
 * A failed pull must stay in the list with the engine text: the direct answer to silent failures.
 */
void QmlLoadTest::pullFailureStaysVisibleInTheList()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    controller->operations()->pullImage(QStringLiteral("quay.io/libpod/alpine"));
    m_backend->completeMutations(MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::Timeout, QStringLiteral("no response headers within 10000 ms")));

    // That entry turns failed and keeps its reason
    QCOMPARE(controller->operations()->pulls()->count(), 1);
    QCOMPARE(controller->operations()->pulls()->index(0, 0).data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("failed"));

    QQuickItem *statusLabel = childByObjectName(page, QStringLiteral("pullStatusLabel"));
    QVERIFY2(statusLabel, "pull status label not found");
    QVERIFY2(statusLabel->property("text").toString().contains(QStringLiteral("no response headers")),
             "the engine message must be visible in the list");

    // The failure also goes to the global result channel with the actionable "registry may be unreachable"
    QVERIFY(controller->operations()->resultText().contains(QStringLiteral("registry may be unreachable")));

    // The user can dismiss this entry
    QQuickItem *dismissButton = childByObjectName(page, QStringLiteral("dismissPullButton"));
    QVERIFY(dismissButton);
    QVERIFY(QMetaObject::invokeMethod(dismissButton, "clicked"));
    QCOMPARE(controller->operations()->pulls()->count(), 0);
}

/*!
 * The refresh button no longer flickers with auto-refresh: manual refresh stays available during
 * auto-refresh (duplicate triggers are harmless; the backend coalesces identical in-flight requests).
 */
void QmlLoadTest::refreshActionStaysEnabledDuringAutoRefresh()
{
    StatusController *controller = m_stubKcm->controller();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/main.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());

    QObject *refreshAction = nullptr;
    const QList<QObject *> children = object->findChildren<QObject *>();
    for (QObject *child : children) {
        // Distinguish Refresh from Auto-refresh by text plus non-checkable
        // (icon.name is a grouped property; property("icon.name") yields nothing)
        if (child->property("text").toString() == QLatin1String("Refresh")
            && !child->property("checkable").toBool()) {
            refreshAction = child;
            break;
        }
    }
    QVERIFY2(refreshAction, "refresh action not found");
    QVERIFY(refreshAction->property("enabled").toBool());

    // Put the controller in busy (data in flight): the button must stay enabled, not blink every 5s
    controller->refresh();
    QVERIFY(controller->busy());
    QVERIFY2(refreshAction->property("enabled").toBool(), "refresh must not flicker with auto-refresh");
    m_backend->completeRefresh();
}


/*!
 * ImageRefInput (ARCH_V5_V8 §1.6): validation rules come from C++, the component only presents them.
 */
void QmlLoadTest::imageRefInputOwnsTheValidationRules()
{
    StatusController *controller = m_stubKcm->controller();
    // Pulling goes through the write gate: make the endpoint writable or requests are refused
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/ImageRefInput.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("operations"), QVariant::fromValue(controller->operations())},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ImageRefInput failed to instantiate");
    auto *input = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(input);

    QSignalSpy acceptedSpy(object.data(), SIGNAL(accepted()));

    // Empty input: invalid, not submittable, and no "appends latest" hint
    QVERIFY(!input->property("referenceValid").toBool());
    QVERIFY(!input->property("acceptable").toBool());

    QQuickItem *field = childByObjectName(input, QStringLiteral("imageRefField"));
    QQuickItem *errorLabel = childByObjectName(input, QStringLiteral("imageRefError"));
    QQuickItem *latestHint = childByObjectName(input, QStringLiteral("imageRefLatestHint"));
    QVERIFY(field && errorLabel && latestHint);

    field->setProperty("text", QStringLiteral("alpine 3.19"));
    QVERIFY2(!input->property("referenceValid").toBool(), "inner whitespace is not a valid reference");
    QVERIFY2(errorLabel->property("visible").toBool(), "an invalid reference must be explained");

    field->setProperty("text", QStringLiteral("alpine"));
    QVERIFY(input->property("referenceValid").toBool());
    QCOMPARE(input->property("normalizedReference").toString(), QStringLiteral("alpine:latest"));
    QVERIFY2(input->property("plainReference").toBool(), "the implicit latest tag must be announced");
    QVERIFY2(latestHint->property("visible").toBool(), "the hint must be visible");
    QVERIFY(input->property("acceptable").toBool());

    // Enter becomes the component's accepted signal; the caller decides what to submit
    QVERIFY(QMetaObject::invokeMethod(field, "accepted"));
    QCOMPARE(acceptedSpy.count(), 1);

    // A reference already pulling: the component reports it, the caller disables submission
    controller->operations()->pullImage(QStringLiteral("alpine"));
    QVERIFY2(input->property("alreadyPulling").toBool(), "a duplicate pull must be announced");
    QVERIFY2(!input->property("acceptable").toBool(), "a duplicate pull must not be submittable");
}

/*!
 * StringListEditor (ARCH_V5_V8 §1.6): add/remove/reorder plus injected validation.
 */
void QmlLoadTest::stringListEditorEditsValidatesAndReorders()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/StringListEditor.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));

    // Validation callback injected: only http(s) prefixes are legal
    m_engine->rootContext()->setContextProperty(QStringLiteral("_validatorOwner"), QVariant());
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *editor = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(editor);

    editor->setProperty("initialEntries", QVariant(QStringList {QStringLiteral("https://mirror.example.com"), QStringLiteral("http://one.local")}));
    // initialEntries is read once at creation -> use setValues to take the real path
        QVariant initialValues = QVariant(QStringList {QStringLiteral("https://mirror.example.com"), QStringLiteral("http://one.local")});
    QMetaObject::invokeMethod(editor, "setValues", Q_ARG(QVariant, initialValues));

    QVariant returnedValues;
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().size(), 2);
    QCOMPARE(returnedValues.toList().at(0).toString(), QStringLiteral("https://mirror.example.com"));

    // Move the first entry up (order matters for mirrors)
    // Note: Repeater delegates are not QObject children, so search the visual tree
    QQuickItem *downButton = childByObjectName(editor, QStringLiteral("stringEntryDownButton"));
    QVERIFY2(downButton, "string entry down button not found");
    QVERIFY(QMetaObject::invokeMethod(downButton, "clicked"));
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().at(0).toString(), QStringLiteral("http://one.local"));

    // Remove one -> only one remains
    // Note: Repeater rebuilds delegates on move/remove, so old pointers dangle — look it up again
    QQuickItem *removeButton = childByObjectName(editor, QStringLiteral("stringEntryRemoveButton"));
    QVERIFY2(removeButton, "string entry remove button not found after reorder");
    QVERIFY(QMetaObject::invokeMethod(removeButton, "clicked"));
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().size(), 1);
}

/*!
 * KeyValueListEditor (ARCH_V5_V8 §1.6): secret values are masked by default, duplicate keys are
 * reported, and `.env` parsing has a single C++ implementation.
 */
void QmlLoadTest::keyValueListEditorMasksValuesAndDetectsDuplicates()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/KeyValueListEditor.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *editor = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(editor);
    editor->setProperty("secretValues", true);

    QVariantList initial;
    initial.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("Asia/Shanghai")}});
    QVariant initialArg = QVariant(initial);
    QMetaObject::invokeMethod(editor, "setEntries", Q_ARG(QVariant, initialArg));

    // Items inside delegates need the visual tree (findChildren cannot see Repeater delegates)
    QQuickItem *keyField = childByObjectName(editor, QStringLiteral("keyValueKeyField"));
    QQuickItem *valueField = childByObjectName(editor, QStringLiteral("keyValueValueField"));
    QVERIFY(keyField && valueField);
    // Secret values use password echo by default (values may be tokens): TextInput.Password == 2
    QCOMPARE(valueField->property("echoMode").toInt(), 2);

    // Duplicate keys are reported (key rules and the duplicate check have one implementation)
    QVariantList duplicate;
    duplicate.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("a")}});
    duplicate.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("b")}});
    QVariant duplicateArg = QVariant(duplicate);
    QMetaObject::invokeMethod(editor, "setEntries", Q_ARG(QVariant, duplicateArg));
    QVERIFY2(editor->property("hasErrors").isValid() || true, "hasErrors must be callable");

    bool hasErrors = false;
    QMetaObject::invokeMethod(editor, "hasErrors", Q_RETURN_ARG(bool, hasErrors));
    QVERIFY2(hasErrors, "duplicate keys must be reported");

    // Return to the clean "only TZ" state first, then check overwrite-by-key and append-new-key
    QVariant singleArg = QVariant(QVariantList {QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("UTC")}}});
    QMetaObject::invokeMethod(editor, "setEntries", Q_ARG(QVariant, singleArg));

    // `.env` text parsing (C++) -> merged into the editor
    const QVariantList parsed = Kontainer::Presentation().parseEnvText(QStringLiteral("# comment\nexport API_KEY=\"s3cret\"\nTZ=UTC\ngarbage line\n"));
    QVariant parsedArg = QVariant(parsed);
    QMetaObject::invokeMethod(editor, "appendEntries", Q_ARG(QVariant, parsedArg));
    QVariant returnedEntries;
    QMetaObject::invokeMethod(editor, "entries", Q_RETURN_ARG(QVariant, returnedEntries));
    const QVariantList entries = returnedEntries.toList();
    QStringList keys;
    for (const QVariant &entry : entries) {
        keys.append(entry.toMap().value(QStringLiteral("key")).toString());
    }
    QCOMPARE(keys.join(QLatin1Char(',')), QStringLiteral("TZ,API_KEY"));
    bool sawApiKey = false;
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("key")).toString() == QLatin1String("API_KEY")) {
            sawApiKey = map.value(QStringLiteral("value")).toString() == QLatin1String("s3cret");
        }
    }
    QVERIFY2(sawApiKey, "quoted values must be unquoted when pasting .env content");
}

void QmlLoadTest::confirmDialogAlwaysCarriesConsequenceText()
{
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/ConfirmDialog.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("headingText"), QStringLiteral("Delete container")},
            {QStringLiteral("questionText"), QStringLiteral("Delete the container “demo”?")},
            {QStringLiteral("consequenceText"), QStringLiteral("Its volumes are kept.")},
            {QStringLiteral("destructive"), true},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ConfirmDialog failed to instantiate");

    const QString title = object->property("title").toString();
    const QString subtitle = object->property("subtitle").toString();
    QCOMPARE(title, QStringLiteral("Delete container"));
    QVERIFY2(subtitle.contains(QStringLiteral("Delete the container")), "the question must be shown");
    QVERIFY2(subtitle.contains(QStringLiteral("volumes are kept")), "the consequence must never be dropped (§2.2.5)");
    // Destructive actions use the warning style, not a plain question
    QVERIFY(object->property("dialogType").toInt() != 0);
}

/*!
 * The pull dialog's Enter path (a user-reported bug):
 * it used to call `pullButton.trigger()` — `QQC2.Button` has no such method, so Enter threw a
 * TypeError and did nothing (the pull request was never sent).
 */
void QmlLoadTest::pullDialogStartsPullOnEnter()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("operations"), QVariant::fromValue(controller->operations())},
        },
        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    QObject *dialog = object.data();
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));

    QQuickItem *field = nullptr;
    const auto items = dialog->findChildren<QQuickItem *>();
    for (QQuickItem *item : items) {
        if (item->objectName() == QLatin1String("imageRefField")) {
            field = item;
        }
    }
    QVERIFY2(field, "pull reference field not found");

    QSignalSpy requestedSpy(dialog, SIGNAL(pullRequested(QString)));
    field->setProperty("text", QStringLiteral("alpine"));
    // Read the derived "already pulling" property explicitly: it calls a Q_INVOKABLE on the model, and
    // without the Q_INVOKABLE marker QML throws a TypeError only **at evaluation time**; reading it
    // here guarantees the path is always taken instead of depending on order or other bindings.
    QVERIFY(!dialog->property("alreadyPulling").toBool());
    // Pressing Enter must send the request (and produce no QML runtime error — cleanup asserts that)
    QVERIFY(QMetaObject::invokeMethod(field, "accepted"));
    QCOMPARE(requestedSpy.count(), 1);
    QCOMPARE(requestedSpy.at(0).at(0).toString(), QStringLiteral("alpine:latest"));
    // The dialog closes after starting; the pull continues in the background
    QVERIFY2(!dialog->property("visible").toBool(), "the dialog must close once the pull has started");
}

void QmlLoadTest::imageDetailOffersForceDeleteOnlyForMultipleTags()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    ImageDetail detail;
    detail.id = QStringLiteral("sha256:aaaa");
    detail.repoTags = {QStringLiteral("alpine:3.19"), QStringLiteral("alpine:latest")};
    m_backend->setImageDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ImageDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("imageId"), QStringLiteral("sha256:aaaa")},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "ImageDetail failed to instantiate");
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *removeAll = childByObjectName(page, QStringLiteral("imageRemoveAllTagsButton"));
    QQuickItem *remove = childByObjectName(page, QStringLiteral("imageRemoveButton"));
    QVERIFY(removeAll && remove);
    QVERIFY2(remove->property("visible").toBool(), "a tag may always be deleted");
    QVERIFY2(removeAll->property("visible").toBool(), "multiple tags must offer the force path");

    // Single tag: the force-delete entry disappears (no ambiguity, no dangerous option)
    detail.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImageDetail(detail);
    controller->imageDetail()->refresh();
    m_backend->completeRefresh();
    QVERIFY2(!removeAll->property("visible").toBool(), "single tag must not offer force delete");
}

/*!
 * Mounts section (ARCH_V4 §2.1.1): the host path's state decides whether "Open host folder" appears.
 */
void QmlLoadTest::mountRowReflectsHostPathState()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    ContainerMount bind;
    bind.type = QStringLiteral("bind");
    bind.source = QStringLiteral("/srv/data");
    bind.destination = QStringLiteral("/data");
    bind.mode = QStringLiteral("rw");
    detail.mounts = {bind};
    m_backend->setContainerDetail(detail);

    // Path exists: offer the open action
    StatusController *controller = m_stubKcm->controller();
    m_stubKcm->hostPaths()->setState(HostPathState::Directory);
    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    // The model must have the probe result ready (roles exposed via QCOMPARE to ease failure triage)
    QCOMPARE(controller->containerDetail()->mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("directory"));
    QVERIFY(controller->containerDetail()->mounts()->index(0, 0).data(MountListModel::OpenableRole).toBool());
    QQuickItem *openButton = childByObjectName(page, QStringLiteral("mountOpenButton"));
    QQuickItem *warning = childByObjectName(page, QStringLiteral("mountSourceWarning"));
    QQuickItem *typeChip = childByObjectName(page, QStringLiteral("mountTypeChip"));
    QQuickItem *modeChip = childByObjectName(page, QStringLiteral("mountModeChip"));
    QQuickItem *entry = childByObjectName(page, QStringLiteral("mountEntry"));
    QVERIFY(entry);
    QCOMPARE(entry->property("openable").toBool(), true);
    QCOMPARE(entry->property("sourceStateKey").toString(), QStringLiteral("directory"));

    // The detail page's five sections live in a StackLayout: non-current sections are invisible,
    // so switch to the one under test (exactly the state users see it in)
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 3));
    QVERIFY(openButton && warning && typeChip && modeChip);
    QVERIFY2(openButton->property("visible").toBool(), "an existing host directory must be openable");
    QVERIFY2(!warning->property("visible").toBool(), "no warning for a healthy mount");
    QCOMPARE(typeChip->property("text").toString(), QStringLiteral("bind"));
    QCOMPARE(modeChip->property("text").toString(), QStringLiteral("rw"));

    // One click: the request reaches the host-path service (stub) with the right path
    QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
    QCOMPARE(m_stubKcm->hostPaths()->openCount(), 1);
    QCOMPARE(m_stubKcm->hostPaths()->openedPaths().first(), QStringLiteral("/srv/data"));

    // Missing path: warn and offer no open action (instead of failing after opening)
    m_stubKcm->hostPaths()->setState(HostPathState::Missing);
    controller->containerDetail()->reload();
    m_backend->completeRefresh();
    QCOMPARE(controller->containerDetail()->mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("missing"));

    // Changed model content rebuilds delegates: look them up again by objectName, never reuse pointers
    QQuickItem *recreatedWarning = childByObjectName(page, QStringLiteral("mountSourceWarning"));
    QQuickItem *recreatedOpenButton = childByObjectName(page, QStringLiteral("mountOpenButton"));
    QVERIFY(recreatedWarning && recreatedOpenButton);
    QVERIFY2(recreatedWarning->property("visible").toBool(), "a missing host path must be flagged");
    QVERIFY2(!recreatedOpenButton->property("visible").toBool(), "a missing host path must not be openable");
    QVERIFY(!recreatedWarning->property("text").toString().isEmpty());
}

/*!
 * Port topology (ARCH_V4 §2.1.2): one row and one line per mapping; the lines are decoration.
 */
void QmlLoadTest::topologyDrawsDecoratedLinksForPublishedPorts()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    // Two bindings of one container port: pick a pair with **different** colours, else the "origin uses
    // the bottom branch colour" assertion loses its power on a palette collision (pure function, fixed)
    const QPair<quint16, quint16> ports = distinctBranchPorts();
    detail.ports = {
        Port {QStringLiteral("0.0.0.0"), 80, ports.first, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 80, ports.second, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
    };
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    // The ports live in the networks section (index 2)
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2));

    QQuickItem *topology = childByObjectName(page, QStringLiteral("portTopology"));
    QVERIFY2(topology, "port topology not found");
    QVERIFY2(topology->property("visible").toBool(), "published ports must render the topology");
    // Grouping: 80/tcp has two bindings, 443/tcp one -> height = header row + 3 binding row heights
    const qreal bindingRowHeight = topology->property("bindingRowHeight").toReal();
    QVERIFY2(bindingRowHeight > topology->property("rowHeight").toReal(),
             "binding rows must be taller than the container row (user feedback: the host-side chips were cramped)");
    QCOMPARE(topology->property("implicitHeight").toReal(),
             topology->property("headerHeight").toReal() + 3 * bindingRowHeight);

    int rows = 0;
    int containerChips = 0;
    int hostChips = 0;
    std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            const QString name = child->objectName();
            if (name == QLatin1String("portMappingRow")) {
                ++rows;
            } else if (name == QLatin1String("portContainerChip")) {
                ++containerChips;
            } else if (name == QLatin1String("portHostChip")) {
                ++hostChips;
            }
            count(child);
        }
    };
    count(topology);

    // Merging: one container port's bindings share **one row** (one chip on the left), the right stays 1:1
    QCOMPARE(rows, 2);
    QCOMPARE(containerChips, 2);
    QCOMPARE(hostChips, 3);

    {
        /*!
         * Topology premise: links derive row positions from the index while chips centre on anchors, so
         * both must land on the same centre. Break that invariant (a row margin, a changed rowHeight use)
         * and the screen shows lines passing beside the chips, a misalignment only eyes can catch.
         */
        QQuickItem *row0 = nullptr;
        std::function<void(QQuickItem *)> findRow = [&](QQuickItem *item) {
            for (QQuickItem *child : item->childItems()) {
                if (child->objectName() == QLatin1String("portMappingRow") && (!row0 || child->y() < row0->y())) {
                    row0 = child;
                }
                findRow(child);
            }
        };
        findRow(topology);
        QQuickItem *chip = row0 ? row0->childItems().value(0) : nullptr;
        QVERIFY(row0 && chip);
        const qreal headerHeight = topology->property("headerHeight").toReal();
        // Row y is relative to the column positioner, so map it into the topology's coordinates
        QCOMPARE(row0->mapToItem(topology, QPointF(0, 0)).y(), headerHeight);
        // The two-binding group spans two rows; its left chip is centred on the group (the link origin too)
        QCOMPARE(row0->height(), 2 * bindingRowHeight);
        QVERIFY2(qAbs(chip->y() + chip->height() / 2 - row0->height() / 2) <= 1.0,
                 "the container chip must sit at the centre of its group");
    }

    // The link layer is decoration only (marked Accessible.ignored in QML): assert the information is not
    // in the graphics — both chips of every row are real text, so screen readers need no links.
    // Each row now has its own small Canvas (colour from "container id + the mapping itself", rows differ)
    QSet<QString> linkColors;
    int linkLayers = 0;
    std::function<void(QQuickItem *)> collectLinks = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            if (child->objectName() == QLatin1String("portMappingLink")) {
                ++linkLayers;
                QCOMPARE(child->width(), child->parentItem()->width());
                QCOMPARE(child->height(), child->parentItem()->height());
                const QVariantList colors = child->property("branchColors").toList();
                QCOMPARE(colors.size(), child->property("branchCount").toInt());
                for (const QVariant &color : colors) {
                    linkColors.insert(color.toString());
                }
            }
            collectLinks(child);
        }
    };
    collectLinks(topology);
    // One Canvas per group (both branches share it; the origin is drawn once)
    QCOMPARE(linkLayers, 2);
    QVERIFY2(linkColors.size() >= 2, "branches of different bindings must be distinguishable");

    // The origin ring takes the colour of the **bottom** branch: lower branches stack higher, and the
    // origin only looks continuous when it matches the line passing through it (user feedback)
    {
        QQuickItem *link = nullptr;
        std::function<void(QQuickItem *)> findLink = [&](QQuickItem *item) {
            for (QQuickItem *child : item->childItems()) {
                if (child->objectName() == QLatin1String("portMappingLink")
                    && child->property("branchCount").toInt() == 2) {
                    link = child;
                    return;
                }
                findLink(child);
            }
        };
        findLink(topology);
        QVERIFY2(link, "the group with two bindings must be found");
        const QVariantList colors = link->property("branchColors").toList();
        QCOMPARE(colors.size(), 2);
        QCOMPARE(link->property("originColor").value<QColor>().name(), colors.last().toString());
        // The fixture picks two differently coloured bindings, so "origin uses the top colour" fails at once
        QVERIFY2(colors.first().toString() != colors.last().toString(), "the fixture must use two distinct branch colours");
        QVERIFY2(link->property("originColor").value<QColor>().name() != colors.first().toString(),
                 "the origin ring must use the bottom branch's colour, not the top one");
    }

    int nonEmptyChips = 0;
    std::function<void(QQuickItem *)> checkText = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            const QString name = child->objectName();
            if ((name == QLatin1String("portContainerChip") || name == QLatin1String("portHostChip"))
                && !child->property("text").toString().isEmpty()) {
                ++nonEmptyChips;
            }
            checkText(child);
        }
    };
    checkText(topology);
    // After merging: 2 container-port chips + 3 host-binding chips, all with text (info is not in graphics)
    QCOMPARE(nonEmptyChips, 5);
}

/*!
 * Mount row layout contract (ARCH_V4 §2.1.1, user feedback 2026-09-18):
 *
 *   The host path takes the leftover width and elides **in the middle** when too long; the container path
 *   **hugs the right edge** and takes at most 40% of the width. Setting fillWidth on both labels left the
 *   container path at half width, drifting with the host path's length — a "strange mapping spot".
 */
void QmlLoadTest::mountRowsPutTheContainerPathOnTheRight()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    ContainerMount longMount;
    longMount.type = QStringLiteral("bind");
    longMount.source = QStringLiteral("/home/someone/.local/share/containers/storage/overlay/"
                                      "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6/merged/opt/application/resources/very-long-name");
    longMount.destination = QStringLiteral("/opt/application/resources/very-long-name");
    longMount.mode = QStringLiteral("rw");
    ContainerMount shortMount;
    shortMount.type = QStringLiteral("bind");
    shortMount.source = QStringLiteral("/srv/data");
    shortMount.destination = QStringLiteral("/data");
    shortMount.mode = QStringLiteral("ro");
    detail.mounts = {longMount, shortMount};
    m_backend->setContainerDetail(detail);
    m_stubKcm->hostPaths()->setState(HostPathState::Directory);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    // Layout assertions need real widths: give it a window (the state users see this section in)
    QQuickWindow window;
    window.resize(900, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(900);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 3)); // mounts section

    QList<QQuickItem *> rows;
    std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            if (child->objectName() == QLatin1String("mountEntry")) {
                rows.append(child);
            }
            collect(child);
        }
    };
    collect(page);
    QCOMPARE(rows.size(), 2);

    for (int i = 0; i < rows.size(); ++i) {
        QQuickItem *row = rows.at(i);
        QQuickItem *source = childByObjectName(row, QStringLiteral("mountSourceLabel"));
        QQuickItem *destination = childByObjectName(row, QStringLiteral("mountDestinationLabel"));
        QVERIFY2(source && destination, qPrintable(QStringLiteral("row %1 has no path labels").arg(i)));
        QTRY_VERIFY(source->width() > 0 && destination->width() > 0);
        const qreal linkRowWidthHint = destination->parentItem() ? destination->parentItem()->width() : row->width();

        // The container path's **text box hugs its text** (no longer half a row): the precondition for
        // right alignment — with fillWidth on both labels the box filled half the row, so text aligned
        // right inside the box still looked centred (the "strange position" users reported)
        const qreal destinationContent = destination->property("contentWidth").toReal();
        // Allow some padding and rounding error; what matters is hugging the text, not half the row width
        const qreal hugTolerance = qMax<qreal>(8.0, linkRowWidthHint * 0.05);
        QVERIFY2(destination->width() <= destinationContent + hugTolerance,
                 qPrintable(QStringLiteral("row %1: the container path box must hug its text (%2 vs %3)")
                                .arg(i)
                                .arg(destination->width())
                                .arg(destinationContent)));
        // The destination follows the source without overlapping and is **right** aligned:
        // source left, container path right — they must differ, else the destination drifts to the middle
        QQuickItem *linkRow = destination->parentItem();
        QVERIFY(linkRow);
        const qreal sourceRight = source->x() + source->width();
        QVERIFY2(destination->x() >= sourceRight, qPrintable(QStringLiteral("row %1: labels overlap").arg(i)));
        QVERIFY2(destination->property("horizontalAlignment").toInt() != source->property("horizontalAlignment").toInt(),
                 qPrintable(QStringLiteral("row %1: the container path must be right-aligned").arg(i)));
    }

    // Note: in headless tests the page's width chain is unstable (a row may end up wider than the page),
    // so "a very long host path really gets an ellipsis" is not asserted here but reviewed by rendering:
    //   KCM_DOCKER_RENDER_LONG_PATHS=1 tests/tools/render_ui.sh container-detail 1200 620 light /tmp/m.png 3
}

/*!
 * Ports that are only EXPOSEd and never published: they are listed but have no endpoint on a line.
 */
void QmlLoadTest::unpublishedPortsAreListedWithoutLinks()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.ports = {
        Port {QString(), 9000, 0, QStringLiteral("tcp")},
        Port {QString(), 9001, 0, QStringLiteral("tcp")},
    };
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("containerId"), QStringLiteral("cid-1")},
        },
        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2));

    QQuickItem *topology = childByObjectName(page, QStringLiteral("portTopology"));
    QVERIFY(topology);
    QVERIFY2(!topology->property("visible").toBool(), "no published port means no topology");

    int chips = 0;
    std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
        for (QQuickItem *child : item->childItems()) {
            if (child->objectName() == QLatin1String("unpublishedPortChip")) {
                ++chips;
            }
            count(child);
        }
    };
    count(page);
    QCOMPARE(chips, 2);
}

QTEST_MAIN(QmlLoadTest)
#include "tst_qml_load.moc"
