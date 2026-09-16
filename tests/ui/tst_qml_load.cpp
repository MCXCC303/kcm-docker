/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/qml_registration.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

using namespace Kontainer;

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
        "function i18n(text) { return text; }\n"
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

    // 统计卡：颜色之外必须有可访问名（§1.8 三重编码）
    QQuickItem *tile = childByObjectName(page, QStringLiteral("statTile"));
    if (tile) {
        const QString accessibleName = tile->property("Accessible.name").toString();
        QVERIFY2(!accessibleName.isEmpty(), "stat tiles need an accessible name");
    }
}

QTEST_MAIN(QmlLoadTest)
#include "tst_qml_load.moc"
