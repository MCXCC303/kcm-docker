/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/image_pull_model.h"
#include "model/qml_registration.h"
#include "support/qml_item_utils.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <QQmlComponent>
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
    void imageDetailOffersForceDeleteOnlyForMultipleTags();
    void mountRowReflectsHostPathState();
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
    g_previousHandler = qInstallMessageHandler(&QmlLoadTest::captureMessages);
}

void QmlLoadTest::init()
{
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
        QStringLiteral("components/FieldChip.qml"),
        QStringLiteral("components/PortTopology.qml"),
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
    QCOMPARE(tabBar->property("count").toInt(), 5);

    QQuickItem *stack = childByObjectName(page, QStringLiteral("detailSectionStack"));
    QVERIFY2(stack, "detail section stack not found");
    QCOMPARE(stack->property("currentIndex").toInt(), 0);

    QQuickItem *environment = childByObjectName(page, QStringLiteral("environmentValues"));
    QVERIFY2(environment, "environment values container not found");
    QVERIFY2(!environment->isVisible(), "environment must stay collapsed (§40)");

    // 切到「日志」分区：占位必须可见，且不重新 inspect、不改变折叠状态
    QVERIFY(tabBar->setProperty("currentIndex", 4));
    QCOMPARE(stack->property("currentIndex").toInt(), 4);
    QQuickItem *logsPlaceholder = childByObjectName(page, QStringLiteral("logsPlaceholder"));
    QVERIFY2(logsPlaceholder, "logs placeholder not found");
    QVERIFY2(!logsPlaceholder->property("message").toString().isEmpty(), "logs tab must explain that logs are not implemented yet");
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::ContainerDetail), 1);
    QVERIFY2(!environment->isVisible(), "switching sections must not expand environment (§40)");
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
    for (QQuickItem *item : items) {
        if (item->objectName() == QLatin1String("pullReferenceField")) {
            field = item;
        } else if (item->objectName() == QLatin1String("pullImageButton")) {
            pullButton = item;
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
    QVERIFY2(dialog->property("plainReference").toBool(), "the implicit latest tag must be announced");
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
    detail.ports = {
        Port {QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
        Port {QStringLiteral("127.0.0.1"), 80, 8081, QStringLiteral("tcp")},
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
    QCOMPARE(topology->property("implicitHeight").toReal(), topology->property("headerHeight").toReal() + 3 * topology->property("rowHeight").toReal());

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

    // 一条映射 = 一行 = 两侧各一枚芯片（两个宿主地址属于同一个容器端口 → 两行）
    QCOMPARE(rows, 3);
    QCOMPARE(containerChips, 3);
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
        const qreal rowHeight = topology->property("rowHeight").toReal();
        QCOMPARE(row0->y(), headerHeight);
        QCOMPARE(row0->height(), rowHeight);
        // 允许 1px 的取整误差：连线画在 rowHeight / 2 上
        QVERIFY(qAbs(chip->y() + chip->height() / 2 - rowHeight / 2) <= 1.0);
    }

    // 连线层只是装饰（QML 里标了 Accessible.ignored）：这里断言「信息不在图形里」——
    // 每行的两侧芯片都必须是真实文本，屏幕阅读器与键盘用户完全不依赖连线
    QQuickItem *links = childByObjectName(page, QStringLiteral("portTopologyLinks"));
    QVERIFY2(links, "link layer not found");
    QCOMPARE(links->width(), topology->width());
    QCOMPARE(links->height(), topology->height());

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
    QCOMPARE(nonEmptyChips, 6);
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
        if (item->objectName() == QLatin1String("pullReferenceField")) {
            field = item;
        }
    }
    QVERIFY2(field, "pull reference field not found");

    QSignalSpy requestedSpy(dialog, SIGNAL(pullRequested(QString)));
    field->setProperty("text", QStringLiteral("alpine"));
    // 按下回车：必须发出请求（并且不能有 QML 运行时错误——由 cleanup 断言）
    QVERIFY(QMetaObject::invokeMethod(field, "accepted"));
    QCOMPARE(requestedSpy.count(), 1);
    QCOMPARE(requestedSpy.at(0).at(0).toString(), QStringLiteral("alpine:latest"));
    // 对话框在发起后关闭，拉取在后台继续
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

QTEST_MAIN(QmlLoadTest)
#include "tst_qml_load.moc"
