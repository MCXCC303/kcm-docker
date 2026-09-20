/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_api_version.h"

#include <QtTest>

using namespace Kontainer;

/*! API version policy tests (ARCH_V1 §8). */
class ApiVersionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesVersionStrings_data();
    void parsesVersionStrings();

    void rejectsInvalidVersions_data();
    void rejectsInvalidVersions();

    void negotiatesWithinClientRange();
    void rejectsServerBelowClientMinimum();
    void rejectsServerWhoseMinimumIsTooNew();
    void pathPrefixIsTheOnlyVersionStringSource();
};

void ApiVersionTest::parsesVersionStrings_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<int>("minor");

    QTest::newRow("1.56") << QStringLiteral("1.56") << true << 56;
    QTest::newRow("1.41") << QStringLiteral("1.41") << true << 41;
    QTest::newRow("with spaces") << QStringLiteral(" 1.44 ") << true << 44;
}

void ApiVersionTest::parsesVersionStrings()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);
    QFETCH(int, minor);

    const auto version = ApiVersion::fromString(input);
    QCOMPARE(version.has_value(), valid);
    if (valid) {
        QCOMPARE(version->major(), 1);
        QCOMPARE(version->minor(), minor);
        QCOMPARE(version->pathPrefix(), QStringLiteral("v1.%1").arg(minor));
    }
}

void ApiVersionTest::rejectsInvalidVersions_data()
{
    QTest::addColumn<QString>("input");

    QTest::newRow("empty") << QString();
    QTest::newRow("garbage") << QStringLiteral("latest");
    QTest::newRow("one component") << QStringLiteral("1");
    QTest::newRow("three components") << QStringLiteral("1.2.3");
    QTest::newRow("negative") << QStringLiteral("1.-4");
    QTest::newRow("zero major") << QStringLiteral("0.56");
}

void ApiVersionTest::rejectsInvalidVersions()
{
    QFETCH(QString, input);

    QVERIFY(!ApiVersion::fromString(input).has_value());
}

void ApiVersionTest::negotiatesWithinClientRange()
{
    // Server newer than the client maximum: use the client maximum, never below the server minimum
    const auto newer = ApiVersion::negotiate(ApiVersion(1, 60), ApiVersion(1, 44));
    QVERIFY(newer.has_value());
    QCOMPARE(newer->minor(), ApiVersion::clientMaxMinor());

    // Server inside the range: use the server version as is
    const auto inside = ApiVersion::negotiate(ApiVersion(1, 47), ApiVersion(1, 40));
    QVERIFY(inside.has_value());
    QCOMPARE(inside->minor(), 47);

    // Negotiation must also work without MinAPIVersion
    const auto withoutMinimum = ApiVersion::negotiate(ApiVersion(1, 56), std::nullopt);
    QVERIFY(withoutMinimum.has_value());
    QCOMPARE(withoutMinimum->minor(), 56);
}

void ApiVersionTest::rejectsServerBelowClientMinimum()
{
    const auto version = ApiVersion::negotiate(ApiVersion(1, 24), std::nullopt);
    QVERIFY(!version.has_value());
}

void ApiVersionTest::rejectsServerWhoseMinimumIsTooNew()
{
    const auto version = ApiVersion::negotiate(ApiVersion(1, 99), ApiVersion(1, 90));
    QVERIFY(!version.has_value());
}

void ApiVersionTest::pathPrefixIsTheOnlyVersionStringSource()
{
    // The version prefix format lives here only; no other source file may hardcode "v1.xx"
    QCOMPARE(ApiVersion(1, 56).pathPrefix(), QStringLiteral("v1.56"));
    QCOMPARE(ApiVersion(1, 56).toString(), QStringLiteral("1.56"));
    QVERIFY(ApiVersion().isValid() == false);
}

QTEST_GUILESS_MAIN(ApiVersionTest)

#include "tst_api_version.moc"
