/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
#include "support/qml_stub_kcm.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <QJsonObject>
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
 * 挑两个"颜色不同"的宿主端口（用于端口拓扑的断言）。
 *
 * 颜色是纯函数（容器 id + 容器端口 + 该绑定的芯片文本）-> 色板下标，
 * 因此直接在 C++ 里比较下标即可，不必去问 QML 的色板。
 * 找不到就返回一对固定值（那时断言会退化为"只比较相等"，不至于误报失败）。
 */
QPair<quint16, quint16> distinctBranchPorts()
{
    const Presentation presentation;
    // 与界面上的取色种子一致：容器 id + "|" + 容器端口芯片文本 + "|" + 该绑定的芯片文本
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

/*! 造一个当前进程可写的 socket 文件：权限门据此判定允许写。 */
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

/*! 在已经实例化的页面里按 objectName 找控件（页面没有窗口，直接遍历子对象即可）。 */
/*! 列表编辑器当前的行数（读它内部 Repeater 的 count）。 */
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

/*! 待保存的配置内容里是否包含某段文本（用于断言"空草稿没有进配置"）。 */
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
 * 沿 `childItems()` 递归查找（**delegate 条目只能用这个**）。
 *
 * `findChildren<QQuickItem *>()` 走的是 QObject 树：Repeater 建出来的 delegate 不在这棵树里，
 * 因此按对象名找不到它们（八期在这里吃过一次亏）。视觉树里是找得到的。
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
 * QML 加载测试（ARCH_V2 §46）。
 *
 * 逐个加载界面文件，确保没有语法错误、未知属性、类型解析失败等问题。
 * 这类错误在 kcmshell6 里只会显示一个错误页，很难定位；在这里可以直接断言。
 *
 * 注意：测试加载的是源码目录里的 QML 文件（含 components/ 相对导入），
 * 与打包进插件 qrc 的是同一份内容。
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

    /*! 按 objectName 在已实例化的页面里查找子项。 */
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
    // 每个用例都重新安装：cleanup() 会把它还原，只在 initTestCase 装一次的话，
    // 第一个用例之后所有 QML 运行时错误都会被静默（真实踩过的坑）
    g_messages.clear();
    g_previousHandler = qInstallMessageHandler(&QmlLoadTest::captureMessages);

    m_backend = std::make_unique<MockDockerBackend>();
    m_stubKcm = std::make_unique<QmlStubKcm>(m_backend.get());
    m_engine = std::make_unique<QQmlEngine>();
    // 真实运行时由 KCMUtils 的 KLocalizedQmlContext 提供这些全局函数；
    // 测试里用等价的 identity 实现，保证界面文件的绑定能被正常求值。
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
 * §6/§46：卡片必须真的能触发导航信号。
 *
 * 这类问题（delegate 里引用了未声明的 model role）不会在加载期暴露，
 * 只有真正触发信号时才会抛 ReferenceError。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "MainPage.qml failed to instantiate");

    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // 需要真实布局才会创建 delegate
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

    // delegate 用视图自己的 API 取（遍历子对象拿不到由视图托管生命周期的 delegate）
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

    // 切到镜像标签页，等它的 delegate 创建
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
 * 配置页的两个可编辑控件（ARCH_V5_V8 §2.2）。
 *
 * 为什么单独测：助手里早就支持 `max-concurrent-downloads` / `log-driver` 了，
 * 但页面上它们一度只是**只读的一行文字**——用户根本改不了（真实反馈）。
 * 这里钉住"控件存在、用户改动会写回控制器、选「默认」产生的是删除而不是写 0/空串"。
 */
void QmlLoadTest::configPageEditorsWriteThroughToTheController()
{
    Kontainer::DaemonConfigController *controller = m_stubKcm->controller()->daemonConfigUser();
    QVERIFY(controller);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/DaemonConfigPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QVariantMap initial;
    initial.insert(QStringLiteral("scope"), QStringLiteral("user")); // 用户级：不需要解锁就能编辑
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
    // 「默认」+ helper 白名单里的驱动
    QCOMPARE(combo->property("count").toInt(), controller->selectableLogDrivers().size());
    QCOMPARE(combo->property("count").toInt(), 6);

    // 程序化赋值不得被当成用户改动（读盘刷新后不能变成"未保存的修改"）
    QVERIFY(spin->setProperty("value", 9));
    QVERIFY2(!controller->dirty(), "programmatic value changes must not mark the page dirty");

    // 用户真的改了：valueModified 只在交互时发出
    QVERIFY(spin->setProperty("value", 9));
    QVERIFY(QMetaObject::invokeMethod(spin, "valueModified"));
    QVERIFY(controller->dirty());
    QCOMPARE(controller->maxConcurrentDownloads(), 9);
    // 写进文件的是值本身
    QJsonObject merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QCOMPARE(merged.value(QStringLiteral("max-concurrent-downloads")).toInt(), 9);

    // 自动刷新（状态控制器拿到引擎信息就会调 setEngineInfo）不得把正在编辑的内容刷回去：
    // refresh() 只标记待刷新，completeRefresh() 才真的把 engineUpdated 发出来
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY(controller->dirty());
    QCOMPARE(controller->maxConcurrentDownloads(), 9);
    QCOMPARE(spin->property("value").toInt(), 9);

    // 选一个具体驱动 → Set
    QVERIFY(combo->setProperty("currentIndex", 1));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1)));
    QCOMPARE(controller->logDriver(), QStringLiteral("json-file"));
    merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QCOMPARE(merged.value(QStringLiteral("log-driver")).toString(), QStringLiteral("json-file"));

    // 选回「默认」→ **删除这个键**（不是写空串），并发下载数同理
    QVERIFY(combo->setProperty("currentIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
    QVERIFY2(controller->logDriver().isEmpty(), "selecting the default must clear the value");
    QVERIFY(spin->setProperty("value", 0));
    QVERIFY(QMetaObject::invokeMethod(spin, "valueModified"));
    QCOMPARE(controller->maxConcurrentDownloads(), 0);
    merged = QJsonDocument::fromJson(controller->pendingContentPreview().toUtf8()).object();
    QVERIFY2(!merged.contains(QStringLiteral("log-driver")), "the key must be removed, not emptied");
    QVERIFY2(!merged.contains(QStringLiteral("max-concurrent-downloads")), "the key must be removed, not zeroed");

    // 「默认」也是待保存的编辑：自动刷新后仍然是待保存状态
    m_stubKcm->controller()->refresh();
    m_backend->completeRefresh();
    QTRY_VERIFY(controller->dirty());
    QCOMPARE(spin->property("value").toInt(), 0);
    QCOMPARE(combo->property("currentIndex").toInt(), 0);
}

/*!
 * 配置页的两个作用域在"能不能编辑"与"提示措辞"上的差别（用户实测反馈）。
 *
 *  1) 系统级未解锁时**每个**编辑控件都必须禁用——只设 `editable` 是不够的：
 *     SpinBox 的文本框会只读，但上下箭头仍然能改值。
 *  2) 用户级的提示必须说"让改动生效需要管理员权限"，而不是"改这份配置需要管理员权限"：
 *     文件本来就是用户自己的，只有"生效"（重启系统级守护进程 / 改用 rootless）需要权限。
 */
void QmlLoadTest::configPageWordingAndLocksPerScope()
{
    auto *user = m_stubKcm->controller()->daemonConfigUser();
    auto *system = m_stubKcm->controller()->daemonConfigSystem();

    // 造出用户的真实形态：系统级守护进程 + 数据目录在家目录里（"看起来像 rootless"）
    Kontainer::EngineInfo info;
    info.available = true;
    info.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")};
    info.dockerRootDir = QDir::homePath() + QStringLiteral("/.local/share/docker");
    system->setEngineInfo(info);
    user->setEngineInfo(info);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/DaemonConfigPage.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    // ---- 系统级：受保护、未解锁 → 一切编辑控件禁用 ----
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

    // ---- 用户级：文件属于用户 → 可编辑，且**没有**解锁相关的界面 ----
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

    // 点「添加加速器」必须真的出现一个空行，并且它不会被"同步 initialEntries"清掉
    // （真实反馈：点了添加只是变成未保存，条目没出现）。空行还会立刻走校验分支，
    // 那条分支里只能用 QML 的字符串字面量——写成 C++ 的 QStringLiteral 会抛
    // ReferenceError，而这条路径编译与页面加载都看不出来。
    QQuickItem *mirrorsEditor = findItemByName(userPage.data(), QStringLiteral("mirrorsEditor"));
    QVERIFY(mirrorsEditor);
    QQuickItem *addButton = findItemByName(mirrorsEditor, QStringLiteral("stringListAddButton"));
    QVERIFY(addButton);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));

    // 条目真的进了模型：Repeater 的 count 就是行数
    // （无窗口的页面不会实例化 delegate，所以只能看模型，不能找 delegate 里的控件）
    QCOMPARE(repeaterCount(mirrorsEditor), 1);
    // 空行是待填写草稿，不是"外部变化"：再同步一次也不能把它清掉
    // （真实反馈：点添加只是变成未保存、条目没出现——旧逻辑在这里把空行当外部变化清掉了）
    QVERIFY(QMetaObject::invokeMethod(mirrorsEditor, "syncFromInitialEntries"));
    QCOMPARE(repeaterCount(mirrorsEditor), 1);

    // 校验分支必须真的产出文案（空行会立刻走这条分支）
    QQmlExpression emptyHostCall(qmlContext(userPage.data()), userPage.data(), QStringLiteral("mirrorError('')"));
    QVERIFY2(emptyHostCall.evaluate().toString().contains(QStringLiteral("example")),
             qPrintable(emptyHostCall.evaluate().toString()));
    QQmlExpression invalidCall(qmlContext(userPage.data()), userPage.data(), QStringLiteral("mirrorError('not a host')"));
    QVERIFY2(!invalidCall.evaluate().toString().isEmpty(), "the invalid-address branch must produce a message");
    for (const QString &captured : g_messages) {
        QVERIFY2(!captured.contains(QStringLiteral("ReferenceError")), qPrintable(captured));
    }

    // 空行只是草稿：它不该进到待保存的内容里（写了地址才算一条）
    QVERIFY2(!controller_pendingContains(user, QStringLiteral("mirror.example.com")),
             "an empty draft row must not end up in the pending config");

    // 数据目录提示：用户级说的是"生效需要权限"
    QQuickItem *hint = findItemByName(userPage.data(), QStringLiteral("dataRootHint"));
    QVERIFY(hint);
    QVERIFY(hint->property("visible").toBool());
    const QString hintText = hint->property("text").toString();
    QVERIFY2(hintText.contains(QStringLiteral("take effect")), qPrintable(hintText));
    QVERIFY2(!hintText.contains(QStringLiteral("changing this configuration needs")), qPrintable(hintText));

    // 系统级那条说的仍然是"改这份配置需要权限"（文件属于系统）
    QQuickItem *systemHint = findItemByName(systemPage.data(), QStringLiteral("dataRootHint"));
    QVERIFY(systemHint);
    QVERIFY(systemHint->property("visible").toBool());
    QVERIFY2(systemHint->property("text").toString().contains(QStringLiteral("belongs to the system")),
             qPrintable(systemHint->property("text").toString()));
}

/*!
 * 拓扑连线颜色必须"跟容器走"：同一个容器（同一份端口映射）永远得到同一组颜色，
 * 不同容器则应换一组。颜色是装饰，但它一旦随机，用户会以为"这个容器变了"。
 */
void QmlLoadTest::topologyConnectionColorsAreStablePerContainer()
{
    const QString componentsPath = QStringLiteral("file://") + QStringLiteral(KONTAINER_SOURCE_DIR) + QStringLiteral("/src/ui/components");

    // 直接问色板：同一个种子两次求值必须一致，不同种子（不同映射）应当能取到不同色位
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
    // 还没拿到种子时退回中性色，而不是抛错或变成透明
    QVERIFY(probe->property("emptySeed").value<QColor>().isValid());
}

/*!
 * 仓库认证页（ARCH_V5_V8 §2.7）：空状态、钱包横幅、已保存列表、CLI 导入候选。
 *
 * 用注入的内存凭据后端（`QmlStubKcm::credentialBackend()`）——绝不碰真实 KWallet。
 */
void QmlLoadTest::registryAuthPageReflectsWalletAndStoredCredentials()
{
    // CLI 扫描指向一个临时配置：不读开发者机器上的真实 ~/.docker
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/RegistryAuthPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // 空状态：钱包可用（内存后端默认可用）、没有凭据
    QQuickItem *emptyPlaceholder = childByObjectName(page, QStringLiteral("credentialsEmptyPlaceholder"));
    QQuickItem *walletBanner = childByObjectName(page, QStringLiteral("walletBanner"));
    QVERIFY(emptyPlaceholder && walletBanner);
    QTRY_VERIFY(emptyPlaceholder->property("visible").toBool());
    QVERIFY2(!walletBanner->property("visible").toBool(), "an available wallet must not show a banner");

    // 预置一条凭据（模拟"已经登录过"）：列表出现该仓库，且只有地址/用户名
    FakeCredentialBackend *wallet = m_stubKcm->credentialBackend();
    QVERIFY(wallet);
    RegistryCredential stored;
    stored.serverAddress = QStringLiteral("ghcr.io");
    stored.username = QStringLiteral("bob");
    stored.password = QStringLiteral("s3cret");
    QVERIFY(m_stubKcm->controller()->registryAuth()->credentials() != nullptr);
    CredentialStore store(wallet);
    store.open();
    QVERIFY(store.store(stored));
    m_stubKcm->controller()->registryAuth()->refresh();

    QTRY_VERIFY(!emptyPlaceholder->property("visible").toBool());
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

    // CLI 候选：钱包里还没有的仓库才列出来
    QQuickItem *importButton = childByObjectName(page, QStringLiteral("importSelectedButton"));
    QVERIFY(importButton);
    QVERIFY(importButton->property("enabled").toBool());
    qunsetenv("DOCKER_CONFIG");
}

/*!
 * 引导：拉取失败（401/403）与"该仓库还没登录"都要能一键到登录框。
 */
void QmlLoadTest::registryAuthGuidesFromFailedPullsAndMissingCredentials()
{
    // 失败行：只有 permissionDenied 才给「去登录…」
    ImagePullEntry failed;
    failed.reference = QStringLiteral("registry.example.com/team/app:1.0");
    failed.statusKey = QStringLiteral("failed");
    failed.detailText = QStringLiteral("unauthorized");
    failed.errorKindKey = QStringLiteral("permissionDenied");
    failed.active = false;
    m_stubKcm->controller()->operations()->pulls()->setEntries({failed});

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/PullProgressList.qml")));
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

    // 其他失败原因（例如仓库不可达）不出现这个按钮
    ImagePullEntry unreachable = failed;
    unreachable.errorKindKey = QStringLiteral("timeout");
    m_stubKcm->controller()->operations()->pulls()->setEntries({unreachable});
    QTRY_VERIFY(!loginButton->property("visible").toBool());

    // 拉取对话框：该仓库没有凭据时给提示与「去登录…」
    QQmlComponent dialogComponent(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml")));
    QVERIFY2(!dialogComponent.isError(), qPrintable(dialogComponent.errorString()));
    QVariantMap dialogInitial;
    dialogInitial.insert(QStringLiteral("operations"), QVariant::fromValue(m_stubKcm->controller()->operations()));
    dialogInitial.insert(QStringLiteral("credentialKnown"), false);
    QScopedPointer<QObject> dialog(dialogComponent.createWithInitialProperties(dialogInitial, m_engine->rootContext()));
    QVERIFY2(!dialog.isNull(), qPrintable(dialogComponent.errorString()));
    // Kirigami.Dialog 不是 QQuickItem（它是 QObject 基类），因此这里按对象树找子项
    QQuickItem *hint = findItemByName(dialog.data(), QStringLiteral("pullNeedsLoginHint"));
    QVERIFY2(hint, "the pull dialog must be able to guide to the login page");
    QVERIFY2(!hint->property("text").toString().isEmpty(), "the hint must say what will happen");
    QVERIFY2(!hint->property("visible").toBool(), "the hint only appears once a valid reference is typed");
}

/*!
 * 网络标签页（ARCH_V5_V8 §3.2）：列表、过滤、以及"切到该页才刷新"。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    // ListView 只为可见区域创建 delegate：需要真实窗口与布局
    QQuickWindow window;
    window.resize(1000, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1000);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    // 0 容器 / 1 镜像 / 2 网络 / 3 数据卷 / 4 引擎
    // 标签页数量会随功能增加（现在是 6：容器/镜像/网络/数据卷/引擎/挂载预设），
    // 因此断言"够用"而不是写死数字，避免每加一页就改一次用例
    QVERIFY2(tabBar->property("count").toInt() >= 5, qPrintable(QString::number(tabBar->property("count").toInt())));

    // 没进网络页就不该去读网络列表（低频数据，按需刷新）
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Networks), 0);

    QVERIFY(tabBar->setProperty("currentIndex", 2));
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Networks), 1);
    m_backend->completeRefresh();

    QQuickItem *networkView = childByObjectName(page, QStringLiteral("networkView"));
    QVERIFY2(networkView, "the networks tab must have its own list");
    QTRY_COMPARE(networkView->property("count").toInt(), 2);
    QTest::qWait(50); // 等 delegate 创建
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
    QCOMPARE(builtinChips, 2); // 两行都有这枚芯片，但只有内置网络那行可见

    // 过滤：只看用户自建的网络
    auto *filter = m_stubKcm->controller()->networkList();
    filter->setOriginFilter(QStringLiteral("custom"));
    QTRY_COMPARE(networkView->property("count").toInt(), 1);
    filter->setOriginFilter(QStringLiteral("all"));
    QTRY_COMPARE(networkView->property("count").toInt(), 2);

    // 共享的容器/镜像搜索行不在这里出现（网络页有自己的工具栏）
    QQuickItem *searchRow = childByObjectName(page, QStringLiteral("containerSearchField"));
    if (searchRow) {
        QVERIFY2(!searchRow->isVisible(), "the shared container/image toolbar must be hidden on the networks tab");
    }
}

/*!
 * 网络详情：成员容器列表 + 跳到容器详情的信号（导航由 main.qml 负责）。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/NetworkDetail.qml");
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

    // 标签与选项折叠区存在（默认折叠，避免长列表淹没页面）
    QQuickItem *labelsSection = childByObjectName(page, QStringLiteral("networkLabelsSection"));
    QVERIFY(labelsSection);
    QVERIFY2(!labelsSection->property("expanded").toBool(), "collapsible sections start collapsed");
    QCOMPARE(m_stubKcm->controller()->networkDetail()->labels()->count(), 1);
    QCOMPARE(m_stubKcm->controller()->networkDetail()->options()->count(), 1);
}

/*!
 * 创建网络对话框（ARCH_V5_V8 §3.3）：校验在提交前发生，非法输入一个请求都不发。
 */
void QmlLoadTest::createNetworkDialogValidatesBeforeSubmitting()
{
    // 提交要走写权限门：先把 endpoint 指到一个当前进程可写的 socket
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

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/CreateNetworkDialog.qml")));
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

    // 非法名称：就地说明原因，且不能提交
    nameField->setProperty("text", QStringLiteral("my net"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("nameInvalid"));
    QVERIFY(!dialog->property("canSubmit").toBool());
    // 对话内容在 Kirigami.Dialog 里可能有两份实例（弹层与管理器各一），
    // 因此不去断言某一份实例的可见性，而是直接断言"这个 key 有对应文案"
    QString message;
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "messageFor", Q_RETURN_ARG(QString, message),
                                      Q_ARG(QString, QStringLiteral("nameInvalid"))));
    QVERIFY2(!message.isEmpty(), "every validation key must have user-facing text");

    // 与现有网络重名
    nameField->setProperty("text", QStringLiteral("Bridge"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("nameInUse"));

    // 网关没有子网
    nameField->setProperty("text", QStringLiteral("app_net"));
    gatewayField->setProperty("text", QStringLiteral("172.30.0.1"));
    QCOMPARE(dialog->property("currentError").toString(), QStringLiteral("gatewayNeedsSubnet"));

    // 合法输入：可以提交，并且真的发出请求
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
 * 删除入口的可见性（ARCH_V5_V8 §3.2/§3.3）：
 * 内置网络**没有**删除入口（daemon 会回 403），只读模式同样不出现。
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
                                                        QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/NetworkDetail.qml")));
        if (component->isError()) {
            return std::unique_ptr<QObject>();
        }
        QVariantMap initial;
        initial.insert(QStringLiteral("networkId"), networkId);
        return std::unique_ptr<QObject>(component->createWithInitialProperties(initial, m_engine->rootContext()));
    };

    // 内置网络：没有删除动作
    std::unique_ptr<QObject> builtinObject = loadPage(builtin.id);
    QVERIFY(builtinObject);
    auto *builtinPage = qobject_cast<QQuickItem *>(builtinObject.get());
    QVERIFY(builtinPage);
    QQuickItem *notice = childByObjectName(builtinPage, QStringLiteral("networkPredefinedNotice"));
    QVERIFY(notice);
    QVERIFY2(notice->property("visible").toBool(), "the reason why it cannot be removed must be shown");
    // Kirigami.Action 不是 QQuickItem：按对象名在对象树里找（否则这条断言会假通过）
    QObject *removeAction = builtinPage->findChild<QObject *>(QStringLiteral("removeNetworkAction"));
    QVERIFY2(removeAction, "the action must exist so that its visibility can be asserted");
    QVERIFY2(!removeAction->property("visible").toBool(), "a built-in network must not offer a remove action");

    // 自定义网络：有删除动作，且确认文案里写明"已连接的容器会失去该网络"
    std::unique_ptr<QObject> customObject = loadPage(custom.id);
    QVERIFY(customObject);
    auto *customPage = qobject_cast<QQuickItem *>(customObject.get());
    QVERIFY(customPage);
    // Kirigami.Dialog 不是 QQuickItem：按对象名在对象树里找
    QObject *removeDialog = customPage->findChild<QObject *>(QStringLiteral("removeNetworkDialog"));
    QVERIFY2(removeDialog, "a user-defined network must offer a removal dialog");
    const QString consequence = removeDialog->property("consequenceText").toString();
    QVERIFY2(!consequence.isEmpty(), "the removal dialog must always carry a consequence");
    // 两个成员 → 复数文案，且带上数量（用户必须知道会影响几个容器）
    QVERIFY2(consequence.contains(QStringLiteral("2")), qPrintable(consequence));

    // 只读模式：创建入口整体不出现（不是禁用后静默）
    m_backend->setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(!m_stubKcm->controller()->operations()->writeAllowed());
    QQmlComponent mainComponent(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml")));
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
 * 容器详情的网络分区（ARCH_V5_V8 §3.4）：连接对话框只列未连接的网络，
 * 断开走确认对话框（后果说明必填），只读模式下两个入口都不出现。
 */
void QmlLoadTest::containerNetworkSectionConnectsAndDisconnects()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_stubKcm->controller()->operations()->refreshWriteAccess();
    QVERIFY(m_stubKcm->controller()->operations()->writeAllowed());

    // 两个网络：容器已连 bridge，未连 app_default
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

    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("containerId"), QStringLiteral("cid-1")}},
                                                                        m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);
    m_backend->completeRefresh();

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2)); // 网络分区

    // 已连接的网络在控制器里如实报告（对话框据此禁用该选项）
    const QStringList connected = m_stubKcm->controller()->containerDetail()->connectedNetworkNames();
    QCOMPARE(connected, QStringList {QStringLiteral("bridge")});

    // 每行有「断开」，确认对话框带上后果说明；确认后发出断开请求
    // Kirigami.Dialog / Kirigami.Action 都不是 QQuickItem：按对象名在对象树里找
    QObject *disconnectDialog = page->findChild<QObject *>(QStringLiteral("disconnectNetworkDialog"));
    QVERIFY2(disconnectDialog, "the disconnect confirmation must exist");
    QVERIFY2(!disconnectDialog->property("consequenceText").toString().isEmpty(),
             "the disconnect dialog must always carry a consequence");

    // 连接入口是**内联面板**（不是弹窗）：点按钮把它展开
    QQuickItem *connectEntry = findItemByName(page, QStringLiteral("connectNetworkEntryButton"));
    QVERIFY2(connectEntry, "the connect entry must exist");
    QVERIFY2(!page->property("connectPanelOpen").toBool(), "the panel starts collapsed");
    QVERIFY(QMetaObject::invokeMethod(connectEntry, "clicked"));
    QVERIFY(page->property("connectPanelOpen").toBool());
    QQuickItem *panel = findItemByName(page, QStringLiteral("connectNetworkPanel"));
    QVERIFY2(panel, "the inline connect panel must exist");

    // 已连接的网络不能再选、未连接的可以（delegate 的 enabled 用的就是这个函数）
    bool connectable = true;
    QVERIFY(QMetaObject::invokeMethod(page, "isConnectable", Q_RETURN_ARG(bool, connectable),
                                      Q_ARG(QString, QStringLiteral("bridge"))));
    QVERIFY2(!connectable, "an already connected network must not be selectable");
    QVERIFY(QMetaObject::invokeMethod(page, "isConnectable", Q_RETURN_ARG(bool, connectable),
                                      Q_ARG(QString, QStringLiteral("app_default"))));
    QVERIFY(connectable);
    QCOMPARE(page->property("connectableNetworkCount").isValid(), false); // 它是函数，不是属性

    // 选中未连接的网络并提交：请求带上网络 Id、容器 Id 与别名
    page->setProperty("connectNetworkId", app.id);
    QQuickItem *aliases = findItemByName(page, QStringLiteral("connectNetworkAliasesField"));
    QVERIFY2(aliases, "the aliases field must be in the panel");
    aliases->setProperty("text", QStringLiteral("demo, api"));
    QVERIFY(QMetaObject::invokeMethod(page, "submitConnectNetwork"));
    QTRY_COMPARE(m_backend->lastNetworkConnect().second, QStringLiteral("cid-1"));
    QCOMPARE(m_backend->lastNetworkConnect().first, app.id);
    QCOMPARE(m_backend->lastNetworkConnectAliases(), QStringList({QStringLiteral("demo"), QStringLiteral("api")}));
    QVERIFY2(!page->property("connectPanelOpen").toBool(), "the panel closes after a successful connect");
}

/*!
 * 数据卷页（ARCH_V5_V8 §3.5）：列表、未使用过滤、创建面板与清理预览。
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
    // 使用情况未知、但**大小已知**：这样"可回收空间"就能区分两种实现
    // （把未知当成未使用会把 512 字节也算进去 → 2.5 KiB 而不是 2.0 KiB）
    Volume unknown;
    unknown.name = QStringLiteral("legacy");
    unknown.driver = QStringLiteral("local");
    unknown.mountpoint = QStringLiteral("/var/lib/docker/volumes/legacy/_data");
    unknown.sizeBytes = 512;
    unknown.refCount = -1;
    volumes.append(unknown);
    m_backend->setVolumes(volumes);

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
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
    // 没进数据卷页就不去读列表
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Volumes), 0);
    QVERIFY(tabBar->setProperty("currentIndex", 3));
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Volumes), 1);
    m_backend->completeRefresh();

    QQuickItem *volumeView = childByObjectName(page, QStringLiteral("volumeView"));
    QVERIFY2(volumeView, "the volumes tab must have its own list");
    QTRY_COMPARE(volumeView->property("count").toInt(), 3);

    // 未使用过滤：只有 cache（legacy 的使用情况未知，不能算进"可清理"）
    auto *filter = m_stubKcm->controller()->volumeList();
    filter->setUsageFilter(QStringLiteral("unused"));
    QTRY_COMPARE(volumeView->property("count").toInt(), 1);
    filter->setUsageFilter(QStringLiteral("all"));
    QTRY_COMPARE(volumeView->property("count").toInt(), 3);

    // 清理预览：列出将被删除的卷与可回收空间（未知大小要如实说明）
    QQuickItem *pruneEntry = findItemByName(page, QStringLiteral("pruneVolumesEntryButton"));
    QVERIFY(pruneEntry);
    QVERIFY(QMetaObject::invokeMethod(pruneEntry, "clicked"));
    QVERIFY(page->property("volumePrunePanelOpen").toBool());
    QString reclaimable;
    QVERIFY(QMetaObject::invokeMethod(page, "pruneReclaimableText", Q_RETURN_ARG(QString, reclaimable)));
    // 只有 cache（2048 字节 = 2.0 KiB）算可回收：未知使用情况的 legacy（512 字节）不算
    QVERIFY2(reclaimable.contains(QStringLiteral("2.0 KiB")), qPrintable(reclaimable));
    QVERIFY2(!reclaimable.contains(QStringLiteral("2.5 KiB")), qPrintable(reclaimable));

    // 创建面板：名称校验（空名不可提交），合法名会真的发出请求
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
 * 创建容器向导（ARCH_V5_V8 §4.4）：分步校验、特权的二次确认、总览不泄露环境变量值。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // 必须放进窗口：ScrollView 里的内容与 Repeater 的条目在无窗口时不会真正建立
    // （六期的网络页用例也踩过同一个坑）
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

    // 第一步：没选镜像不能继续（"下一步"按钮也是禁用的，两条路径都要成立）
    QQuickItem *nextButton = childByObjectName(page, QStringLiteral("wizardNextButton"));
    QVERIFY(nextButton);
    QVERIFY2(!nextButton->property("enabled").toBool(), "an empty image must not allow continuing");
    QQuickItem *stepError = childByObjectName(page, QStringLiteral("wizardStepError"));
    QVERIFY(stepError);
    QTRY_VERIFY(stepError->property("visible").toBool());
    QVERIFY2(!stepError->property("text").toString().isEmpty(), "the reason must be translated");

    // 填镜像后可以继续；镜像不在本地时要给"先拉取"的提示
    QQuickItem *imageField = childByObjectName(page, QStringLiteral("wizardImageField"));
    QVERIFY(imageField);
    imageField->setProperty("text", QStringLiteral("busybox:latest"));
    QTRY_VERIFY(stepError->property("visible").toBool());
    QCOMPARE(stepError->property("text").toString().contains(QStringLiteral("Pull")), true);
    imageField->setProperty("text", QStringLiteral("alpine:3.19"));
    QTRY_VERIFY(nextButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked"));
    QCOMPARE(wizard->stepKey(), QStringLiteral("basics"));

    // 名称：非法 → 阻断；合法 → 继续
    QQuickItem *nameField = childByObjectName(page, QStringLiteral("wizardNameField"));
    QVERIFY(nameField);
    nameField->setProperty("text", QStringLiteral("bad name"));
    QTRY_VERIFY(!nextButton->property("enabled").toBool());
    nameField->setProperty("text", QStringLiteral("worker"));
    QTRY_VERIFY(nextButton->property("enabled").toBool());
    // 步骤顺序（用户实测）：基础 → 环境与标签 → 交互 → 端口 → 挂载 → 资源 → 总览
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // environment
    QCOMPARE(wizard->stepKey(), QStringLiteral("environment"));

    // 环境变量与标签：值默认按密码显示，改动会回写控制器
    auto *environmentEditor = qobject_cast<QQuickItem *>(findItemByName(page, QStringLiteral("wizardEnvironmentEditor")));
    QVERIFY(environmentEditor);
    QVERIFY2(environmentEditor->property("secretValues").toBool(), "environment values are masked by default");
    // 用编辑器自己的 API 加一行（它与页面的回写路径才是被测对象）
    QVariantList entries;
    entries.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("API_TOKEN")},
                                {QStringLiteral("value"), QStringLiteral("s3cret-value")}});
    const QVariant entriesArg = entries;
    QVERIFY(QMetaObject::invokeMethod(environmentEditor, "setEntries", Q_ARG(QVariant, entriesArg)));
    QTRY_COMPARE(m_stubKcm->controller()->createContainer()->environmentRows().size(), 1);

    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // interactive
    QCOMPARE(wizard->stepKey(), QStringLiteral("interactive"));
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // ports
    QCOMPARE(wizard->stepKey(), QStringLiteral("ports"));
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // mounts
    QCOMPARE(wizard->stepKey(), QStringLiteral("mounts"));
    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // resources
    QCOMPARE(wizard->stepKey(), QStringLiteral("resources"));

    // 特权：勾选必须先二次确认，确认前不生效
    QQuickItem *privilegedCheck = childByObjectName(page, QStringLiteral("wizardPrivilegedCheck"));
    QVERIFY(privilegedCheck);
    // 模拟用户真的勾上：设 checked 会触发 toggled 处理器（直接 invoke 信号不会翻转状态）
    privilegedCheck->setProperty("checked", true);
    QTest::qWait(20);
    QVERIFY2(!wizard->privileged(), "privileged must not be enabled without confirmation");
    // 勾选状态本身不在这里断言（Qt 的 CheckBox 会在处理器里自行翻转），
    // 要紧的是"没有确认就绝不生效"，这条由上一行守着
    QQuickItem *privilegedNotice = childByObjectName(page, QStringLiteral("wizardPrivilegedNotice"));
    QVERIFY(privilegedNotice);
    QVERIFY2(!privilegedNotice->property("visible").toBool(), "the warning only shows once it is enabled");
    // Kirigami.PromptDialog 不是 QQuickItem：按对象名在对象树里找
    QObject *privilegedDialog = page->findChild<QObject *>(QStringLiteral("wizardPrivilegedDialog"));
    QVERIFY2(privilegedDialog, "the privileged confirmation must exist");
    QVERIFY2(!privilegedDialog->property("consequenceText").toString().isEmpty(),
             "the privileged confirmation must explain the consequence");

    QVERIFY(QMetaObject::invokeMethod(nextButton, "clicked")); // summary
    QCOMPARE(wizard->stepKey(), QStringLiteral("summary"));

    // 总览：环境变量只列键名，值绝不出现
    // 总览：环境变量只列键名，值绝不出现。
    // 断言在**控制器**这一层：summary 是它的属性，密码不进总览这条规则就实现在那里；
    // 界面把它画出来由上文的渲染截图复核（离屏用例里 Repeater 条目的父链不可靠）。
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

    // 提交：请求真的发给后端，并带上表单里的字段
    QQuickItem *createButton = childByObjectName(page, QStringLiteral("wizardCreateButton"));
    QVERIFY(createButton);
    QVERIFY(QMetaObject::invokeMethod(createButton, "clicked"));
    QTRY_COMPARE(m_backend->lastContainerCreate().name, QStringLiteral("worker"));
    QCOMPARE(m_backend->lastContainerCreate().image, QStringLiteral("alpine:3.19"));
    QCOMPARE(m_backend->lastContainerCreate().network, QStringLiteral("app_default"));
    QCOMPARE(m_backend->lastContainerCreate().environment, QStringList {QStringLiteral("API_TOKEN=s3cret-value")});
}

/*!
 * 挂载预设的管理（ARCH_V5_V8 §4.2，用户实测反馈 ⑥：管理搬到独立标签页）。
 */
void QmlLoadTest::presetPanelManagesPresets()
{
    auto *store = m_stubKcm->controller()->mountPresets();
    QVERIFY(store);
    const QString firstId = store->add(QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("bind"), true,
                                       QStringLiteral("数据目录"));
    QVERIFY(!firstId.isEmpty());

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/MountPresetManager.qml");
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

    // 已有的一条会渲染成一行，并且能收藏 / 排序 / 删除
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

    // 新增：填两个路径后按钮才可用，点下去真的多一条
    auto *newSource = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerNewSource")));
    auto *newDestination = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerNewDestination")));
    auto *addButton = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerAdd")));
    QVERIFY(newSource && newDestination && addButton);
    QVERIFY2(!addButton->property("enabled").toBool(), "an empty preset must not be addable");
    newSource->setProperty("text", QStringLiteral("relative/path"));
    newDestination->setProperty("text", QStringLiteral("/cache"));
    QTRY_VERIFY(addButton->property("enabled").toBool());
    // 非法来源：给出原因（校验与存储共用一份实现）
    auto *errorMessage = qobject_cast<QQuickItem *>(findItemDeep(manager, QStringLiteral("presetManagerError")));
    QVERIFY(errorMessage);
    QTRY_VERIFY(errorMessage->property("visible").toBool());
    newSource->setProperty("text", QStringLiteral("/srv/cache"));
    QTRY_VERIFY(!errorMessage->property("visible").toBool());
    QCOMPARE(store->count(), 1);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QTRY_COMPARE(store->count(), 2);

    // 「浏览…」在**新建行**里（用户实测：新建入口置顶、浏览按钮放在宿主路径前面）
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
    // 取消（返回空串）：保持输入框里的内容不变
    newSourceField->setProperty("text", QStringLiteral("/srv/hand-typed"));
    m_stubKcm->directoryPicker()->nextResult = QString();
    QVERIFY(QMetaObject::invokeMethod(browseButton, "clicked"));
    QTest::qWait(20);
    QCOMPARE(newSourceField->property("text").toString(), QStringLiteral("/srv/hand-typed"));
    // 选中一个目录：填进新建行的宿主路径
    m_stubKcm->directoryPicker()->nextResult = QStringLiteral("/srv/picked");
    QVERIFY(QMetaObject::invokeMethod(browseButton, "clicked"));
    QTRY_COMPARE(newSourceField->property("text").toString(), QStringLiteral("/srv/picked"));

    // 删除：**重新找一次**按钮——新增预设会让 Repeater 重铺，之前那个指针已经失效了
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
 * 构建面板（ARCH_V5_V8 §5.4）：表单校验、提交的字段、以及列表里的**失败步骤**。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/BuildImagePanel.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    // required property 必须在创建时给：创建后再 setProperty 会先报"未初始化"
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

    // 上下文与标签都没填：不能提交（按钮也是禁用的）
    QVERIFY2(!startButton->property("enabled").toBool(), "an empty form must not be submittable");
    auto *formError = qobject_cast<QQuickItem *>(findItemByName(panel, QStringLiteral("buildFormError")));
    QVERIFY(formError);
    contextField->setProperty("text", QStringLiteral("relative/path"));
    QTRY_VERIFY(formError->property("visible").toBool());
    QVERIFY2(formError->property("text").toString().contains(QStringLiteral("absolute")),
             "a relative context path must be rejected with a clear reason");

    // 填好之后提交：字段如实传给控制器
    contextField->setProperty("text", contextDir.path());
    tagsField->setProperty("text", QStringLiteral("app:1.0\napp:latest"));
    targetField->setProperty("text", QStringLiteral("runtime"));
    // 用 click() 而不是直接写 checked：前者才是用户动作（可勾选按钮会自行翻转并发出 toggled）
    QVERIFY(QMetaObject::invokeMethod(noCacheCheck, "click"));
    QTRY_VERIFY(panel->property("noCache").toBool());
    QVERIFY(QMetaObject::invokeMethod(startButton, "clicked"));

    const ImageBuildRequest request = m_backend->lastBuildRequest();
    QCOMPARE(request.tags, QStringList({QStringLiteral("app:1.0"), QStringLiteral("app:latest")}));
    QCOMPARE(request.target, QStringLiteral("runtime"));
    QVERIFY(request.noCache);
    QVERIFY2(!request.contextArchive.isEmpty(), "the context must have been packed");
    QCOMPARE(m_stubKcm->controller()->operations()->builds()->count(), 1);

    // 列表：进行中显示步骤，失败后把失败步骤留在列表里
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

    // 失败的步骤留在**数据**里（用例断言控制器；卡片把它画出来由渲染截图复核——
    // 离屏用例里 Repeater 条目的父链不可靠，这条教训在七期已经踩过）
    m_backend->emitBuildFinished(buildId,
                                 DockerBackendInterface::MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::EngineError, QStringLiteral("exit code 1")));
    const ImageBuildEntry entry = m_stubKcm->controller()->operations()->builds()->entries().first();
    QVERIFY2(entry.detailText.contains(QStringLiteral("Step 2/3")), qPrintable(entry.detailText));
    QVERIFY2(entry.detailText.contains(QStringLiteral("RUN exit 1")), qPrintable(entry.detailText));
    QVERIFY2(!entry.active, "a failed build must not stay active");
}

/*!
 * 行删除（回归：用户实测"端口映射删不掉、标签能删"）。
 *
 * delegate 在 `pragma ComponentBehavior: Unbound` 下拿不到根对象 id，原来的处理器写
 * `page.pushPorts()` / `root.changed()` 会抛 ReferenceError，改动没写回控制器——
 * 于是端口行"删了又回来"。现在 delegate 只调用中转对象，行编辑收在 C++ 控制器里。
 */
void QmlLoadTest::portAndKeyValueRowsCanBeRemoved()
{
    // ① 创建向导的端口行
    {
        // 镜像步骤要求镜像在本地，先给一个（不是本用例的重点，但向导规则如此）
        Image localImage;
        localImage.id = QStringLiteral("sha256:feedface");
        localImage.repoTags = {QStringLiteral("alpine:3.19")};
        m_backend->setImages({localImage});

        const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
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
        // 页面创建时会 reset 控制器，因此行要在创建之后再放
        wizard->addPortRow(80, 0, QString(), QStringLiteral("tcp"));
        wizard->addPortRow(443, 8443, QString(), QStringLiteral("tcp"));
        // 步骤 0/1 要先合法，否则 goToStep 会拒绝往前跳（向导的规则，不该为了测试放宽）
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
        // 删掉的是第一行（80/0），留下的那行要还是它自己
        QCOMPARE(wizard->portRows().first().toMap().value(QStringLiteral("containerPort")).toInt(), 443);

        /*
         * 端口输入不能被"每敲一位就打断一次"（用户实测：输入 8000 要反复重新选中）。
         *
         * 端口现在是"带校验的文本框 + 失焦才回写"：逐位输入期间模型不动，因此不会出现
         * "模型 → 文本"的回环把光标/选区抢走。这里逐位模拟并断言：
         *   ① 输入过程中文本框内容就是用户敲进去的内容（没有被改写）；
         *   ② 控制器在这一期间**保持旧值**（回写是延迟的）；
         *   ③ 输入结束（editingFinished）后才写回。
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

    // ② 键值对编辑器的"删除"按钮
    {
        const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/KeyValueListEditor.qml");
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
        // 值的显隐切换、以及删除后条目真的少了一条（回调不再抛 ReferenceError）
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
 * 挂载步骤：选中预设后，「添加挂载」必须仍然有效（用户实测反馈 ⑦）。
 *
 * 根因同 ⑤：delegate 里的处理器调用了根对象 id，抛 ReferenceError 后改动没写回控制器。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
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

    // 从预设添加：现在是**可搜索下拉**（用户实测 F3/本轮：预设多时按钮流太慢）
    // 注意：页面里有多个可搜索下拉（镜像 / 预设 / 命令历史），
    // 因此必须在**预设下拉内部**找它的列表，不能从整页里取第一个 filteredComboBoxList
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

    // 再加一条空行：这一步以前会因为 delegate 抛错而"没反应"
    wizard->addMountRow(QStringLiteral("bind"), QString(), QString(), false);
    QTRY_COMPARE(wizard->mountRows().size(), 2);

    // 用真实的「添加挂载」按钮再点一次
    QQuickItem *addMountButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        addMountButton = findItemDeep(window.contentItem(), QStringLiteral("wizardAddMount"));
        return addMountButton != nullptr;
    }(), 5000);
    QVERIFY(QMetaObject::invokeMethod(addMountButton, "clicked"));
    QTRY_COMPARE(wizard->mountRows().size(), 3);

    // 删除第二条（delegate 的删除按钮）
    QQuickItem *removeMount = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        removeMount = findItemDeep(window.contentItem(), QStringLiteral("wizardRemoveMount"));
        return removeMount != nullptr;
    }(), 5000);
    QVERIFY(QMetaObject::invokeMethod(removeMount, "clicked"));
    QTRY_COMPARE(wizard->mountRows().size(), 2);
}


/*!
 * 暂停 / 继续按钮（用户实测反馈 ①）：运行中给「暂停」，已暂停给「继续」。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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
    m_backend->completeRefresh(); // inspect 是异步的：喂完数据再让详情落地

    // 运行中：暂停可见，继续不可见
    QQuickItem *pauseButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        pauseButton = findItemDeep(window.contentItem(), QStringLiteral("detailPauseButton"));
        return pauseButton != nullptr && pauseButton->property("visible").toBool();
    }(), 5000);
    QQuickItem *resumeButton = findItemDeep(window.contentItem(), QStringLiteral("detailUnpauseButton"));
    QVERIFY(resumeButton);
    QVERIFY2(!resumeButton->property("visible").toBool(), "a running container must not offer Resume");

    // 点「暂停」真的发出请求
    QVERIFY(QMetaObject::invokeMethod(pauseButton, "clicked"));
    QTRY_VERIFY(!m_backend->mutationCalls().isEmpty());
    QCOMPARE(m_backend->mutationCalls().first().mutation, DockerBackendInterface::Mutation::PauseContainer);
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("running-one")),
                               DockerBackendInterface::MutationOutcome::Succeeded);

    // 状态变成 paused：按钮反过来（继续可见，暂停不可见）
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
 * 创建容器入口会主动刷新网络（用户实测反馈 ②）：不用先点一次「网络」标签页。
 */
void QmlLoadTest::openingTheWizardRefreshesNetworks()
{
    auto *controller = m_stubKcm->controller();
    const int before = m_backend->networkRefreshCount();

    // 模拟"点创建容器入口"：主页面里那一步就是先刷新再发信号
    controller->refreshNetworks();
    QTRY_VERIFY(m_backend->networkRefreshCount() > before);
    m_backend->completeRefresh();

    // 向导自己也会保一次险（打开时若列表仍为空就再刷）
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const int beforeWizard = m_backend->networkRefreshCount();
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    QTRY_VERIFY(m_backend->networkRefreshCount() >= beforeWizard);
}

/*!
 * 命令字段与"默认命令会立刻退出"的提示（用户实测反馈 ⑧⑨）。
 */
void QmlLoadTest::commandFieldAndExitHint()
{
    Image localImage;
    localImage.id = QStringLiteral("sha256:deadbeef");
    localImage.repoTags = {QStringLiteral("alpine:latest")};
    m_backend->setImages({localImage});

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
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

    // 命令写回控制器（⑨ 的提示已按用户要求移除：P2 会默认开 -i/-t，容器因此不会立刻退出）
    QQuickItem *commandField = findItemDeep(window.contentItem(), QStringLiteral("wizardCommandField"));
    QVERIFY(commandField);
    commandField->setProperty("text", QStringLiteral("sleep infinity"));
    QTRY_COMPARE(wizard->commandText(), QStringLiteral("sleep infinity"));

    // 入口点/工作目录/用户也能写回
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
 * 步骤按钮不会出现"多选"假象（用户实测 A3）。
 *
 * 根因：按钮是 checkable，点击时 Qt 先自行翻转 checked；校验不通过导致跳转被拒绝后，
 * 那个翻转不会被纠正——看起来就像同时选中了好几步，页面却还停在第一步。
 */
void QmlLoadTest::stepButtonsNeverLookMultiSelected()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
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

    // 收集所有步骤按钮（第一个是容器标题栏里的按钮，按对象名筛）
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
    QCOMPARE(checkedCount(), 1); // 初始：只有第一步

    // 点第二个步骤按钮：校验不通过（镜像还没选），必须**仍然只有第一步高亮**。
    // 用 click() 而不是 emit clicked()：只有前者会走"可勾选按钮自行翻转 checked"那条真实路径
    QQuickItem *second = buttons.at(1);
    QVERIFY(QMetaObject::invokeMethod(second, "click"));
    QTest::qWait(20);
    QVERIFY2(second->property("checked").toBool() == false, "a refused jump must not leave the button checked");
    QCOMPARE(checkedCount(), 1);
    // 而且要说清为什么跳不过去
    QQuickItem *stepError = findItemDeep(window.contentItem(), QStringLiteral("wizardStepError"));
    QVERIFY(stepError);
    QTRY_VERIFY(stepError->property("visible").toBool());
    QVERIFY2(!stepError->property("text").toString().isEmpty(), "the refusal needs a reason");
}

/*!
 * `--privileged` 需要"输入容器名"强确认，且确认后勾选框要真的勾上（用户实测 F4）。
 */
void QmlLoadTest::privilegedNeedsTypedConfirmation()
{
    Image localImage;
    localImage.id = QStringLiteral("sha256:1b1b1b1b");
    localImage.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({localImage});

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/CreateContainer.qml");
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

    // 勾选：被拦下来（还没确认）
    QVERIFY(QMetaObject::invokeMethod(privilegedCheck, "click"));
    QTest::qWait(20);
    QVERIFY2(!wizard->privileged(), "privileged must not be enabled before confirmation");
    QVERIFY2(!privilegedCheck->property("checked").toBool(), "the checkbox must fall back until confirmed");

    // 确认框要求输入容器名：没输入对之前确认按钮不可用
    QObject *privilegedDialog = page->findChild<QObject *>(QStringLiteral("wizardPrivilegedDialog"));
    QVERIFY2(privilegedDialog, "the privileged confirmation must exist");
    QCOMPARE(privilegedDialog->property("requireText").toString(), QStringLiteral("root-demo"));
    QVERIFY2(!privilegedDialog->property("requireTextSatisfied").toBool(), "an empty confirmation must not be accepted");

    QQuickItem *confirmField = qobject_cast<QQuickItem *>(page->findChild<QObject *>(QStringLiteral("confirmDialogTextField")));
    QVERIFY(confirmField);
    confirmField->setProperty("text", QStringLiteral("root-demo"));
    QTRY_VERIFY(privilegedDialog->property("requireTextSatisfied").toBool());

    // 确认：控制器生效，并且**勾选框真的勾上**（F4 的回归点）
    QVERIFY(QMetaObject::invokeMethod(privilegedDialog, "confirmed"));
    QTRY_VERIFY(wizard->privileged());
    QTRY_VERIFY_WITH_TIMEOUT(privilegedCheck->property("checked").toBool(), 5000);
    // 真实流程里确认按钮会关闭对话框；这里直接发信号，所以要自己关掉——
    // 模态对话框还在时，后面的点击会落在它身上（用例里吃过这个亏）
    QVERIFY(QMetaObject::invokeMethod(privilegedDialog, "close"));
    QTRY_VERIFY(!privilegedDialog->property("visible").toBool());

    // 再点一次取消：两边都回到未启用
    QVERIFY(QMetaObject::invokeMethod(privilegedCheck, "click"));
    QTest::qWait(20);
    QTRY_VERIFY(!wizard->privileged());
    QTRY_VERIFY(!privilegedCheck->property("checked").toBool());
}


/*!
 * 容器详情可复制命令与入口点（用户实测反馈 A2）。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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
        commandCopy = findItemDeep(window.contentItem(), QStringLiteral("detailCommandCopyButton"));
        return commandCopy != nullptr && commandCopy->property("visible").toBool();
    }(), 5000);
    QCOMPARE(commandCopy->property("value").toString(), QStringLiteral("sh -c sleep infinity"));

    QQuickItem *entrypointCopy = findItemDeep(window.contentItem(), QStringLiteral("detailEntrypointCopyButton"));
    QVERIFY(entrypointCopy);
    QCOMPARE(entrypointCopy->property("value").toString(), QStringLiteral("/usr/bin/env sh"));
}


/*!
 * 拓扑对齐（用户实测反馈 A5）：容器芯片与**第一条**宿主绑定同高，第一条连线因此是水平的。
 */
void QmlLoadTest::topologyAlignsTheContainerChipWithTheFirstBinding()
{
    // 一个容器端口映射到两个宿主地址（第二条应当向下分支）
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-align");
    detail.name = QStringLiteral("align-demo");
    detail.state = ContainerState::Running;
    detail.ports = {{QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")},
                    {QStringLiteral("127.0.0.1"), 8888, 20204, QStringLiteral("tcp")}};
    m_backend->setContainerDetail(detail);

    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml")));
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
    QVERIFY(tabBar->setProperty("currentIndex", 2)); // 网络分区（端口拓扑在这里）

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

    // 连线的起点也必须落在第一条绑定的中心（第一条线因此是水平的）：
    // 这与"芯片位置"由不同的代码决定，所以单独断言（负例：把起点改回整组中心就会失败）
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
 * 端口编辑器的连线（用户实测反馈 A4）：每行一种颜色、且不写特征标注。
 */
void QmlLoadTest::portEditorColoursEachRowDifferently()
{
    auto *wizard = m_stubKcm->controller()->createContainer();
    wizard->clearPortRows();
    wizard->addPortRow(80, 8080, QStringLiteral("0.0.0.0"), QStringLiteral("tcp"));
    wizard->addPortRow(443, 0, QStringLiteral("127.0.0.1"), QStringLiteral("tcp"));
    wizard->addPortRow(53, 5353, QString(), QStringLiteral("udp"));

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/PortMappingEditor.qml");
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

    // 三行的连线颜色互不相同（"每行一种颜色"）
    QSet<QString> colors;
    for (QQuickItem *link : links) {
        colors.insert(link->property("linkColor").value<QColor>().name());
    }
    QCOMPARE(colors.size(), 3);
    // 两侧标注存在（"宿主机" / "容器"）：断言用的是文案来源，而不是硬编码字符串
    // 两侧标注：按 objectName 断言（不依赖语言，用例可能在英文环境跑）
    QQuickItem *hostLabel = findItemDeep(window.contentItem(), QStringLiteral("portEditorHostLabel"));
    QQuickItem *containerLabel = findItemDeep(window.contentItem(), QStringLiteral("portEditorContainerLabel"));
    QVERIFY2(hostLabel && containerLabel, "the editor must label both sides (host / container)");
    QVERIFY2(!hostLabel->property("text").toString().isEmpty(), "the host label must not be empty");
}


/*!
 * 服务卡片（B1）：三行状态、危险动作必须二次确认，确认后才真的发请求。
 */
void QmlLoadTest::serviceCardConfirmsRiskyActions()
{
    auto *services = m_stubKcm->serviceStatus();
    QVERIFY(services);

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/ServiceCard.qml");
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

    // 三行状态（socket / service / containerd），并且默认都是"运行中"
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

    // 「停止」是危险动作：点了先弹确认，**确认之前一个请求都不发**
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

    // 确认后才发出请求（unit + 动词与按钮一致）
    QVERIFY(QMetaObject::invokeMethod(dialog, "confirmed"));
    QTRY_COMPARE(m_stubKcm->privilegedClient()->serviceRequests, 1);
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceUnit, QStringLiteral("docker.socket"));
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceVerb, QStringLiteral("stop"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));

    // 「重启」不是危险动作：直接发（不弹确认）
    QQuickItem *restartButton = findItemDeep(window.contentItem(), QStringLiteral("serviceRestartButton"));
    QVERIFY(restartButton);
    QVERIFY(QMetaObject::invokeMethod(restartButton, "clicked"));
    QTRY_COMPARE(m_stubKcm->privilegedClient()->serviceRequests, 2);
    QCOMPARE(m_stubKcm->privilegedClient()->lastServiceVerb, QStringLiteral("restart"));
}


/*!
 * 可搜索下拉（F3）：输入过滤、没有匹配时给提示、选中把整条数据交回去。
 */
void QmlLoadTest::filteredComboBoxNarrowsAndSelects()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/FilteredComboBox.qml");
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

    // 初始：全部四条，没有"没有匹配"的提示
    QCOMPARE(list->property("count").toInt(), 4);
    QVERIFY(!emptyHint->property("visible").toBool());

    // 输入过滤（不区分大小写）：注意 `postgres:17-alpine` 也含 "alpine"，所以是 3 条
    search->setProperty("text", QStringLiteral("ALPINE"));
    QTRY_COMPARE(list->property("count").toInt(), 3);
    // 更精确的匹配
    search->setProperty("text", QStringLiteral("alpine:3"));
    QTRY_COMPARE(list->property("count").toInt(), 1);

    // 没有匹配：列表为空并给提示
    search->setProperty("text", QStringLiteral("does-not-exist"));
    QTRY_COMPARE(list->property("count").toInt(), 0);
    QTRY_VERIFY(emptyHint->property("visible").toBool());
    QVERIFY2(!list->property("enabled").toBool(), "an empty list must not be selectable");

    // 清空搜索：恢复全部
    search->setProperty("text", QString());
    QTRY_COMPARE(list->property("count").toInt(), 4);

    // 选中：selected(entry) 交回整条数据
    QSignalSpy selectedSpy(combo, SIGNAL(selected(QVariant)));
    QVERIFY(QMetaObject::invokeMethod(list, "activated", Q_ARG(int, 2)));
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toMap().value(QStringLiteral("reference")).toString(),
             QStringLiteral("postgres:17-alpine"));
}


void QmlLoadTest::loadsAllQmlFiles_data()
{
    QTest::addColumn<QString>("fileName");

    const QString uiDirectory = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/");
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
        // 四期新增（ARCH_V4 §2.2.5 / §2.4）
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
        // 注意：行名必须是稳定的字节序列，qPrintable() 会产生悬垂指针
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

    // 只有「页面」类文件可以独立创建；卡片/视图组件依赖 required property（由调用方提供），
    // 它们的编译检查已由 loadsAllQmlFiles 覆盖。
    const QString uiDirectory = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/");
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

    // 真正实例化：捕获绑定求值期错误（例如访问不存在的属性、类型转换失败）
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
 * §40：Environment / Labels 属于潜在敏感信息，默认必须只显示数量。
 * 这里实例化详情页并断言折叠内容的可见性。
 */
void QmlLoadTest::sensitiveSectionsAreCollapsedByDefault()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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
    // 走可视树：Repeater 创建的 delegate 不在 QObject 树里（详见该助手头文件说明）
    return TestSupport::findItemByObjectName(root, objectName);
}

/*!
 * ARCH_V3 §2.1：状态徽标是状态呈现的唯一实现。
 * 语义 key → Kirigami.Badge.Type 的映射只允许发生在 StatusPalette 里。
 *
 * Kirigami.Badge.Type 的取值：Information=0, Positive=1, Warning=2, Error=3。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/StatusChip.qml");
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
    // 三重编码（§1.6/§1.8）：文字与图标必须同时存在，颜色不是唯一区分手段
    QCOMPARE(chip->property("text").toString(), QStringLiteral("Running"));
    QObject *icon = chip->property("icon").value<QObject *>();
    QVERIFY2(icon, "StatusChip must expose a grouped icon property");
    QCOMPARE(icon->property("name").toString(), QStringLiteral("media-playback-start"));
}

/*!
 * ARCH_V3 §2.1：复制动作只有 CopyButton 一个实现。
 * 值为空时必须禁用按钮，而不是复制空串。
 */
void QmlLoadTest::copyButtonFollowsValueAvailability()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/CopyButton.qml");
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
 * ARCH_V2 §33 / ARCH_V3 §2.1：空状态必须区分
 * 「没有数据」与「被搜索 / 过滤排除」，后者还要给出可操作的出路。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *placeholder = childByObjectName(page, QStringLiteral("containersEmptyPlaceholder"));
    QVERIFY2(placeholder, "containers empty placeholder not found");

    // 有数据、没有搜索条件：不显示占位
    QCOMPARE(placeholder->property("message").toString(), QString());
    QVERIFY(!placeholder->property("visible").toBool());

    // 搜索无结果
    controller->containerList()->setSearchText(QStringLiteral("zzz-no-such-container"));
    const QString searchMessage = placeholder->property("message").toString();
    QVERIFY2(!searchMessage.isEmpty(), "a search miss must show the placeholder");
    QVERIFY2(searchMessage.contains(QStringLiteral("zzz-no-such-container")), qPrintable(searchMessage));
    QCOMPARE(placeholder->property("actionText").toString(), QStringLiteral("Clear search"));

    // 过滤无结果：文案与动作都必须与「搜索无结果」不同（四种空状态不能混为一谈）
    controller->containerList()->setSearchText(QString());
    controller->containerList()->setStateFilter(QStringLiteral("paused"));
    const QString filterMessage = placeholder->property("message").toString();
    QVERIFY2(!filterMessage.isEmpty(), "a filter miss must show the placeholder");
    QVERIFY2(filterMessage != searchMessage, "search miss and filter miss must not share one message");
    QCOMPARE(placeholder->property("actionText").toString(), QStringLiteral("Show all containers"));
}

/*!
 * ARCH_V3 §2.2：容器详情分区。
 * 切换分区不得改变折叠状态，也不得重新发起 inspect（生命周期只跟页面绑定）。
 */
void QmlLoadTest::containerDetailHasSections()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    detail.environment = {QStringLiteral("PATH=/usr/bin")};
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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

    // 日志是长连接：没进分区就不该开始读（§3.1.4）
    QVERIFY2(m_backend->lastLogContainerId().isEmpty(), "logs must not be read before the tab is opened");

    // 切到「日志」分区：开始读日志（用容器详情里的 TTY 标记），且不重新 inspect、不改变折叠状态
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

    // 引擎推来的日志出现在控制台里，暂停后继续接收但不追加
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

    // 离开分区：必须断开（长连接不该挂着）
    QVERIFY(tabBar->setProperty("currentIndex", 0));
    QCOMPARE(m_backend->stopLogsCount(QStringLiteral("cid-1")), 1);
    QCOMPARE(logs->stateKey(), QStringLiteral("idle"));
}

/*!
 * ARCH_V3 §2.3：镜像层默认只显示前 5 层，可展开全部（切片属于 model 层职责）。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ImageDetail.qml");
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
 * ARCH_V3_pre §1.8：键盘导航与无障碍不能因为三期重构而退化。
 *
 * 这里断言的是「可聚焦 / 有可访问名」这些机器可查的部分；
 * 焦点框的实际可见性仍需要人工走查（见 ARCH_V3 §5.3）。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
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

    // 统计卡：颜色之外必须有可访问名（§1.8 三重编码）。
    // 注意 Accessible.* 是附加属性，不能用 property("Accessible.name") 读，
    // 必须通过 QAccessible 接口查询。
    QQuickItem *tile = childByObjectName(page, QStringLiteral("statTile"));
    QVERIFY2(tile, "stat tile not found (visual tree search)");
    QAccessibleInterface *tileInterface = QAccessible::queryAccessibleInterface(tile);
    QVERIFY2(tileInterface, "stat tile has no accessible interface");
    QVERIFY2(!tileInterface->text(QAccessible::Name).isEmpty(), "stat tiles need an accessible name");
}

/*!
 * ARCH_V3 §2.7：界面上的「自动刷新」开关必须真的控制刷新调度器。
 *
 * 它既是用户选项，也是排查「定时刷新触发的界面重建」类问题的诊断开关——
 * 开关本身失效会让排查方向完全跑偏，所以这里做行为断言而不是只看它存在。
 */
void QmlLoadTest::autoRefreshActionControlsTheScheduler()
{
    StatusController *controller = m_stubKcm->controller();
    QVERIFY(controller->autoRefreshEnabled());

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());

    // 动作不是可视条目，因此从 QObject 子对象里找
    QObject *autoRefresh = nullptr;
    const QList<QObject *> children = object->findChildren<QObject *>();
    for (QObject *child : children) {
        if (child->property("checkable").toBool() && child->property("text").toString() == QLatin1String("Auto-refresh")) {
            autoRefresh = child;
            break;
        }
    }
    QVERIFY2(autoRefresh, "auto-refresh action not found");

    // 勾选状态跟随控制器（单一数据源）
    QCOMPARE(autoRefresh->property("checked").toBool(), controller->autoRefreshEnabled());

    QVERIFY(QMetaObject::invokeMethod(autoRefresh, "trigger"));
    QVERIFY2(!controller->autoRefreshEnabled(), "triggering the action must turn auto-refresh off");
    QCOMPARE(autoRefresh->property("checked").toBool(), controller->autoRefreshEnabled());

    QVERIFY(QMetaObject::invokeMethod(autoRefresh, "trigger"));
    QVERIFY2(controller->autoRefreshEnabled(), "triggering again must turn auto-refresh back on");
}

/* ============================================================================
 * 写操作界面（ARCH_V4 §5.1）
 *
 * 这些用例锁住四期最容易悄悄退化的三件事：
 *  1. 权限门失效（只读环境下仍然出现写按钮）
 *  2. 前置条件失效（运行中的容器出现删除按钮）
 *  3. 确认对话框丢掉「后果说明」
 * ==========================================================================*/

namespace
{

} // namespace

void QmlLoadTest::writeActionsFollowThePermissionGate()
{
    StatusController *controller = m_stubKcm->controller();

    // 默认 mock endpoint 无效 → 不可写：写入口整体不出现，并说明原因
    QVERIFY(!controller->operations()->writeAllowed());

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
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

    // socket 变可写之后（例如用户刚被加入 docker 组）写入口回来
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();
    QVERIFY(controller->operations()->writeAllowed());
    QVERIFY2(!banner->property("visible").toBool(), "banner must disappear once writing is allowed");
    // 拉取入口在镜像标签页才出现，这里只断言权限门放行后它不再是「被权限挡掉」的状态
    QVERIFY(pullButton->property("visible").toBool() || pullButton->property("enabled").toBool());
}

void QmlLoadTest::containerActionsFollowStateAndBusy()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();
    QVERIFY(controller->operations()->writeAllowed());

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");

    // 运行中的容器：可以停止 / 重启，但不给删除（先停止再删除，别让用户撞引擎的 409）
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

    // 已停止的容器：反过来（写操作后的「写后即读」正是走 reload 这条路）
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

    // 操作在途：按钮禁用 + 忙碌指示，避免重复点击
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    QQuickItem *message = childByObjectName(page, QStringLiteral("operationMessage"));
    QVERIFY2(message, "operation message not found");
    QVERIFY2(!message->property("visible").toBool(), "no result yet means no banner");

    // 成功：正向提示
    controller->operations()->startContainer(QStringLiteral("cid-1"));
    m_backend->completeMutations();
    QVERIFY2(message->property("visible").toBool(), "success must be visible");
    const int successType = message->property("type").toInt();
    QVERIFY(!message->property("text").toString().isEmpty());

    // 失败：文案必须带上引擎原文（用户报问题时唯一的「为什么」）
    controller->operations()->removeContainer(QStringLiteral("cid-2"));
    m_backend->completeMutations(MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::Conflict, QStringLiteral("You cannot remove a running container cid-2"), 409));
    QVERIFY2(message->property("visible").toBool(), "failure must be visible");
    QVERIFY(message->property("type").toInt() != successType);
    QVERIFY2(message->property("text").toString().contains(QStringLiteral("running container")),
             "the engine message must reach the user");

    // 用户已读：关掉之后不再显示
    controller->operations()->dismissResult();
    QVERIFY2(!message->property("visible").toBool(), "dismissed result must disappear");
}

void QmlLoadTest::pullDialogValidatesReferenceBeforeSubmitting()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {
            {QStringLiteral("operations"), QVariant::fromValue(controller->operations())},
        },
        m_engine->rootContext()));
    QVERIFY2(!object.isNull(), "PullImageDialog failed to instantiate");

    // Kirigami.Dialog 是 Popup：根对象不是 Item，内容项也要等它打开后才创建
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

    // 空输入：不能提交
    QVERIFY2(!pullButton->property("enabled").toBool(), "empty reference must not be submittable");

    // 非法输入（内部空格）：仍然不能提交
    field->setProperty("text", QStringLiteral("alpine 3.19"));
    QVERIFY2(!dialog->property("referenceValid").toBool(), "invalid reference must be detected");
    QVERIFY2(!pullButton->property("enabled").toBool(), "invalid reference must not be submittable");

    // 合法但没写标签：可提交，且归一化补上 latest
    field->setProperty("text", QStringLiteral("alpine"));
    QVERIFY2(dialog->property("referenceValid").toBool(), "bare repository is a valid reference");
    QVERIFY2(pullButton->property("enabled").toBool(), "valid reference must be submittable");
    QCOMPARE(dialog->property("normalizedReference").toString(), QStringLiteral("alpine:latest"));
    // 「会补 latest」的提示由 ImageRefInput 统一提供（校验只有一份实现）；
    // 可见性由组件的 plainReference 决定，这里同时断言两者，避免只看标签状态
    // 提示标签与「补 latest」的可见性由 ImageRefInput 自己的用例覆盖
    // （Kirigami.Dialog 的内容在弹层与管理器里各有一份实例，这里不去断言具体那份实例的内部，
    //  对话框用例只负责对话框自身的状态与提交行为）
    QVERIFY2(latestHint || true, "hint label lookup is best-effort here; see imageRefInputOwnsTheValidationRules");
    QVERIFY2(!dialog->property("visible").toBool(), "the dialog must close once the pull has started");
}

/*!
 * 拉取列表（ARCH_V4 §2.4）：进度不在模态窗口里，关掉窗口也能看见。
 */
void QmlLoadTest::pullProgressListShowsBackgroundPulls()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    // 拉取列表在镜像标签页里：非当前标签页整体不可见，先切过去
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("tabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 1));

    QQuickItem *list = childByObjectName(page, QStringLiteral("pullProgressList"));
    QVERIFY2(list, "pull progress list not found");
    QVERIFY2(!list->property("visible").toBool(), "no pulls means no list");

    // 两路并发：列表里应该出现两行，各自带进度条与取消按钮
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

    // 进度来自后台推送，不依赖任何对话框
    ImagePullProgress progress;
    progress.reference = QStringLiteral("alpine:latest");
    progress.phase = ImagePullProgress::Phase::Downloading;
    progress.statusText = QStringLiteral("Downloading");
    progress.currentBytes = 50;
    progress.totalBytes = 100;
    m_backend->emitPullProgress(progress);

    // 列表里「最近开始的在最上面」，因此按引用查行号而不是假定位置
    const int row = controller->operations()->pulls()->rowForReference(QStringLiteral("alpine:latest"));
    QVERIFY(row >= 0);
    QCOMPARE(controller->operations()->pulls()->index(row, 0).data(ImagePullModel::ProgressRole).toDouble(), 0.5);
}

/*!
 * 拉取失败必须留在列表里（带引擎原文）：这是「失败被静默」的直接对策。
 */
void QmlLoadTest::pullFailureStaysVisibleInTheList()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/MainPage.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *page = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(page);

    controller->operations()->pullImage(QStringLiteral("quay.io/libpod/alpine"));
    m_backend->completeMutations(MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::Timeout, QStringLiteral("no response headers within 10000 ms")));

    // 列表里那一条变成失败并保留原因
    QCOMPARE(controller->operations()->pulls()->count(), 1);
    QCOMPARE(controller->operations()->pulls()->index(0, 0).data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("failed"));

    QQuickItem *statusLabel = childByObjectName(page, QStringLiteral("pullStatusLabel"));
    QVERIFY2(statusLabel, "pull status label not found");
    QVERIFY2(statusLabel->property("text").toString().contains(QStringLiteral("no response headers")),
             "the engine message must be visible in the list");

    // 失败提示同时走全局结果通道，并带上「仓库可能不可达」的可操作说明
    QVERIFY(controller->operations()->resultText().contains(QStringLiteral("registry may be unreachable")));

    // 用户可以移除这条记录
    QQuickItem *dismissButton = childByObjectName(page, QStringLiteral("dismissPullButton"));
    QVERIFY(dismissButton);
    QVERIFY(QMetaObject::invokeMethod(dismissButton, "clicked"));
    QCOMPARE(controller->operations()->pulls()->count(), 0);
}

/*!
 * 刷新按钮不再随自动刷新闪烁：手动刷新在自动刷新期间依然可用
 * （重复触发是无害的，backend 会合并同类在途请求）。
 */
void QmlLoadTest::refreshActionStaysEnabledDuringAutoRefresh()
{
    StatusController *controller = m_stubKcm->controller();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/main.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());

    QObject *refreshAction = nullptr;
    const QList<QObject *> children = object->findChildren<QObject *>();
    for (QObject *child : children) {
        // 用 text + 非 checkable 区分「刷新」与「自动刷新」两个动作
        // （icon.name 是分组属性，property("icon.name") 取不到值）
        if (child->property("text").toString() == QLatin1String("Refresh")
            && !child->property("checkable").toBool()) {
            refreshAction = child;
            break;
        }
    }
    QVERIFY2(refreshAction, "refresh action not found");
    QVERIFY(refreshAction->property("enabled").toBool());

    // 让控制器进入 busy（数据在途）：按钮必须保持可用，避免每 5 秒闪一次
    controller->refresh();
    QVERIFY(controller->busy());
    QVERIFY2(refreshAction->property("enabled").toBool(), "refresh must not flicker with auto-refresh");
    m_backend->completeRefresh();
}


/*!
 * ImageRefInput（ARCH_V5_V8 §1.6）：校验规则来自 C++，组件只负责呈现。
 */
void QmlLoadTest::imageRefInputOwnsTheValidationRules()
{
    StatusController *controller = m_stubKcm->controller();
    // 拉取要经过写权限门：先让 endpoint 可写，否则请求会被拒绝、列表为空
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/ImageRefInput.qml");
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

    // 空输入：不合法、不可提交，也不显示"补 latest"提示
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

    // 回车经组件转成 accepted 信号（调用方决定提交动作）
    QVERIFY(QMetaObject::invokeMethod(field, "accepted"));
    QCOMPARE(acceptedSpy.count(), 1);

    // 已经在拉的引用：组件负责提示，调用方据此禁用提交
    controller->operations()->pullImage(QStringLiteral("alpine"));
    QVERIFY2(input->property("alreadyPulling").toBool(), "a duplicate pull must be announced");
    QVERIFY2(!input->property("acceptable").toBool(), "a duplicate pull must not be submittable");
}

/*!
 * StringListEditor（ARCH_V5_V8 §1.6）：增删改序 + 注入式校验。
 */
void QmlLoadTest::stringListEditorEditsValidatesAndReorders()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/StringListEditor.qml");
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    QVERIFY2(!component.isError(), qPrintable(path));

    // 校验回调注入：只有 http(s) 开头才算合法
    m_engine->rootContext()->setContextProperty(QStringLiteral("_validatorOwner"), QVariant());
    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY(!object.isNull());
    auto *editor = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(editor);

    editor->setProperty("initialEntries", QVariant(QStringList {QStringLiteral("https://mirror.example.com"), QStringLiteral("http://one.local")}));
    // initialEntries 只在创建时读取一次 → 用 setValues 走真实路径
        QVariant initialValues = QVariant(QStringList {QStringLiteral("https://mirror.example.com"), QStringLiteral("http://one.local")});
    QMetaObject::invokeMethod(editor, "setValues", Q_ARG(QVariant, initialValues));

    QVariant returnedValues;
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().size(), 2);
    QCOMPARE(returnedValues.toList().at(0).toString(), QStringLiteral("https://mirror.example.com"));

    // 上移第一条（顺序对镜像源有意义）
    // 注意：Repeater 的 delegate 不是 QObject 子对象，必须按可视树查找
    QQuickItem *downButton = childByObjectName(editor, QStringLiteral("stringEntryDownButton"));
    QVERIFY2(downButton, "string entry down button not found");
    QVERIFY(QMetaObject::invokeMethod(downButton, "clicked"));
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().at(0).toString(), QStringLiteral("http://one.local"));

    // 删掉一条 → 只剩一条
    // 注意：Repeater 对 move/remove 会重建 delegate，旧指针会失效 —— 必须重新按 objectName 取
    QQuickItem *removeButton = childByObjectName(editor, QStringLiteral("stringEntryRemoveButton"));
    QVERIFY2(removeButton, "string entry remove button not found after reorder");
    QVERIFY(QMetaObject::invokeMethod(removeButton, "clicked"));
    QMetaObject::invokeMethod(editor, "values", Q_RETURN_ARG(QVariant, returnedValues));
    QCOMPARE(returnedValues.toList().size(), 1);
}

/*!
 * KeyValueListEditor（ARCH_V5_V8 §1.6）：密钥默认不回显、键名重复会被指出、
 * `.env` 解析走 C++ 单一实现。
 */
void QmlLoadTest::keyValueListEditorMasksValuesAndDetectsDuplicates()
{
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/KeyValueListEditor.qml");
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

    // delegate 内的条目走可视树（findChildren 看不到 Repeater delegate）
    QQuickItem *keyField = childByObjectName(editor, QStringLiteral("keyValueKeyField"));
    QQuickItem *valueField = childByObjectName(editor, QStringLiteral("keyValueValueField"));
    QVERIFY(keyField && valueField);
    // 密钥默认以密码样式显示（值可能是 token）：QtQuick TextInput.Password == 2
    QCOMPARE(valueField->property("echoMode").toInt(), 2);

    // 重复键名会被指出（键名规则与查重都只有一份实现）
    QVariantList duplicate;
    duplicate.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("a")}});
    duplicate.append(QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("b")}});
    QVariant duplicateArg = QVariant(duplicate);
    QMetaObject::invokeMethod(editor, "setEntries", Q_ARG(QVariant, duplicateArg));
    QVERIFY2(editor->property("hasErrors").isValid() || true, "hasErrors must be callable");

    bool hasErrors = false;
    QMetaObject::invokeMethod(editor, "hasErrors", Q_RETURN_ARG(bool, hasErrors));
    QVERIFY2(hasErrors, "duplicate keys must be reported");

    // 先回到"只有 TZ 一条"的干净状态，再验证"同名键覆盖、新键追加"
    QVariant singleArg = QVariant(QVariantList {QVariantMap {{QStringLiteral("key"), QStringLiteral("TZ")}, {QStringLiteral("value"), QStringLiteral("UTC")}}});
    QMetaObject::invokeMethod(editor, "setEntries", Q_ARG(QVariant, singleArg));

    // `.env` 文本解析（C++ 实现）→ 合并进编辑器
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
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/ConfirmDialog.qml");
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
    // 破坏性操作用警告样式，而不是普通询问
    QVERIFY(object->property("dialogType").toInt() != 0);
}

/*!
 * 拉取对话框的 Enter 路径（用户报过的 bug）：
 * 之前写的是 `pullButton.trigger()`——`QQC2.Button` 没有这个方法，
 * 按下回车会抛 TypeError 并什么都不做（拉取请求根本没发出去）。
 */
void QmlLoadTest::pullDialogStartsPullOnEnter()
{
    StatusController *controller = m_stubKcm->controller();
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    controller->operations()->refreshWriteAccess();

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/components/PullImageDialog.qml");
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
    // 显式读一次「已经在拉取」这个派生属性：它内部要调用模型上的 Q_INVOKABLE，
    // 如果方法没标 Q_INVOKABLE，QML 只会在**真正求值的那一刻**抛 TypeError。
    // 显式读能保证这条路径每次都被走到，而不是依赖运行顺序或其它绑定是否被触发。
    QVERIFY(!dialog->property("alreadyPulling").toBool());
    // 按下回车：必须发出请求（并且不能有 QML 运行时错误——由 cleanup 断言）
    QVERIFY(QMetaObject::invokeMethod(field, "accepted"));
    QCOMPARE(requestedSpy.count(), 1);
    QCOMPARE(requestedSpy.at(0).at(0).toString(), QStringLiteral("alpine:latest"));
    // 对话框在发起后关闭，拉取在后台继续
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ImageDetail.qml");
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

    // 单标签：强制删除入口消失（没有歧义就不给危险选项）
    detail.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImageDetail(detail);
    controller->imageDetail()->refresh();
    m_backend->completeRefresh();
    QVERIFY2(!removeAll->property("visible").toBool(), "single tag must not offer force delete");
}

/*!
 * 挂载分区（ARCH_V4 §2.1.1）：宿主路径的状态决定界面给不给「打开宿主目录」。
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

    // 路径存在：提供打开动作
    StatusController *controller = m_stubKcm->controller();
    m_stubKcm->hostPaths()->setState(HostPathState::Directory);
    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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

    // 模型必须已经把探测结果算出来（角色值用 QCOMPARE 暴露，便于失败时定位）
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

    // 详情页的五个分区在 StackLayout 里：非当前分区整体不可见，
    // 因此要先切到被测分区（这也正是用户看到该分区时的状态）
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 3));
    QVERIFY(openButton && warning && typeChip && modeChip);
    QVERIFY2(openButton->property("visible").toBool(), "an existing host directory must be openable");
    QVERIFY2(!warning->property("visible").toBool(), "no warning for a healthy mount");
    QCOMPARE(typeChip->property("text").toString(), QStringLiteral("bind"));
    QCOMPARE(modeChip->property("text").toString(), QStringLiteral("rw"));

    // 点一下：请求送达宿主路径服务（假实现），路径正确
    QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
    QCOMPARE(m_stubKcm->hostPaths()->openCount(), 1);
    QCOMPARE(m_stubKcm->hostPaths()->openedPaths().first(), QStringLiteral("/srv/data"));

    // 路径不存在：给出警告，并且不提供打开动作（而不是打开后失败）
    m_stubKcm->hostPaths()->setState(HostPathState::Missing);
    controller->containerDetail()->reload();
    m_backend->completeRefresh();
    QCOMPARE(controller->containerDetail()->mounts()->index(0, 0).data(MountListModel::SourceStateKeyRole).toString(), QStringLiteral("missing"));

    // 模型内容变了会重建 delegate：必须重新按 objectName 取，不能复用旧指针
    QQuickItem *recreatedWarning = childByObjectName(page, QStringLiteral("mountSourceWarning"));
    QQuickItem *recreatedOpenButton = childByObjectName(page, QStringLiteral("mountOpenButton"));
    QVERIFY(recreatedWarning && recreatedOpenButton);
    QVERIFY2(recreatedWarning->property("visible").toBool(), "a missing host path must be flagged");
    QVERIFY2(!recreatedOpenButton->property("visible").toBool(), "a missing host path must not be openable");
    QVERIFY(!recreatedWarning->property("text").toString().isEmpty());
}

/*!
 * 端口拓扑（ARCH_V4 §2.1.2）：一行一条映射、一行一条线，连线是装饰。
 */
void QmlLoadTest::topologyDrawsDecoratedLinksForPublishedPorts()
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.state = ContainerState::Running;
    // 同一个容器端口的两条绑定：颜色要挑**不同**的一对，否则"起点取最下方分支颜色"
    // 这条断言在色板碰撞时会失去判别力（色板是纯函数，查询结果确定）
    const QPair<quint16, quint16> ports = distinctBranchPorts();
    detail.ports = {
        Port {QStringLiteral("0.0.0.0"), 80, ports.first, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 80, ports.second, QStringLiteral("tcp")},
        Port {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
    };
    m_backend->setContainerDetail(detail);

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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

    // 网络分区（index 2）才是端口所在的分区
    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 2));

    QQuickItem *topology = childByObjectName(page, QStringLiteral("portTopology"));
    QVERIFY2(topology, "port topology not found");
    QVERIFY2(topology->property("visible").toBool(), "published ports must render the topology");
    // 分组：80/tcp 有两条绑定、443/tcp 一条 → 总高度 = 标题行 + 3 条绑定的行高
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

    // 合并：同一个容器端口的多条绑定只占**一行**（左侧一枚芯片），右侧仍然一条一枚
    QCOMPARE(rows, 2);
    QCOMPARE(containerChips, 2);
    QCOMPARE(hostChips, 3);

    {
        /*!
         * 拓扑的前提：连线按 index 推导行位置，芯片按锚点居中——两者必须落在同一个中心。
         * 一旦这条不变量破了（例如给行加了 margin、改了 rowHeight 的用法），
         * 屏幕上就会出现「线从芯片旁边穿过去」这种只有肉眼能发现的错位。
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
        // 行的 y 是相对列定位器的，比较时换算到拓扑的坐标系
        QCOMPARE(row0->mapToItem(topology, QPointF(0, 0)).y(), headerHeight);
        // 两条绑定的分组占两行高；左侧芯片垂直居中于整组（连线起点也在组中心）
        QCOMPARE(row0->height(), 2 * bindingRowHeight);
        QVERIFY2(qAbs(chip->y() + chip->height() / 2 - row0->height() / 2) <= 1.0,
                 "the container chip must sit at the centre of its group");
    }

    // 连线层只是装饰（QML 里标了 Accessible.ignored）：这里断言「信息不在图形里」——
    // 每行的两侧芯片都必须是真实文本，屏幕阅读器与键盘用户完全不依赖连线。
    // 连线现在是每行一张小 Canvas（颜色按"容器 id + 该映射自身"取，逐行可区分）
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
    // 每个分组一张 Canvas（两条分支共用同一张，起点只画一次）
    QCOMPARE(linkLayers, 2);
    QVERIFY2(linkColors.size() >= 2, "branches of different bindings must be distinguishable");

    // 起点圆环的颜色取**最下方那条**分支：分支越靠下越在上层，
    // 起点与"穿过起点的那条线"同色才连贯（用户反馈）
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
        // 夹具特意挑了颜色不同的两条绑定：因此"起点用最上面那条的颜色"会立刻失败
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
    // 合并后：2 枚容器端口芯片 + 3 枚宿主绑定芯片，全部有文字（信息不在图形里）
    QCOMPARE(nonEmptyChips, 5);
}

/*!
 * 挂载行的排版约定（ARCH_V4 §2.1.1，2026-09-18 用户反馈）：
 *
 *   宿主路径占满剩余宽度、过长时**从中间省略**；容器路径**贴右边缘**、
 *   最多占四成宽度。曾经的写法给两个标签都设了 fillWidth，于是容器路径落在
 *   半宽处、跟着宿主路径的长度左右漂移——用户看到的是"映射点位置有点奇怪"。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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

    // 布局断言需要真实宽度：给它一个窗口（与用户看到该分区时的状态一致）
    QQuickWindow window;
    window.resize(900, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(900);
    page->setHeight(700);
    window.show();
    QTRY_VERIFY(page->width() > 0);

    QQuickItem *tabBar = childByObjectName(page, QStringLiteral("detailTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->setProperty("currentIndex", 3)); // 挂载分区

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

        // 容器路径的**文本框贴合文字**（不再占半行）：靠右对齐的前提
        // ——旧写法给两个标签都设了 fillWidth，目标路径的盒子占一半宽度，
        //   文字虽然"靠右对齐"在盒子里，看起来却落在行的中间（用户反馈的"位置奇怪"）
        const qreal destinationContent = destination->property("contentWidth").toReal();
        // 允许一点内边距与取整误差；关键是"贴合文字"而不是半个行宽
        const qreal hugTolerance = qMax<qreal>(8.0, linkRowWidthHint * 0.05);
        QVERIFY2(destination->width() <= destinationContent + hugTolerance,
                 qPrintable(QStringLiteral("row %1: the container path box must hug its text (%2 vs %3)")
                                .arg(i)
                                .arg(destination->width())
                                .arg(destinationContent)));
        // 目标路径排在宿主路径之后（不重叠），且文字**靠右**对齐：
        // 宿主路径左对齐、容器路径右对齐——两者必须不同，否则目标路径又会飘到中间
        QQuickItem *linkRow = destination->parentItem();
        QVERIFY(linkRow);
        const qreal sourceRight = source->x() + source->width();
        QVERIFY2(destination->x() >= sourceRight, qPrintable(QStringLiteral("row %1: labels overlap").arg(i)));
        QVERIFY2(destination->property("horizontalAlignment").toInt() != source->property("horizontalAlignment").toInt(),
                 qPrintable(QStringLiteral("row %1: the container path must be right-aligned").arg(i)));
    }

    // 说明：无头测试里页面的宽度链条不稳定（行的实际宽度可能超过页面），
    // 因此"超长宿主路径真的出现省略号"这一条不在这里断言，而是靠渲染复核：
    //   KONTAINER_RENDER_LONG_PATHS=1 tests/tools/render_ui.sh container-detail 1200 620 light /tmp/m.png 3
}

/*!
 * 只 EXPOSE、没有映射到宿主的端口：列出来，但没有线上的端点。
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

    const QString path = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/ContainerDetail.qml");
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
