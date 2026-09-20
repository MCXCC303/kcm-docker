/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/service_control.h"
#include "kauth/privileged_config_request.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

using namespace Kontainer;

/*!
 * Wiring test for the privilege path (ARCH_V5_V8 appendix C).
 *
 * Why it exists: the same polkit/KAuth identifier must appear in four places:
 *
 *   1. the helper id in `KAUTH_HELPER_MAIN()` (which bus name the helper owns)
 *   2. `KAuth::Action::setHelperId()` (without it the polkit backend refuses to run)
 *   3. the action name prefix in `.actions` (which actions the policy registers)
 *   4. `allow own` in the D-Bus system policy (the system bus defaults to `<deny own="*"/>`)
 *
 * Hence this test pins the wiring: diverging in any of the four keeps **compilation, unit tests and
 * the UI silent**; on a real machine it shows as "authorization failed / no such action / helper
 * not found", and the symptoms point the wrong way (observed: the packaging path shipped an empty
 * policy, the install script missed the D-Bus policy file, the session side never called setHelperId).
 */
class KauthWiringTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void helperIdAndActionsAreSingleSourced();
    void installScriptInstallsEveryRequiredFile();
    void actionsFileDefinesExactlyOurActions();
    void actionsFileCarriesChineseDialogText();
    void helperSlotsMatchActionNames();
    void generatedPolicyIsNotSilentlyEmpty();
};

namespace
{

QString sourceDir()
{
    return QStringLiteral(KCM_DOCKER_SOURCE_DIR);
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/*! Map an action name to a helper slot name per KAuth's rules. */
QString slotNameFor(const QString &actionName)
{
    QString slot = actionName;
    const QString prefix = QString::fromLatin1(kHelperId) + QLatin1Char('.');
    if (slot.startsWith(prefix)) {
        slot = slot.mid(prefix.size());
    }
    slot.replace(QLatin1Char('.'), QLatin1Char('_'));
    return slot;
}

/*! Parse .actions (INI) into section → (key → value); keys are case-insensitive. */
QHash<QString, QHash<QString, QString>> parseActions(const QString &content)
{
    QHash<QString, QHash<QString, QString>> sections;
    QString current;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            current = line.mid(1, line.size() - 2);
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0 || current.isEmpty()) {
            continue;
        }
        sections[current].insert(line.left(equals).trimmed().toLower(), line.mid(equals + 1).trimmed());
    }
    return sections;
}

QString actionsFilePath()
{
    return sourceDir() + QStringLiteral("/src/kauth/org.kde.kcm.docker.actions");
}

QString generatedPolicyPath()
{
    return QStringLiteral(KCM_DOCKER_BUILD_DIR) + QStringLiteral("/src/org.kde.kcm.docker.policy");
}

QString installScriptPath()
{
    return QStringLiteral(KCM_DOCKER_BUILD_DIR) + QStringLiteral("/install-privileged-helper.sh");
}

} // namespace

void KauthWiringTest::helperIdAndActionsAreSingleSourced()
{
    // helper id and both action ids live in privileged_config_request.h, and actions must
    // sit under this helper's namespace (KAuth resolves slots by stripping the prefix, dots to underscores)
    const QString helperId = QString::fromLatin1(kHelperId);
    QCOMPARE(helperId, QStringLiteral("org.kde.kcm.docker"));

    for (const char *action : {kSaveActionName, kRestartActionName}) {
        const QString name = QString::fromLatin1(action);
        QVERIFY2(name.startsWith(helperId + QLatin1Char('.')), qPrintable(name));
        // Official rule: lowercase, digits and `.` only; kauth-policy-gen rejects uppercase and underscores
        const QRegularExpression valid(QStringLiteral("^[a-z0-9.]+$"));
        QVERIFY2(valid.match(name).hasMatch(), qPrintable(name));
    }

    // The session side must use the constants, never string literals (a typo still compiles)
    const QString client = readFile(sourceDir() + QStringLiteral("/src/backend/privileged_config_client.cpp"));
    QVERIFY(!client.isEmpty());
    QVERIFY2(!client.contains(QLatin1String("\"org.kde.kcm.docker")),
             "action/helper ids must come from privileged_config_request.h, not string literals");
    QVERIFY2(client.contains(QLatin1String("setHelperId")), "the client must set the helper id");
    QVERIFY2(client.contains(QLatin1String("kHelperId")), "the client must use the shared helper id constant");

    // Helper side likewise: the helper id comes from the constant (the macro's third arg is the bus name)
    const QString helper = readFile(sourceDir() + QStringLiteral("/src/kauth/kcm_docker_helper.cpp"));
    QVERIFY(!helper.isEmpty());
    QVERIFY2(helper.contains(QLatin1String("KAUTH_HELPER_MAIN(Kontainer::kHelperId")),
             "the helper must take its bus name from the shared constant");
    QVERIFY2(!helper.contains(QLatin1String("\"org.kde.kcm.docker")),
             "the helper must not hardcode the helper id");
}

void KauthWiringTest::installScriptInstallsEveryRequiredFile()
{
    // Required: helper, polkit policy, D-Bus service file (activation), D-Bus system policy (own)
    const QString script = readFile(installScriptPath());
    if (script.isEmpty()) {
        QSKIP("install-privileged-helper.sh has not been generated yet");
    }
    // Paths are built from variables, so check directories and file names separately
    for (const char *directory : {"/usr/lib/kf6/kauth",
                                  "/usr/share/polkit-1/actions",
                                  "/usr/share/dbus-1/system-services",
                                  "/usr/share/dbus-1/system.d"}) {
        QVERIFY2(script.contains(QString::fromLatin1(directory)), directory);
    }
    for (const char *fileName : {"kcm_docker_helper",
                                 "org.kde.kcm.docker.policy",
                                 "org.kde.kcm.docker.service",
                                 "org.kde.kcm.docker.conf"}) {
        QVERIFY2(script.contains(QString::fromLatin1(fileName)), fileName);
    }

    // It must install the **generated** policy (sourced from .actions), never a hand-written XML
    QVERIFY2(script.contains(QStringLiteral("${KCM_DOCKER_POLICY_FILE}"))
                 || script.contains(QStringLiteral("build/src/org.kde.kcm.docker.policy")),
             qPrintable(script));
    QVERIFY2(!script.contains(QStringLiteral("src/kauth/org.kde.kcm.docker.policy")),
             "the script must install the generated policy, not a hand-written one");

    // The uninstall branch must exist and delete the same variables (no second path list in the README)
    const int uninstall = script.indexOf(QLatin1String("uninstall)"));
    QVERIFY2(uninstall > 0, "the script must support uninstalling what it installed");
    // Look only at the rm line: all four files must be in that one rm
    QString removeLine;
    for (const QString &line : script.mid(uninstall).split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String("rm -f"))) {
            removeLine = line;
            break;
        }
    }
    QVERIFY2(!removeLine.isEmpty(), "the uninstall branch must remove files");
    for (const char *variable : {"$HELPER", "$POLICY", "$SERVICE", "$BUSCONF"}) {
        QVERIFY2(removeLine.contains(QString::fromLatin1(variable)),
                 qPrintable(QStringLiteral("%1 is not removed: %2").arg(QString::fromLatin1(variable), removeLine)));
    }

    // The D-Bus system policy must punch the hole, not be an empty file
    QVERIFY2(script.contains(QStringLiteral("<allow own=\"org.kde.kcm.docker\"/>")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("<allow send_destination=\"org.kde.kcm.docker\"/>")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("Name=org.kde.kcm.docker")), qPrintable(script));
}

void KauthWiringTest::actionsFileDefinesExactlyOurActions()
{
    const QString content = readFile(actionsFilePath());
    QVERIFY2(!content.isEmpty(), qPrintable(actionsFilePath()));
    const auto sections = parseActions(content);

    // [Domain] supplies vendor / icon to the policy file (official format)
    QVERIFY(sections.contains(QStringLiteral("Domain")));
    const auto domain = sections.value(QStringLiteral("Domain"));
    QVERIFY(!domain.value(QStringLiteral("name")).isEmpty());
    QVERIFY(!domain.value(QStringLiteral("icon")).isEmpty());
    QVERIFY(!domain.value(QStringLiteral("url")).isEmpty());

    QStringList actionSections;
    for (auto it = sections.constBegin(); it != sections.constEnd(); ++it) {
        if (it.key() != QLatin1String("Domain")) {
            actionSections.append(it.key());
        }
    }
    actionSections.sort();
    // Expected comes from a **single source**: `.actions` actions = 2 config read/write + 5 service
    // management (unit/verb whitelist lives in service_control; here we only check all names are present)
    QStringList expected {QString::fromLatin1(kRestartActionName), QString::fromLatin1(kSaveActionName)};
    for (const QString &verb : managedServiceVerbs()) {
        ServiceVerb parsed = ServiceVerb::Start;
        if (serviceVerbFromKey(verb, &parsed)) {
            expected.append(serviceActionName(parsed));
        }
    }
    expected.sort();
    QCOMPARE(actionSections, expected);
    QCOMPARE(expected.size(), 7);

    for (const QString &name : expected) {
        const auto action = sections.value(name);
        // Asking the admin every time is our design (the authorization window comes from
        // polkit keep; the generator maps Persistence to allow_active=auth_admin_keep)
        QCOMPARE(action.value(QStringLiteral("policy")), QStringLiteral("auth_admin"));
        QCOMPARE(action.value(QStringLiteral("persistence")), QStringLiteral("session"));
        QVERIFY(!action.value(QStringLiteral("name")).isEmpty());
        QVERIFY(!action.value(QStringLiteral("description")).isEmpty());
    }
}

void KauthWiringTest::actionsFileCarriesChineseDialogText()
{
    // The polkit auth dialog uses the text in .actions. That is the **only** user-visible
    // text outside the po system (xgettext does not extract this format), so this test guards
    // the Chinese translation directly: without it a Chinese session shows an English line.
    const auto sections = parseActions(readFile(actionsFilePath()));
    for (const char *action : {kSaveActionName, kRestartActionName}) {
        const auto section = sections.value(QString::fromLatin1(action));
        QVERIFY2(!section.value(QStringLiteral("name[zh_cn]")).isEmpty(), action);
        QVERIFY2(!section.value(QStringLiteral("description[zh_cn]")).isEmpty(), action);
    }
}

void KauthWiringTest::helperSlotsMatchActionNames()
{
    // KAuth looks up slots by action name minus the helper id prefix, `.` replaced by `_`
    // (DBusHelperProxy): a mismatch still compiles and only shows as "no such action" on a real machine
    const QString helper = readFile(sourceDir() + QStringLiteral("/src/kauth/kcm_docker_helper.cpp"));
    QVERIFY(!helper.isEmpty());

    for (const char *action : {kSaveActionName, kRestartActionName}) {
        const QString slot = slotNameFor(QString::fromLatin1(action));
        const QString signature = QStringLiteral("ActionReply %1(const QVariantMap &arguments)").arg(slot);
        QVERIFY2(helper.contains(signature),
                 qPrintable(QStringLiteral("helper has no slot %1 for action %2").arg(slot, QString::fromLatin1(action))));
    }
}

void KauthWiringTest::generatedPolicyIsNotSilentlyEmpty()
{
    const QString policy = readFile(generatedPolicyPath());
    if (policy.isEmpty()) {
        QSKIP("the generated policy is missing; build the kcm_docker_policy target first");
    }

    // The real-world trap: feeding XML to kauth-policy-gen "succeeds" and produces an empty
    // policy with no actions; after install every authorization fails with no error reported
    const int actionCount = policy.count(QLatin1String("<action id="));
    // 2 config read/write + 5 service management
    QCOMPARE(actionCount, 7);

    for (const char *action : {kSaveActionName, kRestartActionName}) {
        QVERIFY2(policy.contains(QString::fromLatin1(action)), action);
    }
    // Authorization window (under polkit = kept for a few minutes) and minimal exposure:
    // all seven actions must be auth_admin + keep (the five service ones are session-level too)
    QCOMPARE(policy.count(QLatin1String("<allow_active>auth_admin_keep</allow_active>")), 7);
    QCOMPARE(policy.count(QLatin1String("<allow_inactive>no</allow_inactive>")), 7);
    QVERIFY2(!policy.contains(QLatin1String("<allow_any>")),
             "allow_any must stay at its default (no): only active sessions may authorize");
}

QTEST_GUILESS_MAIN(KauthWiringTest)

#include "tst_kauth_wiring.moc"
