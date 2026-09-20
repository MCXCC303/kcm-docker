/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/image_reference.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * Image reference parsing (ARCH_V4 §2.4 / §5.1).
 *
 * Two consumers: the backend builds `fromImage` / `tag` from it, QML pre-validates with it.
 * So these cases cover both sides, what must be accepted and what must be rejected.
 */
class ImageReferenceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acceptsCommonForms_data();
    void acceptsCommonForms();
    void rejectsInvalidInput_data();
    void rejectsInvalidInput();
    void registryPortIsNotATag();
    void digestSkipsTagNormalisation();
    void shortFormDropsRegistryOnly();
    void trimsSurroundingWhitespace();
};

void ImageReferenceTest::acceptsCommonForms_data()
{
    QTest::addColumn<QString>("reference");
    QTest::addColumn<QString>("repository");
    QTest::addColumn<QString>("tag");
    QTest::addColumn<QString>("registry");

    QTest::newRow("bare name") << QStringLiteral("alpine") << QStringLiteral("alpine") << QStringLiteral("latest") << QString();
    QTest::newRow("name with tag") << QStringLiteral("alpine:3.19") << QStringLiteral("alpine") << QStringLiteral("3.19") << QString();
    QTest::newRow("namespaced") << QStringLiteral("library/alpine") << QStringLiteral("library/alpine") << QStringLiteral("latest") << QString();
    QTest::newRow("namespaced with tag")
        << QStringLiteral("library/alpine:3.19") << QStringLiteral("library/alpine") << QStringLiteral("3.19") << QString();
    QTest::newRow("registry host")
        << QStringLiteral("registry.example.com/team/app:1.2.3") << QStringLiteral("registry.example.com/team/app") << QStringLiteral("1.2.3")
        << QStringLiteral("registry.example.com");
    QTest::newRow("registry with port")
        << QStringLiteral("registry:5000/team/app:1.0") << QStringLiteral("registry:5000/team/app") << QStringLiteral("1.0")
        << QStringLiteral("registry:5000");
    QTest::newRow("localhost") << QStringLiteral("localhost/app") << QStringLiteral("localhost/app") << QStringLiteral("latest")
                               << QStringLiteral("localhost");
    QTest::newRow("tag with dash and dot") << QStringLiteral("app:1.0-rc.1") << QStringLiteral("app") << QStringLiteral("1.0-rc.1") << QString();
}

void ImageReferenceTest::acceptsCommonForms()
{
    QFETCH(QString, reference);
    QFETCH(QString, repository);
    QFETCH(QString, tag);
    QFETCH(QString, registry);

    const auto parts = ImageReference::parse(reference);
    QVERIFY2(parts.has_value(), qPrintable(reference));
    QCOMPARE(parts->repository, repository);
    QCOMPARE(parts->tag, tag);
    QCOMPARE(parts->registry, registry);
    QVERIFY(ImageReference::isValid(reference));
    // fromImage carries no tag (tag is a separate parameter); only a digest carries @
    QCOMPARE(parts->fromImage(), repository);
}

void ImageReferenceTest::rejectsInvalidInput_data()
{
    QTest::addColumn<QString>("reference");

    QTest::newRow("empty") << QString();
    QTest::newRow("only spaces") << QStringLiteral("   ");
    QTest::newRow("inner space") << QStringLiteral("alpine 3.19");
    QTest::newRow("url") << QStringLiteral("https://registry.example.com/app");
    QTest::newRow("shell glob") << QStringLiteral("alpine*");
    QTest::newRow("uppercase repo") << QStringLiteral("Alpine");
    QTest::newRow("empty tag") << QStringLiteral("alpine:");
    QTest::newRow("tag with space") << QStringLiteral("alpine:3 19");
    QTest::newRow("tag too long") << QStringLiteral("alpine:") + QString(200, QLatin1Char('a'));
    QTest::newRow("leading slash") << QStringLiteral("/alpine");
    QTest::newRow("empty digest") << QStringLiteral("alpine@");
    QTest::newRow("short digest") << QStringLiteral("alpine@sha256:abcd");
}

void ImageReferenceTest::rejectsInvalidInput()
{
    QFETCH(QString, reference);
    QVERIFY2(!ImageReference::isValid(reference), qPrintable(reference));
    QVERIFY(!ImageReference::parse(reference).has_value());
    // Normalisation neither throws nor silently rewrites invalid input
    QCOMPARE(ImageReference::normalized(reference), reference.trimmed());
}

void ImageReferenceTest::registryPortIsNotATag()
{
    const auto parts = ImageReference::parse(QStringLiteral("registry:5000/app"));
    QVERIFY(parts.has_value());
    QCOMPARE(parts->registry, QStringLiteral("registry:5000"));
    QCOMPARE(parts->repository, QStringLiteral("registry:5000/app"));
    QCOMPARE(parts->tag, QStringLiteral("latest"));
}

void ImageReferenceTest::digestSkipsTagNormalisation()
{
    const QString reference = QStringLiteral("alpine@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    const auto parts = ImageReference::parse(reference);
    QVERIFY(parts.has_value());
    QCOMPARE(parts->tag, QString());
    QCOMPARE(parts->repository, QStringLiteral("alpine"));
    QVERIFY(!parts->digest.isEmpty());
    // The digest form must reach fromImage verbatim, with no latest appended
    QCOMPARE(parts->fromImage(), reference);
    QCOMPARE(ImageReference::normalized(reference), reference);
}

void ImageReferenceTest::shortFormDropsRegistryOnly()
{
    QCOMPARE(ImageReference::shortForm(QStringLiteral("registry.example.com:5000/team/app:1.0")), QStringLiteral("team/app:1.0"));
    QCOMPARE(ImageReference::shortForm(QStringLiteral("library/alpine:3.19")), QStringLiteral("library/alpine:3.19"));
    QCOMPARE(ImageReference::shortForm(QStringLiteral("alpine:3.19")), QStringLiteral("alpine:3.19"));
}

/*!
 * Pasted input often has surrounding whitespace: parsing trims it, so such input is valid
 * (inner whitespace is still invalid - it usually means a typo).
 */
void ImageReferenceTest::trimsSurroundingWhitespace()
{
    QVERIFY(ImageReference::isValid(QStringLiteral("  alpine:3.19  ")));
    QCOMPARE(ImageReference::normalized(QStringLiteral(" alpine ")), QStringLiteral("alpine:latest"));
    QCOMPARE(ImageReference::parse(QStringLiteral(" alpine "))->tag, QStringLiteral("latest"));
    QVERIFY(!ImageReference::isValid(QStringLiteral("alpine :3.19")));
}

QTEST_GUILESS_MAIN(ImageReferenceTest)

#include "tst_image_reference.moc"
