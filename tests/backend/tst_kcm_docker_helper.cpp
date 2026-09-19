/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kauth/privileged_config_request.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Kontainer;

/*!
 * 提权助手的安全边界（ARCH_V5_V8 §2.4 / §6.1）。
 *
 * 这是整个项目**唯一**以 root 运行的入口，因此这里的每一条拒绝都对应一种
 * "如果放过去就会变成提权后门"的情形：任意路径、任意键、任意内容、任意命令。
 * 校验逻辑与 helper 共用同一个实现，所以这些断言等价于对 helper 的断言
 * （不需要 root 就能跑，也不需要真的安装 polkit policy）。
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

    // data-root 会改变容器数据目录：界面都不能改，helper 更不能
    QVariantMap arguments = mirrorsRequest({QStringLiteral("https://mirror.example.com")});
    arguments.insert(QStringLiteral("data-root"), QStringLiteral("/tmp/evil"));
    QVERIFY2(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), "unknown keys must be rejected");
    QCOMPARE(errorKey, QStringLiteral("unknownKey"));

    // 路径参数这种"看起来合理"的键同样拒绝
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
        QStringLiteral("mirror.example.com"), // 没有 scheme
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
        QStringLiteral("https://mirror.example.com/\n{\"data-root\":\"/tmp\"}"), // 试图注入 JSON
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

    // 合法形态：host[:port]
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
    // 空请求意味着"什么都没改"：让调用方以为生效了是最坏的结果，因此明确拒绝
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

    // 现有文件是坏 JSON：helper 必须拒绝写（否则会把用户可用的配置换成起不来的）
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
    // 纯删除不是"空请求"：否则界面点保存会被 noEdits 拒掉
    QVERIFY(!request.isEmpty());
}

void KontainerHelperTest::rejectsRemovalOfUnmanagedKeys()
{
    // helper 只能删它管得着的键：data-root / 任意路径都不行
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
    // 同一个键既赋值又要求删除：拒绝，而不是替调用方猜哪个优先
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

    // 「解锁」按钮：用户还没改任何东西，但需要一次针对同一 action 的授权
    QVariantMap arguments;
    arguments.insert(QStringLiteral("dryRun"), true);
    QVERIFY2(PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey), qPrintable(errorKey));
    QVERIFY(request.dryRun());
    QVERIFY2(request.isEmpty(), "a dry run carries no edits");

    // 没有 dryRun 的空请求仍然拒绝（避免"什么都没改却报告成功"）
    QVERIFY(!PrivilegedConfigRequest::fromArguments(QVariantMap(), &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noEdits"));
}

void KontainerHelperTest::dryRunStillRejectsUnknownKeys()
{
    PrivilegedConfigRequest request;
    QString errorKey;

    // dryRun 不是"放宽校验"的开关：越权键在解锁路径上同样被拒
    QVariantMap arguments;
    arguments.insert(QStringLiteral("dryRun"), true);
    arguments.insert(QStringLiteral("data-root"), QStringLiteral("/tmp/evil"));
    QVERIFY(!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("unknownKey"));
}

QTEST_MAIN(KontainerHelperTest)

#include "tst_kcm_docker_helper.moc"
