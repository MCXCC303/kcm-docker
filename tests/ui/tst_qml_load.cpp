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

private:
    static void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message);
    void failOnQmlRuntimeErrors();

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
    m_engine->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                      "function i18nc(context, text) { return text; }\n"
                                      "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                      "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
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

QTEST_MAIN(QmlLoadTest)

#include "tst_qml_load.moc"
