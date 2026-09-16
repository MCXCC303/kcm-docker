/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/qml_registration.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <QDir>
#include <QDirIterator>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

using namespace Kontainer;

/*!
 * 从 **qrc** 加载界面（ARCH_V3 §5.1）。
 *
 * 与 tst_qml_load 的分工：
 *  - tst_qml_load 读源码目录里的 .qml，检查语法、绑定与交互行为；
 *  - 本测试把同一份文件（同一份清单，见顶层 CMakeLists.txt 的
 *    KONTAINER_QML_FILES）打进 qrc，再按插件运行时的路径
 *    `qrc:/kcm/kcm_docker/main.qml` 加载。
 *
 * 存在的理由：资源清单漏项、qmldir / 单例在 qrc 下解析失败、资源前缀写错
 * 这三类问题都不会被源码目录测试发现，却会让安装后的 KCM 直接打不开
 * （kcmshell6 只显示一个错误页，--smoke-test 只给一个非零退出码）。
 * 实测教训：ChartPalette.qml / StatusPalette.qml 曾经漏出资源清单，
 * 源码目录测试 15/15 全绿，而 `kcmshell6 --smoke-test` 退出码是 1。
 */
class QmlResourceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void mainUiLoadsFromResource();
    void everySourceFileIsInTheResource_data();
    void everySourceFileIsInTheResource();
    void singletonPalettesResolveFromResource();

private:
    static QString sourceUiDir();

    std::unique_ptr<MockDockerBackend> m_backend;
    std::unique_ptr<QmlStubKcm> m_stubKcm;
    std::unique_ptr<QQmlEngine> m_engine;
};

QString QmlResourceTest::sourceUiDir()
{
    return QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui");
}

void QmlResourceTest::initTestCase()
{
    setupTranslationDomain();
    registerKontainerQmlTypes();
}

void QmlResourceTest::init()
{
    m_backend = std::make_unique<MockDockerBackend>();
    m_stubKcm = std::make_unique<QmlStubKcm>(m_backend.get());
    m_engine = std::make_unique<QQmlEngine>();
    m_engine->evaluate(QStringLiteral("function i18n(text) { return text; }\n"
                                      "function i18nc(context, text) { return text; }\n"
                                      "function i18np(singular, plural, count) { return count === 1 ? singular : plural; }\n"
                                      "function i18ncp(context, singular, plural, count) { return count === 1 ? singular : plural; }\n"));
    m_engine->rootContext()->setContextProperty(QStringLiteral("kcm"), m_stubKcm.get());
}

void QmlResourceTest::cleanup()
{
    m_engine.reset();
    m_stubKcm.reset();
    m_backend.reset();
}

/*!
 * 插件的入口：KQuickConfigModule::mainUi() 就是从这个 URL 加载界面。
 * 这里失败 = 安装后的 KCM 打不开。
 */
void QmlResourceTest::mainUiLoadsFromResource()
{
    QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc:/kcm/kcm_docker/main.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
}

/*!
 * 源码目录里的每个界面文件（含 qmldir）都必须能在资源里找到**完全相同**的内容。
 *
 * 期望值直接从源码目录扫描得到，不维护第二份清单：
 * 新增组件时忘了加进 KONTAINER_QML_FILES 会立刻在这里失败，
 * 而不是等到安装之后。
 */
void QmlResourceTest::everySourceFileIsInTheResource_data()
{
    QTest::addColumn<QString>("relativePath");

    const QDir root(sourceUiDir());
    QDirIterator iterator(root.absolutePath(), {QStringLiteral("*.qml"), QStringLiteral("qmldir")}, QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = root.relativeFilePath(absolute);
        const QByteArray rowName = relative.toUtf8();
        QTest::newRow(rowName.constData()) << relative;
        ++count;
    }
    // 扫描到 0 个文件说明测试自身失效了（路径写错等），必须显式失败
    QVERIFY2(count >= 15, qPrintable(QStringLiteral("only %1 interface files found under %2").arg(count).arg(root.absolutePath())));
}

void QmlResourceTest::everySourceFileIsInTheResource()
{
    QFETCH(QString, relativePath);

    QFile source(sourceUiDir() + QLatin1Char('/') + relativePath);
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(relativePath));
    const QByteArray expected = source.readAll();

    QFile resource(QStringLiteral(":/kcm/kcm_docker/") + relativePath);
    QVERIFY2(resource.exists(),
             qPrintable(QStringLiteral("%1 is missing from the QML resource list "
                                       "(add it to KONTAINER_QML_FILES in the top-level CMakeLists.txt)")
                            .arg(relativePath)));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    QCOMPARE(resource.readAll(), expected);
}

/*!
 * 单例必须能从 qrc 解析：`import "components" as Components` 之后
 * Components.StatusPalette / Components.ChartPalette 可用且能取到主题色。
 *
 * 这段片段不依赖任何具体页面，因此页面加载失败时它能直接指出
 * 问题出在单例解析上（而不是让 main.qml 报一句 "Type MainPage unavailable"）。
 */
void QmlResourceTest::singletonPalettesResolveFromResource()
{
    const QByteArray qml = R"(
        import QtQuick
        import "qrc:/kcm/kcm_docker/components" as Components

        QtObject {
            property color statusPositive: Components.StatusPalette.color("positive")
            property int badgeType: Components.StatusPalette.badgeType("negative")
            property color cpuSeries: Components.ChartPalette.cpuSeries
            property real contrast: Components.ChartPalette.contrastRatio("#ffffff", "#000000")
        }
    )";

    QQmlComponent component(m_engine.get());
    component.setData(qml, QUrl(QStringLiteral("qrc:/kcm/kcm_docker/singleton-probe.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QScopedPointer<QObject> object(component.create());
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));

    // 颜色值本身随主题变化，重要的是解析成功且有合理取值
    QVERIFY(object->property("statusPositive").value<QColor>().isValid());
    QCOMPARE(object->property("badgeType").toInt(), 3); // Error 用于 negative
    QVERIFY(object->property("cpuSeries").value<QColor>().isValid());
    QCOMPARE(object->property("contrast").toReal(), 21.0);
}

QTEST_MAIN(QmlResourceTest)

#include "tst_qml_resource.moc"
