/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtTest>

namespace
{
QString sourceDir()
{
    return QStringLiteral(KCM_DOCKER_SOURCE_DIR);
}

/*! 递归收集某个目录下的文件内容（`relativePath -> content`）。 */
QMap<QString, QString> collectFiles(const QString &directory, const QStringList &nameFilters)
{
    QMap<QString, QString> files;
    const QDir root(directory);
    if (!root.exists()) {
        return files;
    }
    QDirIterator iterator(root.absolutePath(), nameFilters, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            files.insert(root.relativeFilePath(path), QString::fromUtf8(file.readAll()));
        }
    }
    return files;
}

/*!
 * 命中给定模式的行（跳过注释行：注释里提到某个 token 名字不算真的在使用它）。
 */
QStringList linesMatching(const QString &content, const QRegularExpression &pattern)
{
    QStringList hits;
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool insideBlockComment = false;
    for (int index = 0; index < lines.size(); ++index) {
        const QString trimmed = lines.at(index).trimmed();
        if (insideBlockComment) {
            if (trimmed.contains(QLatin1String("*/"))) {
                insideBlockComment = false;
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1String("/*"))) {
            insideBlockComment = !trimmed.contains(QLatin1String("*/"));
            continue;
        }
        if (trimmed.startsWith(QLatin1String("//")) || trimmed.startsWith(QLatin1Char('*'))) {
            continue;
        }
        if (pattern.match(lines.at(index)).hasMatch()) {
            hits.append(QStringLiteral("%1: %2").arg(index + 1).arg(trimmed));
        }
    }
    return hits;
}
} // namespace

/*!
 * 源码约定测试（ARCH_V3 §2.1 的验收条件）。
 *
 * 这些约定靠代码评审很容易漏：一次复制粘贴就能让「状态配色只有一处实现」
 * 或「只读边界」悄悄失效，而它们在界面上不会立刻表现出来。
 * 因此把它们写成可执行的断言。
 */
class SourceConventionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void copyActionHasSingleImplementation();
    void qmlPropertiesUseValidSyntax();
    void statusColorsStayInPalettes();
    void mutationsHaveSingleChokePoint();
    void restPathsStayInOneHeader();
    void qmlNeverTalksHttp();
    void kioStaysInHostPathService();
    void externalProcessesStayForbidden();
    void qmlUsesOnlyQmlIdentifiers();
    void pluginMetadataVersionMatchesProject();
    void kcmMetadataIsCompleteForSystemSettings();
};

/*!
 * 复制动作只允许有一个实现（CopyButton）：以前这段逻辑在两个详情页里手写了 6 遍。
 */
void SourceConventionsTest::copyActionHasSingleImplementation()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!files.isEmpty(), "no QML files found");

    QStringList offenders;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key() == QLatin1String("components/CopyButton.qml")) {
            continue;
        }
        if (it.value().contains(QLatin1String("copyToClipboard"))) {
            offenders.append(it.key());
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("copying must go through components/CopyButton.qml, found in: %1").arg(offenders.join(QStringLiteral(", ")))));
}

/*!
 * 状态语义色只允许出现在 StatusPalette 里（ARCH_V2 §12 / ARCH_V3 §2.1）。
 * 数据可视化使用 ChartPalette 的固定取色，两者不得混用。
 */
/*!
 * QML 属性声明必须是合法写法（`property bool foo: false`）。
 *
 * 这条是拿一次真实事故换来的：`property bool foo: bool = false` 这种写法在**运行时**才报
 * `Error: Invalid write to global property "bool"`，属性根本没被声明成功（界面上的开关因此
 * 静默失效），而单元测试与编译都不会报错——因为它是合法 JSON 意义上的"表达式"，
 * 要跑起来才炸。这里用源码扫描把它挡在提交之前。
 */
void SourceConventionsTest::qmlPropertiesUseValidSyntax()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY(!files.isEmpty());

    // property <type> <name>: <type> = ...  ← 冒号后面又写了一次类型，是错的
    static const QRegularExpression suspicious(
        QStringLiteral(R"(^\s*property\s+\w+\s+\w+\s*:\s*(bool|int|real|string|var|double|url|color)\s*=)"));
    QStringList violations;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QStringList lines = it.value().split(QLatin1Char('\n'));
        for (int index = 0; index < lines.size(); ++index) {
            const QRegularExpressionMatch match = suspicious.match(lines.at(index));
            if (match.hasMatch()) {
                violations.append(QStringLiteral("%1:%2  %3").arg(it.key()).arg(index + 1).arg(lines.at(index).trimmed()));
            }
        }
    }
    QVERIFY2(violations.isEmpty(),
             qPrintable(QStringLiteral("invalid QML property declaration (property <type> <name>: <type> = …):\n")
                        + violations.join(QLatin1Char('\n'))));
}

void SourceConventionsTest::statusColorsStayInPalettes()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    const QRegularExpression statusToken(QStringLiteral("(positiveTextColor|neutralTextColor|negativeTextColor|disabledTextColor)"));

    QStringList offenders;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key() == QLatin1String("components/StatusPalette.qml")) {
            continue;
        }
        const QStringList hits = linesMatching(it.value(), statusToken);
        if (!hits.isEmpty()) {
            offenders.append(QStringLiteral("%1 (%2)").arg(it.key(), hits.first()));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("status colors must be mapped in components/StatusPalette.qml only:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * 写操作的咽喉点（ARCH_V4 §1.5 / §2.2.1）。
 *
 * 三期用一条「生产代码不得出现任何写动词」的断言把只读边界钉死。四期打开写操作时，
 * 这条断言**不是被删掉，而是换成更精确的形状**：写动词与 REST 路径都必须收敛到
 * 唯一一处，界面层完全碰不到传输层。这样「打开写操作」仍然是一次显式、被审阅、
 * 能被测试发现的改动，只是审查对象从「有没有写动词」变成「写动词在哪里」。
 */
void SourceConventionsTest::mutationsHaveSingleChokePoint()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // 写动词只允许出现在传输层：方法名在那里被写进请求行
    const QRegularExpression writeVerb(QStringLiteral("\"(POST|PUT|PATCH|DELETE)\""));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key() == QLatin1String("backend/docker_client.cpp")) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), writeVerb)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("write verbs must stay in backend/docker_client.cpp:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * REST 路径只允许出现在 docker_api_paths.h（ARCH_V4 §2.2.1）。
 *
 * 路径散落在多个 .cpp 里时，「这个程序到底会调用哪些端点」就没人能一眼答上来——
 * 对现在有写操作的项目来说，这个问题必须能一眼答上来。
 */
void SourceConventionsTest::restPathsStayInOneHeader()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});

    const QRegularExpression restPath(QStringLiteral("QStringLiteral\\(\"/(containers|images|system/df|_ping|version|info)"));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key() == QLatin1String("backend/docker_api_paths.h")) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), restPath)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Docker REST paths must be built in backend/docker_api_paths.h only:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * 界面层不认识传输层（ARCH_V4 §1.5）：QML 里出现 http / 动词 / socket 路径，
 * 说明有请求逻辑漏到了界面里。
 */
void SourceConventionsTest::qmlNeverTalksHttp()
{
    const QMap<QString, QString> qmlFiles = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!qmlFiles.isEmpty(), "no QML sources found");

    // 只拦"真的在碰传输层"的写法：
    //  - 请求 API（XMLHttpRequest / fetch）
    //  - HTTP 方法字面量与 REST 路径片段、socket 地址
    // 示例地址（例如镜像加速器占位符 `https://mirror.example.com`）是数据而不是请求，
    // 因此不再把 `http(s)://` 一律当成违规——那会把"校验用户输入"也误判成越界。
    const QRegularExpression transport(QStringLiteral("(XMLHttpRequest|fetch\\(|\"GET |\"POST|\"DELETE|unix://|/containers/|/images/)"));
    QStringList offenders;
    for (auto it = qmlFiles.constBegin(); it != qmlFiles.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), transport)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("QML must not contain transport details:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * 打开宿主目录是四期唯一新增的「非 Docker 外部动作」，
 * 因此它必须被限制在一个实现文件里，而不是散落到各个页面。
 */
void SourceConventionsTest::kioStaysInHostPathService()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});

    const QRegularExpression kioUse(QStringLiteral("(KIO::|#include <KIO/)"));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("backend/kio_host_path_service"))) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), kioUse)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("KIO may only be used by backend/kio_host_path_service.*:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * 外部进程与提权的边界（ARCH_V3 §1.3 → **ARCH_V5_V8 §1.5.1 有条件放宽**）。
 *
 * 五期引入了第一个受限提权组件（用户已批准，理由见 ARCH_V5_V8 §1.5.1：
 * 系统级部署下 `/etc/docker/daemon.json` 用户不可写，配置镜像源没有不提权的实现方式）。
 * 因此这条断言从"KAuth 全面禁止"改成**白名单**：
 *
 *  - `QProcess` 仍然全面禁止（我们从不 shell out；重启走 systemd D-Bus）
 *  - `KAuth` 只允许出现在被审阅过的提权文件里：客户端与 helper
 *  - 其他任何文件引入 KAuth / 提权机制 → 直接失败
 */
void SourceConventionsTest::externalProcessesStayForbidden()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // 允许出现 KAuth 的文件（提权边界：改动这里必须是一次显式、被审阅的设计变更）
    const QStringList privilegedFiles = {
        QStringLiteral("backend/privileged_config_client.cpp"),
        QStringLiteral("backend/privileged_config_client.h"),
        QStringLiteral("kauth/kcm_docker_helper.cpp"),
        QStringLiteral("kauth/privileged_config_request.cpp"),
        QStringLiteral("kauth/privileged_config_request.h"),
    };

    // 只拦"真的会执行外部程序"的写法：`systemctl` 这类词会出现在给用户复制的命令文本里，
    // 那不是我们在执行（重启走 systemd D-Bus），因此不按关键词拦。
    const QRegularExpression externalProcess(QStringLiteral("(QProcess|popen\\(|execv|/bin/sh)"));
    const QRegularExpression privilegeEscalation(QStringLiteral("(KAuth|polkit)"));

    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), externalProcess)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
        if (privilegedFiles.contains(it.key())) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), privilegeEscalation)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Kontainer never shells out; privilege escalation is limited to the reviewed helper files:\n%1")
                            .arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * QML 里不许出现 C++ 侧的名字（`QStringLiteral` / `QString` / …）。
 *
 * 真实踩过：把 `i18n("… %1", QStringLiteral("https://…"))` 写进 QML —— C++ 里
 * 完全正常，QML 里则抛 `ReferenceError: QStringLiteral is not defined`，
 * 而且只在**那条分支真的被执行**时才报（添加空行触发校验），编译与页面加载都看不出来。
 * 这类错误的代价是"看起来只是校验没生效"。
 */
void SourceConventionsTest::qmlUsesOnlyQmlIdentifiers()
{
    const QMap<QString, QString> qmlFiles = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!qmlFiles.isEmpty(), "no QML sources found");

    const QRegularExpression cppOnly(QStringLiteral("\\b(QStringLiteral|QStringList|QLatin1String|QVariantMap|QStringView|qPrintable|QString::)\\b"));
    const QRegularExpression plainQString(QStringLiteral("\\bQString\\s*\\("));
    QStringList offenders;
    for (auto it = qmlFiles.constBegin(); it != qmlFiles.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), cppOnly)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
        for (const QString &hit : linesMatching(it.value(), plainQString)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("C++ identifiers are not available in QML:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * 插件元数据里的版本必须与 `project VERSION` 一致。
 *
 * 两处版本号一旦漂移，用户看到的"关于"版本与实际构建的版本就对不上，
 * 而这类错误在发布流程里极难被发现（没人会去比对两个文件）。
 */
void SourceConventionsTest::pluginMetadataVersionMatchesProject()
{
    QFile file(sourceDir() + QStringLiteral("/src/kcm/kcm_docker.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject plugin = root.value(QStringLiteral("KPlugin")).toObject();
    QVERIFY2(!plugin.isEmpty(), "the metadata must have a KPlugin section");
    QCOMPARE(plugin.value(QStringLiteral("Version")).toString(), QString::fromLatin1(KCM_DOCKER_VERSION));
}

/*!
 * 模块要出现在「系统设置」里，元数据必须齐全（ARCH §5.18）。
 *
 * 这一条是**安装路径之外**的全部要求：System Settings 只按 `KPluginMetaData` 分组与搜索，
 * 因此 Id / 名称 / 描述 / 图标 / 关键词 / 父分类 / 翻译域 少一个都会以不同方式"看起来没装上"：
 *   - 没有 `X-KDE-System-Settings-Parent-Category` → 落到默认分组，用户找不到；
 *   - 分组名拼错（例如写成 `systemadmin`）→ 同样落到默认分组；
 *   - 没有 `X-KDE-Keywords` → 在系统设置里搜"容器/docker"搜不到；
 *   - 没有 `KLocalizedString.TranslationDomain` → 界面文案不翻译。
 */
void SourceConventionsTest::kcmMetadataIsCompleteForSystemSettings()
{
    QFile file(sourceDir() + QStringLiteral("/src/kcm/kcm_docker.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject plugin = root.value(QStringLiteral("KPlugin")).toObject();

    // 模块名就是用户在 `systemsettings <module>` 里敲的那个
    QCOMPARE(plugin.value(QStringLiteral("Id")).toString(), QStringLiteral("kcm_docker"));
    // 名称与描述都要有中英两份（本项目的界面是双语的）
    for (const QString &key : {QStringLiteral("Name"), QStringLiteral("Description"),
                               QStringLiteral("Name[zh_CN]"), QStringLiteral("Description[zh_CN]")}) {
        QVERIFY2(!plugin.value(key).toString().isEmpty(), qPrintable(QStringLiteral("missing %1").arg(key)));
    }
    // 图标必须给（breeze 里有 folder-docker；名字写错在界面上就是空白图标）
    QCOMPARE(plugin.value(QStringLiteral("Icon")).toString(), QStringLiteral("folder-docker"));

    // 父分类必须是 Plasma 6 真实存在的分组之一，否则不会出现在预期位置
    const QStringList knownCategories {QStringLiteral("system-administration"),
                                       QStringLiteral("hardware"),
                                       QStringLiteral("network"),
                                       QStringLiteral("security-privacy"),
                                       QStringLiteral("appearance"),
                                       QStringLiteral("applications"),
                                       QStringLiteral("session"),
                                       QStringLiteral("search"),
                                       QStringLiteral("themes"),
                                       QStringLiteral("windowmanagement"),
                                       QStringLiteral("keyboard"),
                                       QStringLiteral("display"),
                                       QStringLiteral("input-devices"),
                                       QStringLiteral("pointing-devices"),
                                       QStringLiteral("regionalsettings")};
    const QString category = root.value(QStringLiteral("X-KDE-System-Settings-Parent-Category")).toString();
    QVERIFY2(knownCategories.contains(category),
             qPrintable(QStringLiteral("unknown system settings category: '%1'").arg(category)));

    // 搜索关键词：中英文都要有，用户在系统设置里搜得到
    const QString keywords = root.value(QStringLiteral("X-KDE-Keywords")).toString();
    QVERIFY2(keywords.contains(QLatin1String("docker")), "english keywords are needed for search");
    QVERIFY2(keywords.contains(QStringLiteral("容器")), "chinese keywords are needed for search");

    /*
     * 区间地图里不得再用附着式悬停提示。
     *
     * 真实事故（用户实测）：`QQC2.ToolTip.text/visible` 是**附着属性**，一个窗口共用同一个
     * 提示框；地图里同时有几十个方块、切筛选/切视图时 delegate 还会被销毁重建，
     * 结果"鼠标停在哪都显示同一个容器名，切回列表还在"。容器名改由 `Accessible.name` 提供，
     * 因此整个文件里不该再出现 ToolTip。
     */
    const QString mapPath = sourceDir() + QStringLiteral("/src/ui/components/HostPortRangeMap.qml");
    QFile mapFile(mapPath);
    QVERIFY2(mapFile.open(QIODevice::ReadOnly), qPrintable(mapPath));
    const QString mapSource = QString::fromUtf8(mapFile.readAll());
    // 只看代码，不看注释（这条规则本身就得在注释里解释清楚为什么）
    QString mapCode;
    {
        static const QRegularExpression blockComment(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
        QString stripped = mapSource;
        stripped.remove(blockComment);
        const QStringList lines = stripped.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const int comment = line.indexOf(QLatin1String("//"));
            mapCode += (comment >= 0 ? line.left(comment) : line);
            mapCode += QLatin1Char('\n');
        }
    }
    QVERIFY2(!mapCode.contains(QLatin1String("ToolTip")),
             "the range map must not use attached tooltips (they leak across delegate rebuilds)");

    // 翻译域必须与 po/ 里的域一致，否则文案不翻译
    QCOMPARE(root.value(QStringLiteral("KLocalizedString")).toObject().value(QStringLiteral("TranslationDomain")).toString(),
             QStringLiteral("kcm_docker"));
}


QTEST_GUILESS_MAIN(SourceConventionsTest)

#include "tst_source_conventions.moc"
