/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kauth/privileged_config_request.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Kontainer;

/*!
 * Security boundary of the privileged helper (ARCH_V5_V8 §2.4 / §6.1).
 *
 * The project's **only** root entry point, so every rejection here maps to a
 * privilege-escalation backdoor if allowed: arbitrary path, key, content or
 * command. Validation shares its implementation with the helper, so these
 * assertions describe the helper too (no root, no installed polkit policy).
 */
class KontainerHelperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acceptsWhitelistedEdits();
    void rejectsUnknownKeys();
    void rejectsInvalidMirrorValues();
    void rejectsInjectionAttempts();
    void rejectsInvalidInsecureRegistries();
    void rejectsUnknownLogDriver();
    void rejectsOversizedRequests();
    void rejectsEmptyRequest();
    void mergesPreservingUnknownKeys();
    void refusesToMergeIntoUnparsableContent();
    void dryRunIsAcceptedWithoutEdits();
    void dryRunStillRejectsUnknownKeys();
    void acceptsRemovalOfManagedKeys();
    void rejectsRemovalOfUnmanagedKeys();
    void rejectsConflictingSetAndRemove();
    void removalPreservesUnmanagedKeys();
};

namespace
{
QVariantMap mirrorsRequest(const QStringList &mirrors)
{
    QVariantMap arguments;
    arguments.insert(QStringLiteral("registry-mirrors"), mirrors);
    return arguments;
}
} // namespace

void KontainerHelperTest::acceptsWhitelistedEdits()
{
    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(PrivilegedConfigRequest::fromArguments(mirrorsRequest({QStringLiteral("https://mirror.example.com")}), &request, &errorKey));
    QVERIFY(errorKey.isEmpty());
    QVERIFY(request.setRegistryMirrors());
    QCOMPARE(request.registryMirrors(), QStringList {QStringLiteral("https://mirror.example.com")});
    QVERIFY(!request.isEmpty());
}

void KontainerHelperTest::rejectsUnknownKeys()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    // data-root moves the container data directory: not even the UI may set it, let alone the helper
    QVariantMap arguments = mirrorsRequest({QStringLiteral("https://mirror.example.com")});
    arguments.insert(QStringLiteral("data-root"), QStringLiteral("/tmp/evil"));
    QVERIFY2(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), "unknown keys must be rejected");
    QCOMPARE(errorKey, QStringLiteral("unknownKey"));

    // Plausible-looking keys such as a path argument are rejected just the same
    QVariantMap pathAttempt;
    pathAttempt.insert(QStringLiteral("path"), QStringLiteral("/etc/shadow"));
    QVERIFY(!PrivilegedConfigRequest::fromArguments(pathAttempt, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("unknownKey"));
}

void KontainerHelperTest::rejectsInvalidMirrorValues()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    const QStringList bad = {
        QStringLiteral("mirror.example.com"), // no scheme
        QStringLiteral("ftp://mirror.example.com"),
        QStringLiteral("https://mirror.example.com/path"),
        QStringLiteral("https://mirror.example.com?q=1"),
        QStringLiteral(""),
        QStringLiteral("https://"),
    };
    for (const QString &value : bad) {
        QVERIFY2(!PrivilegedConfigRequest::fromArguments(mirrorsRequest({value}), &request, &errorKey), qPrintable(value));
        QCOMPARE(errorKey, QStringLiteral("invalidValue"));
    }
}

void KontainerHelperTest::rejectsInjectionAttempts()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    const QStringList attacks = {
        QStringLiteral("https://mirror.example.com/\n{\"data-root\":\"/tmp\"}"), // tries to inject JSON
        QStringLiteral("https://mirror.example.com;rm -rf /"),
        QStringLiteral("https://mirror.example.com$(id)"),
        QStringLiteral("../../etc/shadow"),
        QStringLiteral("https://mirror.example.com/#\"}"),
    };
    for (const QString &value : attacks) {
        QVERIFY2(!PrivilegedConfigRequest::fromArguments(mirrorsRequest({value}), &request, &errorKey), qPrintable(value));
    }
}

void KontainerHelperTest::rejectsInvalidInsecureRegistries()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    QVariantMap arguments;
    arguments.insert(QStringLiteral("insecure-registries"), QStringList {QStringLiteral("https://registry.local:5000")});
    QVERIFY2(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), "a scheme is not allowed here");
    QCOMPARE(errorKey, QStringLiteral("invalidValue"));

    // Valid form: host[:port]
    QVariantMap good;
    good.insert(QStringLiteral("insecure-registries"), QStringList {QStringLiteral("registry.local:5000")});
    QVERIFY(PrivilegedConfigRequest::fromArguments(good, &request, &errorKey));
    QCOMPARE(request.insecureRegistries(), QStringList {QStringLiteral("registry.local:5000")});
}

void KontainerHelperTest::rejectsUnknownLogDriver()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    QVariantMap arguments;
    arguments.insert(QStringLiteral("log-driver"), QStringLiteral("json-file; rm -rf /"));
    QVERIFY(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("invalidValue"));

    QVariantMap good;
    good.insert(QStringLiteral("log-driver"), QStringLiteral("journald"));
    QVERIFY(PrivilegedConfigRequest::fromArguments(good, &request, &errorKey));
    QCOMPARE(request.logDriver(), QStringLiteral("journald"));
}

void KontainerHelperTest::rejectsOversizedRequests()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    QStringList many;
    for (int i = 0; i < PrivilegedConfigRequest::kMaxListEntries + 1; ++i) {
        many.append(QStringLiteral("https://mirror%1.example.com").arg(i));
    }
    QVERIFY(!PrivilegedConfigRequest::fromArguments(mirrorsRequest(many), &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("tooLarge"));

    QVariantMap huge;
    huge.insert(QStringLiteral("max-concurrent-downloads"), 100000);
    QVERIFY(!PrivilegedConfigRequest::fromArguments(huge, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("invalidValue"));
}

void KontainerHelperTest::rejectsEmptyRequest()
{
    PrivilegedConfigRequest request;
    QString errorKey;
    // An empty request means "nothing changed": a caller believing it applied is worst, so reject it
    QVERIFY(!PrivilegedConfigRequest::fromArguments(QVariantMap(), &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noEdits"));
}

void KontainerHelperTest::mergesPreservingUnknownKeys()
{
    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(PrivilegedConfigRequest::fromArguments(mirrorsRequest({QStringLiteral("https://new.example.com")}), &request, &errorKey));

    const QByteArray existing = QByteArrayLiteral("{\"data-root\":\"/home/thf/.local/share/docker/\",\"features\":{\"buildkit\":true}}");
    const QByteArray merged = request.mergeInto(existing);
    QVERIFY(!merged.isEmpty());

    const QJsonObject root = QJsonDocument::fromJson(merged).object();
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/home/thf/.local/share/docker/"));
    QCOMPARE(root.value(QStringLiteral("features")).toObject().value(QStringLiteral("buildkit")).toBool(), true);
    QCOMPARE(root.value(QStringLiteral("registry-mirrors")).toArray().first().toString(), QStringLiteral("https://new.example.com"));
}

void KontainerHelperTest::refusesToMergeIntoUnparsableContent()
{
    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(PrivilegedConfigRequest::fromArguments(mirrorsRequest({QStringLiteral("https://mirror.example.com")}), &request, &errorKey));

    // Existing file is bad JSON: refuse to write, else a working config is replaced by a broken one
    QVERIFY2(request.mergeInto(QByteArrayLiteral("{ broken")).isEmpty(), "must not overwrite an unparsable config");
}

void KontainerHelperTest::acceptsRemovalOfManagedKeys()
{
    QVariantMap arguments;
    arguments.insert(QStringLiteral("remove"), QStringList {QStringLiteral("log-driver"), QStringLiteral("max-concurrent-downloads")});

    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));
    QCOMPARE(request.removeKeys(), QStringList({QStringLiteral("log-driver"), QStringLiteral("max-concurrent-downloads")}));
    // Remove-only is not an "empty request", else saving from the UI would be rejected with noEdits
    QVERIFY(!request.isEmpty());
}

void KontainerHelperTest::rejectsRemovalOfUnmanagedKeys()
{
    // The helper may only remove keys it manages: not data-root or an arbitrary path
    for (const QString &key : {QStringLiteral("data-root"),
                               QStringLiteral("storage-driver"),
                               QStringLiteral("features"),
                               QStringLiteral("../../etc/passwd")}) {
        QVariantMap arguments;
        arguments.insert(QStringLiteral("remove"), QStringList {key});
        PrivilegedConfigRequest request;
        QString errorKey;
        QVERIFY2(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), qPrintable(key));
        QCOMPARE(errorKey, QStringLiteral("unknownKey"));
    }
}

void KontainerHelperTest::rejectsConflictingSetAndRemove()
{
    // Same key both set and removed: reject instead of guessing which one wins
    QVariantMap arguments;
    arguments.insert(QStringLiteral("log-driver"), QStringLiteral("json-file"));
    arguments.insert(QStringLiteral("remove"), QStringList {QStringLiteral("log-driver")});

    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("conflictingKeys"));
}

void KontainerHelperTest::removalPreservesUnmanagedKeys()
{
    const QByteArray existing = QByteArrayLiteral(
        "{\"log-driver\":\"json-file\",\"max-concurrent-downloads\":5,\"data-root\":\"/srv\"}");

    QVariantMap arguments;
    arguments.insert(QStringLiteral("remove"), QStringList {QStringLiteral("log-driver")});
    PrivilegedConfigRequest request;
    QString errorKey;
    QVERIFY(PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));

    const QJsonObject root = QJsonDocument::fromJson(request.mergeInto(existing)).object();
    QVERIFY(!root.contains(QStringLiteral("log-driver")));
    QCOMPARE(root.value(QStringLiteral("max-concurrent-downloads")).toInt(), 5);
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/srv"));
}

void KontainerHelperTest::dryRunIsAcceptedWithoutEdits()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    // "Unlock" button: the user changed nothing yet, but needs one authorization for the same action
    QVariantMap arguments;
    arguments.insert(QStringLiteral("dryRun"), true);
    QVERIFY2(PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), qPrintable(errorKey));
    QVERIFY(request.dryRun());
    QVERIFY2(request.isEmpty(), "a dry run carries no edits");

    // An empty request without dryRun is still rejected (never report success for no change)
    QVERIFY(!PrivilegedConfigRequest::fromArguments(QVariantMap(), &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noEdits"));
}

void KontainerHelperTest::dryRunStillRejectsUnknownKeys()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    // dryRun is not a "relax validation" switch: out-of-scope keys are rejected on the unlock path too
    QVariantMap arguments;
    arguments.insert(QStringLiteral("dryRun"), true);
    arguments.insert(QStringLiteral("data-root"), QStringLiteral("/tmp/evil"));
    QVERIFY(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("unknownKey"));
}

QTEST_MAIN(KontainerHelperTest)

#include "tst_kcm_docker_helper.moc"
