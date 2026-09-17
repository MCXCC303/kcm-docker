/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    离屏渲染工具（开发用，不参与 ctest）：把界面渲染成 PNG，用于人工视觉复核。

    为什么需要它：
      - Qt 的 vnc platform 插件在本机不可用（渲染期间段错误 / 不响应
        FramebufferUpdateRequest），无法用它截图；
      - 直接在用户桌面上开窗口截图会干扰用户，而且只能看到当前主题。

    本工具完全离屏：使用确定性 fixture（MockDockerBackend）+ 显式注入的
    Breeze 亮色 / 暗色配色，因此可以稳定复现两种主题下的排版与配色，
    用来核对 ARCH_V3_pre §1.4/§1.8 的对比度与 §1.2/§1.5 的排版要求。

    用法：
        render_ui <page> <width> <height> <light|dark> <output.png>
        page = main | container-detail | image-detail | engine | daemon-config | daemon-config-user

    环境变量 KONTAINER_RENDER_LANG=zh_CN 时按 `po/<lang>/kcm_docker.po` 的译文渲染：
    中文文案普遍更长，横幅折行、按钮宽度、省略号是否合理只有看中文截图才知道
    （真实会话的 LANG 就是 zh_CN，所以这其实是默认形态）。
    注意：只有 QML 里的文案会变中文。C++ 组装的文本（"3 seconds ago"、"Restarting (1)"）
    在截图里仍是英文——ki18n 不经过 `QCoreApplication` 的 translator 链（安装自定义
    QTranslator 实测无效），而 Qt 的 QTranslator 又不认 gettext 的 .mo。
    C++ 侧译文的正确性由 `tst_i18n_consistency` 的运行时用例负责（真的加载 .mo 并断言译文）。

    注意：注入的是 Kirigami.Theme 的颜色 token（Kirigami 允许应用覆盖它们），
    不会修改任何业务代码；字体的度量仍来自当前平台。
*/

#include "i18n.h"
#include "model/qml_registration.h"
#include "model/operation_controller.h"
#include "support/mock_docker_backend.h"
#include "support/qml_stub_kcm.h"

#include <KIconLoader>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>
#include <functional>
#include <memory>

using namespace Kontainer;

namespace
{

/*! 确定性 fixture：覆盖运行中 / 暂停 / 已退出 / 不健康、多个镜像与悬空镜像。 */
void fillFixture(MockDockerBackend &backend)
{
    EngineInfo engine;
    engine.available = true;
    engine.countsAvailable = true;
    engine.serverVersion = QStringLiteral("29.8.0");
    // 部署形态相关字段按本机真实情况填写（系统级 root daemon、无 rootless 标记、
    // 数据目录在家目录、live-restore 关闭）：配置页与 Engine 页的截图才具备参考价值
    engine.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")};
    // KONTAINER_RENDER_ROOTLESS=1：模拟 rootless daemon（配合 HOME 覆盖可复核"用户可写"形态）
    if (qEnvironmentVariableIsSet("KONTAINER_RENDER_ROOTLESS")) {
        engine.securityOptions.append(QStringLiteral("name=rootless"));
    }
    engine.dockerRootDir = QStringLiteral("/home/thf/.local/share/docker/");
    engine.loggingDriver = QStringLiteral("json-file");
    engine.liveRestoreEnabled = false;
    engine.apiVersion = QStringLiteral("1.56");
    engine.minApiVersion = QStringLiteral("1.24");
    engine.osType = QStringLiteral("linux");
    engine.architecture = QStringLiteral("x86_64");
    engine.kernelVersion = QStringLiteral("6.17.4-arch1-1");
    engine.engineName = QStringLiteral("workstation");
    engine.operatingSystem = QStringLiteral("Arch Linux");
    engine.cgroupVersion = QStringLiteral("2");
    engine.storageDriver = QStringLiteral("overlayfs");
    engine.containerTotal = 5;
    engine.containersRunning = 3;
    engine.containersPaused = 1;
    engine.containersStopped = 1;
    engine.imageCount = 6;
    engine.memoryTotalBytes = 38ll * 1024 * 1024 * 1024;
    backend.setEngineInfo(engine);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto makeContainer = [&now](const QString &id, const QString &name, const QString &image, ContainerState state, HealthState health, const QString &status, int ageMinutes) {
        Container container;
        container.id = id;
        container.name = name;
        container.image = image;
        container.imageId = QStringLiteral("sha256:aaaa");
        container.state = state;
        container.health = health;
        container.status = status;
        container.created = now.addSecs(-60ll * ageMinutes);
        return container;
    };

    Container running = makeContainer(QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"),
                                      QStringLiteral("web-frontend"),
                                      QStringLiteral("registry.example.com/team/frontend:2.4.1"),
                                      ContainerState::Running,
                                      HealthState::Healthy,
                                      QStringLiteral("Up 2 hours (healthy)"),
                                      180);
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}, {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")}};

    Container unhealthy = makeContainer(QStringLiteral("2222222222222222222222222222222222222222222222222222222222222222"),
                                        QStringLiteral("postgres-primary"),
                                        QStringLiteral("postgres:17-alpine"),
                                        ContainerState::Running,
                                        HealthState::Unhealthy,
                                        QStringLiteral("Up 5 hours (unhealthy)"),
                                        300);
    unhealthy.ports = {{QStringLiteral("127.0.0.1"), 5432, 5432, QStringLiteral("tcp")}};

    Container paused = makeContainer(QStringLiteral("3333333333333333333333333333333333333333333333333333333333333333"),
                                     QStringLiteral("worker-batch"),
                                     QStringLiteral("python:3.13-slim"),
                                     ContainerState::Paused,
                                     HealthState::None,
                                     QStringLiteral("Up 12 minutes (Paused)"),
                                     30);

    Container restarting = makeContainer(QStringLiteral("4444444444444444444444444444444444444444444444444444444444444444"),
                                         QStringLiteral("cache-redis"),
                                         QStringLiteral("redis:7"),
                                         ContainerState::Restarting,
                                         HealthState::Starting,
                                         QStringLiteral("Restarting (1) 3 seconds ago"),
                                         15);

    Container exited = makeContainer(QStringLiteral("5555555555555555555555555555555555555555555555555555555555555555"),
                                     QStringLiteral("migration-job"),
                                     QStringLiteral("alpine:3.21"),
                                     ContainerState::Exited,
                                     HealthState::None,
                                     QStringLiteral("Exited (0) 5 minutes ago"),
                                     45);

    backend.setContainers({running, unhealthy, paused, restarting, exited});

    Image frontend;
    frontend.id = QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    frontend.repoTags = {QStringLiteral("registry.example.com/team/frontend:2.4.1"), QStringLiteral("registry.example.com/team/frontend:latest")};
    frontend.repoDigests = {QStringLiteral("registry.example.com/team/frontend@sha256:bbbbbbbbbbbb")};
    frontend.sizeBytes = 412ll * 1024 * 1024;
    frontend.created = now.addDays(-9);
    frontend.containerCount = 2;
    frontend.inUse = true;

    Image postgres;
    postgres.id = QStringLiteral("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    postgres.repoTags = {QStringLiteral("postgres:17-alpine")};
    postgres.sizeBytes = 268ll * 1024 * 1024;
    postgres.created = now.addDays(-30);
    postgres.containerCount = 1;
    postgres.inUse = true;

    Image dangling;
    dangling.id = QStringLiteral("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    dangling.sizeBytes = 96ll * 1024 * 1024;
    dangling.created = now.addDays(-120);

    backend.setImages({frontend, postgres, dangling});

    StorageUsage storage;
    storage.valid = true;
    storage.buildCacheAvailable = true;
    storage.imagesBytes = 812ll * 1024 * 1024;
    storage.containersBytes = 148ll * 1024 * 1024;
    storage.volumesBytes = 2ll * 1024 * 1024 * 1024 + 340ll * 1024 * 1024;
    storage.buildCacheBytes = 96ll * 1024 * 1024;
    storage.layersBytes = 1400ll * 1024 * 1024;
    storage.imageCount = 6;
    storage.containerCount = 5;
    storage.volumeCount = 3;
    storage.buildCacheCount = 12;
    backend.setStorageUsage(storage);

    ContainerDetail detail;
    detail.id = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");
    detail.name = QStringLiteral("web-frontend");
    detail.image = QStringLiteral("registry.example.com/team/frontend:2.4.1");
    detail.imageId = QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    detail.state = ContainerState::Running;
    detail.health = HealthState::Healthy;
    detail.status = QStringLiteral("Up 2 hours (healthy)");
    detail.created = now.addSecs(-60ll * 180);
    detail.started = now.addSecs(-60ll * 120);
    detail.exitCode = 0;
    detail.restartCount = 2;
    detail.pid = 41237;
    detail.platform = QStringLiteral("linux");
    detail.restartPolicy = QStringLiteral("unless-stopped");
    detail.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
                    {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
                    {QStringLiteral("::"), 9090, 0, QStringLiteral("tcp")}};
    // KONTAINER_RENDER_MANY_PORTS=1：造一堆映射，用来复核拓扑在 20+ 行时的观感
    // （连线按 index 推导行高，行数多了不应该错位或溢出）
    if (qEnvironmentVariableIsSet("KONTAINER_RENDER_MANY_PORTS")) {
        detail.ports.clear();
        for (int i = 0; i < 12; ++i) {
            const quint16 hostPort = quint16(20000 + i * 7);
            const quint16 containerPort = quint16(3000 + i);
            detail.ports.append({QStringLiteral("0.0.0.0"), containerPort, hostPort, QStringLiteral("tcp")});
            detail.ports.append({QStringLiteral("127.0.0.1"), containerPort, quint16(hostPort + 1), QStringLiteral("tcp")});
        }
        for (int i = 0; i < 4; ++i) {
            detail.ports.append({QString(), quint16(9000 + i), 0, QStringLiteral("tcp")});
        }
    }
    detail.networks = {{QStringLiteral("bridge"),
                        QStringLiteral("a1b2c3d4e5f6"),
                        QStringLiteral("172.17.0.4"),
                        QStringLiteral("fd00::4"),
                        QStringLiteral("02:42:ac:11:00:04"),
                        QStringLiteral("172.17.0.1")}};
    detail.mounts = {{QStringLiteral("bind"),
                      QString(),
                      QStringLiteral("/srv/frontend/config"),
                      QStringLiteral("/etc/frontend"),
                      QStringLiteral("ro"),
                      true},
                     {QStringLiteral("volume"),
                      QStringLiteral("frontend-cache"),
                      QStringLiteral("/var/lib/docker/volumes/frontend-cache/_data"),
                      QStringLiteral("/var/cache/frontend"),
                      QStringLiteral("rw"),
                      false}};
    // KONTAINER_RENDER_LONG_PATHS=1：把挂载路径拉长，用来复核"宿主路径省略 + 容器路径靠右"
    // 在极端长度下的表现（真实机器上 WinBoat 那类容器的宿主路径可以很长）
    if (qEnvironmentVariableIsSet("KONTAINER_RENDER_LONG_PATHS")) {
        detail.mounts = {{QStringLiteral("bind"),
                          QString(),
                          QStringLiteral("/home/someone/.local/share/containers/storage/overlay/"
                                         "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6/merged/opt/application/"
                                         "resources/very-long-directory-name-for-elision-check"),
                          QStringLiteral("/opt/application/resources/very-long-directory-name-for-elision-check"),
                          QStringLiteral("rw"),
                          true},
                         {QStringLiteral("bind"),
                          QString(),
                          QStringLiteral("/srv/data"),
                          QStringLiteral("/data"),
                          QStringLiteral("ro"),
                          true}};
    }
    detail.environment = {QStringLiteral("NODE_ENV=production"),
                          QStringLiteral("API_BASE_URL=https://api.example.com"),
                          QStringLiteral("LOG_LEVEL=info"),
                          QStringLiteral("TZ=Asia/Shanghai")};
    detail.command = {QStringLiteral("node"), QStringLiteral("server.js")};
    detail.entrypoint = {QStringLiteral("/usr/local/bin/docker-entrypoint.sh")};
    detail.workingDirectory = QStringLiteral("/app");
    detail.user = QStringLiteral("node");
    detail.hostname = QStringLiteral("a1b2c3d4e5f6");
    detail.labels = {{QStringLiteral("com.example.stack"), QStringLiteral("frontend")},
                     {QStringLiteral("com.example.version"), QStringLiteral("2.4.1")}};
    backend.setContainerDetail(detail);

    ContainerStats stats;
    stats.containerId = detail.id;
    stats.timestamp = now;
    stats.cpuTotalUsage = 42'000'000'000ull;
    stats.cpuPreTotalUsage = 41'000'000'000ull;
    stats.systemCpuUsage = 900'000'000'000ull;
    stats.systemPreCpuUsage = 899'000'000'000ull;
    stats.onlineCpus = 16;
    stats.memoryUsageBytes = 340ll * 1024 * 1024;
    stats.memoryCacheBytes = 40ll * 1024 * 1024;
    stats.memoryLimitBytes = 1ll * 1024 * 1024 * 1024;
    stats.networkRxBytes = 128ll * 1024 * 1024;
    stats.networkTxBytes = 24ll * 1024 * 1024;
    stats.blockReadBytes = 12ll * 1024 * 1024;
    stats.blockWriteBytes = 3ll * 1024 * 1024;
    stats.pids = 18;
    backend.setContainerStats(stats);

    ImageDetail imageDetail;
    imageDetail.id = QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    imageDetail.repoTags = {QStringLiteral("registry.example.com/team/frontend:2.4.1"), QStringLiteral("registry.example.com/team/frontend:latest")};
    imageDetail.repoDigests = {QStringLiteral("registry.example.com/team/frontend@sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")};
    imageDetail.created = now.addDays(-9);
    imageDetail.sizeBytes = 412ll * 1024 * 1024;
    imageDetail.architecture = QStringLiteral("amd64");
    imageDetail.variant = QStringLiteral("v3");
    imageDetail.os = QStringLiteral("linux");
    imageDetail.author = QStringLiteral("Platform Team <platform@example.com>");
    for (int i = 0; i < 14; ++i) {
        imageDetail.layers.append(QStringLiteral("sha256:%1").arg(QStringLiteral("0123456789abcdef").repeated(4).left(64), 0).arg(i));
    }
    imageDetail.environment = {QStringLiteral("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"),
                               QStringLiteral("NODE_VERSION=22.11.0"),
                               QStringLiteral("NODE_ENV=production")};
    imageDetail.entrypoint = {QStringLiteral("docker-entrypoint.sh")};
    imageDetail.command = {QStringLiteral("node"), QStringLiteral("server.js")};
    imageDetail.workingDirectory = QStringLiteral("/app");
    backend.setImageDetail(imageDetail);
}

/*!
    选择图标主题。

    离屏渲染默认没有图标主题（平台主题为空时 Qt 只找 hicolor），
    结果是所有 Kirigami.Icon / 图标按钮都渲染成空白——
    而「图标 + 颜色 + 文字」三重编码正是 ARCH_V3 §1.6/§1.8 的验收项，
    所以这里显式指定 Breeze 图标主题，保证渲染结果能反映真实观感。
*/
void applyIconTheme(bool dark)
{
    const QString theme = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");

    // QIcon 侧（QQC2 的图标按钮走这条路径）
    QIcon::setThemeName(theme);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    // Kirigami.Icon 走 KDE 的 KIconLoader，它自己缓存主题（KIconTheme 从
    // QIcon::themeName() 读取），因此设置 QIcon 之后必须让它重新加载配置。
    if (KIconLoader *loader = KIconLoader::global()) {
        loader->reconfigure(QStringLiteral("kontainer-render-ui"));
    }
}

} // namespace

namespace
{

/*!
 * 读 `po/<lang>/kcm_docker.po`，返回 msgid → msgstr 表。
 *
 * 为什么不走 QTranslator：Qt 不认 gettext 的 .mo（实测 `QTranslator::load()` 返回 false），
 * 而 ki18n 加载译文的路径在 KQuickConfigModule 里。渲染工具只需要"界面上显示什么字"，
 * 直接读 .po 反而更贴近译者实际提交的内容。未设置 KONTAINER_RENDER_LANG 时返回空表
 * （保持英文渲染，与之前的截图可比）。
 */
QVariantMap loadTranslations()
{
    const QString language = qEnvironmentVariable("KONTAINER_RENDER_LANG");
    if (language.isEmpty()) {
        return {};
    }
    QFile file(QStringLiteral(KONTAINER_SOURCE_DIR "/po/%1/kcm_docker.po").arg(language));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("cannot read translations for %s", qPrintable(language));
        return {};
    }

    const QString content = QString::fromUtf8(file.readAll());
    // 去掉首尾引号；`msgstr[0] "..."` 这类行要先切掉 key 本身
    const auto unquote = [](const QString &raw) {
        const QString text = raw.trimmed();
        if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
            return text.mid(1, text.size() - 2);
        }
        return text;
    };
    const auto field = [&unquote](const QStringList &block, const QString &key) {
        QString value;
        bool collecting = false;
        for (const QString &line : block) {
            if (line.startsWith(key + QLatin1Char(' '))) {
                collecting = true;
                value += unquote(line.mid(key.size() + 1));
            } else if (collecting && line.startsWith(QLatin1Char('"'))) {
                value += unquote(line);
            } else if (collecting) {
                break;
            }
        }
        return value;
    };

    QVariantMap translations;
    const QStringList blocks = content.split(QStringLiteral("\n\n"));
    for (const QString &raw : blocks) {
        if (raw.startsWith(QLatin1String("#~"))) {
            continue;
        }
        const QStringList block = raw.split(QLatin1Char('\n'));
        const QString id = field(block, QStringLiteral("msgid"));
        if (id.isEmpty()) {
            continue; // 头部元数据
        }
        // 复数条目在 zh_CN 里只有一种形式，单复数都用它
        const QString value = field(block, QStringLiteral("msgstr[0]")).isEmpty()
            ? field(block, QStringLiteral("msgstr"))
            : field(block, QStringLiteral("msgstr[0]"));
        if (value.isEmpty()) {
            continue; // 未翻译：保持英文，与真实界面一致
        }
        translations.insert(id, value);
        const QString plural = field(block, QStringLiteral("msgid_plural"));
        if (!plural.isEmpty()) {
            translations.insert(plural, value);
        }
    }
    return translations;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    if (argc < 6) {
        std::fprintf(stderr, "usage: %s <main|container-detail|image-detail|engine> <width> <height> <light|dark> <output.png>\n", argv[0]);
        return 2;
    }

    const QString page = QString::fromLocal8Bit(argv[1]);
    const int width = QString::fromLocal8Bit(argv[2]).toInt();
    const int height = QString::fromLocal8Bit(argv[3]).toInt();
    const QString theme = QString::fromLocal8Bit(argv[4]);
    const QString output = QString::fromLocal8Bit(argv[5]);
    const int tabIndex = argc > 6 ? QString::fromLocal8Bit(argv[6]).toInt() : 0;
    const bool dark = theme == QLatin1String("dark");

    // 主题必须在引擎创建之前设好：Kirigami 从应用 QPalette 推导主题色
    applyIconTheme(dark);

    // 必须使用 KDE 的 QQC2 样式（真实会话里 kcmshell6 就是这个）：
    // 默认样式（Fusion/Basic）下的 Label 颜色取自 QPalette，而 Kirigami.AbstractCard
    // 内部是 `Theme.inherit: false` + `colorSet: View`，两者不一致时会出现
    // 「深色卡片 + 黑色文字」这种只属于离屏渲染的组合。用 KDE 样式后
    // 控件颜色统一由 Kirigami.Theme 决定，渲染结果与真实会话一致。
    // QQuickStyle 需要单独的 include 路径，这里直接用环境变量等价设置
    qputenv("QT_QUICK_CONTROLS_STYLE", "org.kde.desktop");

    setupTranslationDomain();
    registerKontainerQmlTypes();

    auto backend = std::make_unique<MockDockerBackend>();
    fillFixture(*backend);
    // 截图要能看到四期的写入口：把 endpoint 指向一个当前进程可写的临时 socket 文件，
    // 权限门（DockerCapabilities）才会放行。不连接它，只是让判定为「可写」。
    QTemporaryDir socketDir;
    const QString socketPath = socketDir.path() + QStringLiteral("/docker.sock");
    {
        QFile socketFile(socketPath);
        if (socketFile.open(QIODevice::WriteOnly)) {
            socketFile.write("x");
            socketFile.close();
        }
        QFile::setPermissions(socketPath, QFile::ReadOwner | QFile::WriteOwner);
    }
    backend->setEndpoint(DockerEndpoint::unixSocket(socketPath));
    auto stub = std::make_unique<QmlStubKcm>(backend.get());

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("kcm"), stub.get());
    // i18n 桩必须做 %N 替换，否则渲染出来的文案是 "%1 · created %2 ago · ID %3"，
    // 与真实运行结果不符（真实运行时由 KLocalizedString 替换）。
    // 译文表来自 .po（KONTAINER_RENDER_LANG）：没有它就只能渲染英文，
    // 而真实会话是 zh_CN——中文更长，折行与截断只有看中文截图才看得出来。
    // 必须挂在 JS 全局对象上：engine.evaluate() 里定义的函数看不到 context property
    // （实测会抛 ReferenceError: ktTranslations is not defined，界面上的文案会整片消失）
    engine.globalObject().setProperty(QStringLiteral("ktTranslations"), engine.toScriptValue(loadTranslations()));
    engine.evaluate(QStringLiteral("function _ktFormat(text, args) {\n"
                                   "    return String(text).replace(/%(\\d+)/g, function (match, index) {\n"
                                   "        const value = args[index - 1];\n"
                                   "        return value !== undefined ? value : match;\n"
                                   "    });\n"
                                   "}\n"
                                   "function _ktText(text) {\n"
                                   "    const translated = ktTranslations[text];\n"
                                   "    return translated !== undefined ? translated : text;\n"
                                   "}\n"
                                   "function i18n(text) { return _ktFormat(_ktText(text), Array.prototype.slice.call(arguments, 1)); }\n"
                                   "function i18nc(context, text) { return _ktFormat(_ktText(text), Array.prototype.slice.call(arguments, 2)); }\n"
                                   "function i18np(singular, plural, count) { return _ktFormat(_ktText(count === 1 ? singular : plural), [count]); }\n"
                                   "function i18ncp(context, singular, plural, count) { return _ktFormat(_ktText(count === 1 ? singular : plural), [count]); }\n"));


    // 先让 controller 完成一轮刷新，页面才有数据可渲染
    stub->controller()->refresh();
    // KONTAINER_RENDER_PULLS=1：造出「一路进行中 + 一路失败」的拉取列表，
    // 用于截图复核进度条、取消按钮与失败原因是否可见（ARCH_V4 §2.4）
    if (qEnvironmentVariableIsSet("KONTAINER_RENDER_PULLS")) {
        auto *operations = stub->controller()->operations();
        operations->pullImage(QStringLiteral("quay.io/libpod/alpine:latest"));
        operations->pullImage(QStringLiteral("registry.example.com/team/app:2.4.1"));

        ImagePullProgress progress;
        progress.reference = QStringLiteral("quay.io/libpod/alpine:latest");
        progress.phase = ImagePullProgress::Phase::Downloading;
        progress.statusText = QStringLiteral("Downloading");
        progress.currentBytes = 41;
        progress.totalBytes = 100;
        progress.completedLayers = 1;
        progress.totalLayers = 3;
        backend->emitPullProgress(progress);

        // 只让后面那一路失败：前一路保持「进行中」，这样截图里三种状态都能看到
        backend->completeMutation(QStringLiteral("image:registry.example.com/team/app:2.4.1"),
                                  DockerBackendInterface::MutationOutcome::Failed,
                                  DockerError(DockerError::Kind::Timeout, QStringLiteral("no response headers within 10000 ms")));
    }

    backend->completeRefresh();

    const QString sourceDir = QStringLiteral(KONTAINER_SOURCE_DIR "/src/ui/");
    QString qmlFile;
    QVariantMap initialProperties;
    if (page == QLatin1String("main") || page == QLatin1String("engine")) {
        qmlFile = QStringLiteral("MainPage.qml");
    } else if (page == QLatin1String("container-detail")) {
        qmlFile = QStringLiteral("ContainerDetail.qml");
        initialProperties.insert(QStringLiteral("containerId"), QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"));
    } else if (page == QLatin1String("image-detail")) {
        qmlFile = QStringLiteral("ImageDetail.qml");
        initialProperties.insert(QStringLiteral("imageId"), QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    } else if (page == QLatin1String("daemon-config-user")) {
        qmlFile = QStringLiteral("DaemonConfigPage.qml");
        initialProperties.insert(QStringLiteral("scope"), QStringLiteral("user"));
    } else if (page == QLatin1String("daemon-config")) {
        // 运行时配置页（ARCH_V5_V8 §2.3）：内容来自真实文件系统，
        // 用 HOME 指向临时目录即可构造"用户可写"的 rootless 形态（见 render_ui.sh 的说明）
        qmlFile = QStringLiteral("DaemonConfigPage.qml");
    } else {
        std::fprintf(stderr, "unknown page: %s\n", qPrintable(page));
        return 2;
    }

    QQmlComponent component(&engine, QUrl::fromLocalFile(sourceDir + qmlFile));
    if (component.isError()) {
        std::fprintf(stderr, "component error: %s\n", qPrintable(component.errorString()));
        return 1;
    }
    QObject *object = component.createWithInitialProperties(initialProperties, engine.rootContext());
    if (!object) {
        std::fprintf(stderr, "create failed: %s\n", qPrintable(component.errorString()));
        return 1;
    }
    backend->completeRefresh();

    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        std::fprintf(stderr, "root object is not an Item\n");
        return 1;
    }

    QQuickWindow window;
    window.resize(width, height);
    item->setParentItem(window.contentItem());
    item->setWidth(width);
    item->setHeight(height);
    window.show();

    // 可选：切到指定分区/标签页，便于逐页复核（例如容器详情的「网络」分区）
    if (tabIndex > 0) {
        QQuickItem *tabBar = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *node) {
            if (!node || tabBar) {
                return;
            }
            if (node->objectName() == QLatin1String("detailTabBar") || node->objectName() == QLatin1String("tabBar")) {
                tabBar = node;
                return;
            }
            const QList<QQuickItem *> children = node->childItems();
            for (QQuickItem *child : children) {
                walk(child);
            }
        };
        walk(item);
        if (tabBar) {
            tabBar->setProperty("currentIndex", tabIndex);
        }
    }

    // 等布局与 delegate 完成（一次事件循环 + 一小段等待即可）
    QTimer::singleShot(900, &app, [&]() {
        const QImage image = window.grabWindow();
        if (image.isNull() || !image.save(output)) {
            std::fprintf(stderr, "failed to save %s\n", qPrintable(output));
            app.exit(1);
            return;
        }
        std::printf("saved %s (%dx%d, %s, %s)\n", qPrintable(output), image.width(), image.height(), qPrintable(theme), qPrintable(page));
        app.exit(0);
    });

    return app.exec();
}
