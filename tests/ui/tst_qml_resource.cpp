/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Load the UI from **qrc** (ARCH_V3 §5.1).
 *
 * Division of labour with tst_qml_load:
 *  - tst_qml_load reads the .qml files from the source directory and checks syntax,
 *    bindings and interactive behaviour;
 *  - this test packs the same files (same manifest, KCM_DOCKER_QML_FILES in the
 *    top-level CMakeLists.txt) into qrc and loads them at the plugin's runtime path
 *    `qrc:/kcm/kcm_docker/main.qml`.
 *
 * Why it exists: a missing entry in the resource manifest, a qmldir / singleton that
 * fails to resolve under qrc, and a wrong resource prefix are all invisible to the
 * source-directory test, yet each makes the installed KCM fail to open
 * (kcmshell6 shows only an error page; --smoke-test only returns a non-zero exit code).
 * Measured lesson: ChartPalette.qml / StatusPalette.qml were once missing from the
 * resource manifest, the source-directory test was 15/15 green, and
 * `kcmshell6 --smoke-test` exited with 1.
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
    return QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui");
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
 * The plugin entry point: KQuickConfigModule::mainUi() loads the UI from this URL.
 * Failure here = the installed KCM does not open.
 */
void QmlResourceTest::mainUiLoadsFromResource()
{
    QQmlComponent component(m_engine.get(), QUrl(QStringLiteral("qrc:/kcm/kcm_docker/main.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QScopedPointer<QObject> object(component.create(m_engine->rootContext()));
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
}

/*!
 * Every UI file in the source directory (including qmldir) must have a **byte-identical**
 * counterpart in the resource.
 *
 * The expectations are scanned from the source directory; there is no second manifest:
 * forgetting to add a new component to KCM_DOCKER_QML_FILES fails here immediately
 * instead of after installation.
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
    // 0 files scanned means the test itself is broken (bad path etc.); fail explicitly
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
                                       "(add it to KCM_DOCKER_QML_FILES in the top-level CMakeLists.txt)")
                            .arg(relativePath)));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    QCOMPARE(resource.readAll(), expected);
}

/*!
 * Singletons must resolve from qrc: after `import "components" as Components`,
 * Components.StatusPalette / Components.ChartPalette must be usable and return theme colors.
 *
 * This snippet depends on no page, so when a page fails to load it points straight at
 * singleton resolution (instead of leaving main.qml to report "Type MainPage unavailable").
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

    // The color itself follows the theme; what matters is that it resolves and has a sane value
    QVERIFY(object->property("statusPositive").value<QColor>().isValid());
    QCOMPARE(object->property("badgeType").toInt(), 3); // Error is used for negative
    QVERIFY(object->property("cpuSeries").value<QColor>().isValid());
    QCOMPARE(object->property("contrast").toReal(), 21.0);
}

QTEST_MAIN(QmlResourceTest)

#include "tst_qml_resource.moc"
