/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 提权链路的"接线"测试（ARCH_V5_V8 附录 C）。
 *
 * 为什么需要它：polkit/KAuth 的同一个标识符必须同时出现在四个地方——
 *
 *   1. `KAUTH_HELPER_MAIN()` 的 helper id（helper own 哪个总线名）
 *   2. `KAuth::Action::setHelperId()`（不设它，polkit 后端直接拒绝执行）
 *   3. `.actions` 里动作名的前缀（策略里注册了哪些动作）
 *   4. D-Bus 系统策略的 `allow own`（系统总线默认 `<deny own="*"/>`）
 *
 * 这四处任何一处不一致，**编译、单测、界面都不会报错**，只在真机上表现为
 * "授权失败 / 没有这个动作 / 找不到 helper"，而且症状会把人引向错误的方向
 * （实测踩过：打包路径装出的是空策略、安装脚本漏了 D-Bus 策略文件、
 *  会话侧根本没调 setHelperId）。所以这里把接线钉死。
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

/*! 按 KAuth 的规则把动作名换算成 helper 槽名。 */
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

/*! 解析 .actions（INI）：section → (key → value)，键名大小写不敏感。 */
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
    // helper id 与两个动作 id 都在 privileged_config_request.h 里，且动作必须挂在
    // 这个 helper 的命名空间下（KAuth 按"去掉前缀 + 点换下划线"查槽）
    const QString helperId = QString::fromLatin1(kHelperId);
    QCOMPARE(helperId, QStringLiteral("org.kde.kcm.docker"));

    for (const char *action : {kSaveActionName, kRestartActionName}) {
        const QString name = QString::fromLatin1(action);
        QVERIFY2(name.startsWith(helperId + QLatin1Char('.')), qPrintable(name));
        // 官方规则：小写字母与数字（分层用 `.`）——kauth-policy-gen 会拒绝大写与下划线
        const QRegularExpression valid(QStringLiteral("^[a-z0-9.]+$"));
        QVERIFY2(valid.match(name).hasMatch(), qPrintable(name));
    }

    // 会话侧只能用常量，不能自己写字符串字面量（写错了不会编译失败）
    const QString client = readFile(sourceDir() + QStringLiteral("/src/backend/privileged_config_client.cpp"));
    QVERIFY(!client.isEmpty());
    QVERIFY2(!client.contains(QLatin1String("\"org.kde.kcm.docker")),
             "action/helper ids must come from privileged_config_request.h, not string literals");
    QVERIFY2(client.contains(QLatin1String("setHelperId")), "the client must set the helper id");
    QVERIFY2(client.contains(QLatin1String("kHelperId")), "the client must use the shared helper id constant");

    // helper 侧同理：helper id 由常量给出（宏的第三个参数就是总线名）
    const QString helper = readFile(sourceDir() + QStringLiteral("/src/kauth/kcm_docker_helper.cpp"));
    QVERIFY(!helper.isEmpty());
    QVERIFY2(helper.contains(QLatin1String("KAUTH_HELPER_MAIN(Kontainer::kHelperId")),
             "the helper must take its bus name from the shared constant");
    QVERIFY2(!helper.contains(QLatin1String("\"org.kde.kcm.docker")),
             "the helper must not hardcode the helper id");
}

void KauthWiringTest::installScriptInstallsEveryRequiredFile()
{
    // 四个文件缺一不可：helper、polkit 策略、D-Bus 服务文件（激活）、D-Bus 系统策略（own）
    const QString script = readFile(installScriptPath());
    if (script.isEmpty()) {
        QSKIP("install-privileged-helper.sh has not been generated yet");
    }
    // 路径由变量拼出，因此分别检查目录与文件名
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

    // 安装的必须是**生成出来的**策略（源头是 .actions）；不能去装手写 XML
    QVERIFY2(script.contains(QStringLiteral("${KCM_DOCKER_POLICY_FILE}"))
                 || script.contains(QStringLiteral("build/src/org.kde.kcm.docker.policy")),
             qPrintable(script));
    QVERIFY2(!script.contains(QStringLiteral("src/kauth/org.kde.kcm.docker.policy")),
             "the script must install the generated policy, not a hand-written one");

    // 卸载分支必须存在，且删的是同一组变量（README 里不再抄一份路径清单）
    const int uninstall = script.indexOf(QLatin1String("uninstall)"));
    QVERIFY2(uninstall > 0, "the script must support uninstalling what it installed");
    // 只看 rm 那一行：四个文件必须都在同一条 rm 里
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

    // D-Bus 系统策略的内容必须是"打洞"而不是空文件
    QVERIFY2(script.contains(QStringLiteral("<allow own=\"org.kde.kcm.docker\"/>")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("<allow send_destination=\"org.kde.kcm.docker\"/>")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("Name=org.kde.kcm.docker")), qPrintable(script));
}

void KauthWiringTest::actionsFileDefinesExactlyOurActions()
{
    const QString content = readFile(actionsFilePath());
    QVERIFY2(!content.isEmpty(), qPrintable(actionsFilePath()));
    const auto sections = parseActions(content);

    // [Domain] 给策略文件提供 vendor / icon（官方格式）
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
    // 期望值来自**单一来源**：`.actions` 里应该有的动作 = 配置读写两个 + 服务管理的五个
    // （服务管理的 unit/动词白名单在 service_control 里，这里只核对"动作名齐全"）
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
        // 每次都问管理员 = 我们的设计（授权窗口由 polkit 的 keep 决定，
        // 生成器会把 Persistence 翻成 allow_active=auth_admin_keep）
        QCOMPARE(action.value(QStringLiteral("policy")), QStringLiteral("auth_admin"));
        QCOMPARE(action.value(QStringLiteral("persistence")), QStringLiteral("session"));
        QVERIFY(!action.value(QStringLiteral("name")).isEmpty());
        QVERIFY(!action.value(QStringLiteral("description")).isEmpty());
    }
}

void KauthWiringTest::actionsFileCarriesChineseDialogText()
{
    // polkit 的授权对话框用的是 .actions 里的文案。它是**唯一**不走 po 体系的
    // 用户可见文本（xgettext 不提取这个格式），因此这里直接守住中文译文存在：
    // 少了它，中文会话里会弹出一句英文。
    const auto sections = parseActions(readFile(actionsFilePath()));
    for (const char *action : {kSaveActionName, kRestartActionName}) {
        const auto section = sections.value(QString::fromLatin1(action));
        QVERIFY2(!section.value(QStringLiteral("name[zh_cn]")).isEmpty(), action);
        QVERIFY2(!section.value(QStringLiteral("description[zh_cn]")).isEmpty(), action);
    }
}

void KauthWiringTest::helperSlotsMatchActionNames()
{
    // KAuth 用"动作名去掉 helper id 前缀、`.` 换成 `_`"来查槽（DBusHelperProxy）：
    // 名字对不上不会编译失败，只会在真机上表现为"没有这个动作"
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

    // 这就是那个真实踩过的坑：把 XML 交给 kauth-policy-gen 会"成功"生成
    // 一个没有动作的空策略，安装后所有授权都失败但没有任何报错
    const int actionCount = policy.count(QLatin1String("<action id="));
    // 配置读写 2 个 + 服务管理 5 个
    QCOMPARE(actionCount, 7);

    for (const char *action : {kSaveActionName, kRestartActionName}) {
        QVERIFY2(policy.contains(QString::fromLatin1(action)), action);
    }
    // 授权窗口（polkit 下 = 保持几分钟）与最小暴露面
    // 七个动作都应该是 auth_admin + keep（服务管理的五个同样是 session 级授权）
    QCOMPARE(policy.count(QLatin1String("<allow_active>auth_admin_keep</allow_active>")), 7);
    QCOMPARE(policy.count(QLatin1String("<allow_inactive>no</allow_inactive>")), 7);
    QVERIFY2(!policy.contains(QLatin1String("<allow_any>")),
             "allow_any must stay at its default (no): only active sessions may authorize");
}

QTEST_GUILESS_MAIN(KauthWiringTest)

#include "tst_kauth_wiring.moc"
