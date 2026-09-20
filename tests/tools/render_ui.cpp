/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Offscreen render tool (dev only, not part of ctest): renders the UI to PNG for manual review.

    Why it exists:
      - Qt's vnc platform plugin is unusable here (segfaults while rendering / never answers
        FramebufferUpdateRequest), so it cannot take screenshots;
      - grabbing a window on the user's desktop disturbs them and shows only the current theme.

    Fully offscreen: a deterministic fixture (MockDockerBackend) plus explicitly injected Breeze
    light/dark colors reproduce layout and colors in both themes, for checking the contrast
    requirements of ARCH_V3_pre §1.4/§1.8 and the layout requirements of §1.2/§1.5.

    Usage:
        render_ui <page> <width> <height> <light|dark> <output.png>
        page = main | container-detail | image-detail | engine | daemon-config | daemon-config-user

    With KCM_DOCKER_RENDER_LANG=zh_CN, strings come from `po/<lang>/kcm_docker.po`: Chinese text
    is longer, and banner wrapping, button widths and ellipses only prove themselves in a
    Chinese screenshot (real sessions run LANG=zh_CN, so this is the default shape).
    Only QML strings turn Chinese. C++-assembled text ("3 seconds ago", "Restarting (1)") stays
    English — ki18n does not go through `QCoreApplication`'s translator chain (installing a
    custom QTranslator provably does nothing) and Qt's QTranslator cannot read gettext .mo files.
    C++ translations are covered at runtime by `tst_i18n_consistency`, which loads the .mo and
    asserts the translated strings.

    Note: only Kirigami.Theme color tokens are injected (Kirigami lets applications override
    them), so no production code is touched; font metrics still come from the current platform.
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

/*! Deterministic fixture: running / paused / exited / unhealthy containers, several images (one dangling). */
void fillFixture(MockDockerBackend &backend)
{
    EngineInfo engine;
    engine.available = true;
    engine.countsAvailable = true;
    engine.serverVersion = QStringLiteral("29.8.0");
    // Deployment fields mirror this machine (system root daemon, no rootless flag, data dir
    // under $HOME, live-restore off) so the DaemonConfig and Engine screenshots stay meaningful.
    engine.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")};
    // KCM_DOCKER_RENDER_ROOTLESS=1: fake a rootless daemon (HOME override = "user-writable" shape)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_ROOTLESS")) {
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
    engine.components = {{QStringLiteral("Engine"), QStringLiteral("29.8.0")},
                         {QStringLiteral("containerd"), QStringLiteral("1.7.24")},
                         {QStringLiteral("runc"), QStringLiteral("1.2.3")},
                         {QStringLiteral("docker-init"), QStringLiteral("0.19.0")}};
    engine.cgroupDriver = QStringLiteral("systemd");
    engine.cpuCount = 16;
    engine.warnings = {QStringLiteral("No swap limit support")};
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
    // KCM_DOCKER_RENDER_DECLARED_PORT=1: a declared-but-never-published port plus a very long
    // range, to check the ports page's declaredNotPublished state and the range map's "N more" cap
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_DECLARED_PORT")) {
        detail.declaredPorts = {{4880, QStringLiteral("tcp"), QString(), 4880, 4880},
                                {3389, QStringLiteral("tcp"), QString(), 1000, 1100}};
    }
    detail.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")},
                    {QStringLiteral("0.0.0.0"), 443, 8443, QStringLiteral("tcp")},
                    {QStringLiteral("::"), 9090, 0, QStringLiteral("tcp")}};
    // KCM_DOCKER_RENDER_MANY_PORTS=1: many mappings, to check the topology with 20+ rows
    // (connectors derive row height from index; many rows must not misalign or overflow)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_MANY_PORTS")) {
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
    // KCM_DOCKER_RENDER_BRANCH_PORTS=1: one container port bound to several host addresses
    // (incl. IPv6 wildcard), the shape real user containers have
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_BRANCH_PORTS")) {
        detail.ports.clear();
        detail.ports.append({QStringLiteral("0.0.0.0"), 8888, 20004, QStringLiteral("tcp")});
        detail.ports.append({QStringLiteral("::"), 8888, 20004, QStringLiteral("tcp")});
        detail.ports.append({QStringLiteral("127.0.0.1"), 8888, 20204, QStringLiteral("tcp")});
        detail.ports.append({QStringLiteral("0.0.0.0"), 4800, 4800, QStringLiteral("tcp")});
        detail.ports.append({QStringLiteral("::"), 4800, 4800, QStringLiteral("tcp")});
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
    // KCM_DOCKER_RENDER_LONG_PATHS=1: very long mount paths, to check "host path elided +
    // container path right-aligned" at extreme lengths (WinBoat-style containers have those)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_LONG_PATHS")) {
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
    // Long command, to check "collapse to 4 lines + show all" (KCM_DOCKER_RENDER_LONG_COMMAND=1)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_LONG_COMMAND")) {
        detail.command = {QStringLiteral("jupyter"), QStringLiteral("notebook"),
                          QStringLiteral("--ip=0.0.0.0"), QStringLiteral("--port=8888"),
                          QStringLiteral("--allow-root"), QStringLiteral("--no-browser"),
                          QStringLiteral("--IdentityProvider.token=bohrium"),
                          QStringLiteral("--ServerApp.root_dir=/workspace"),
                          QStringLiteral("--ServerApp.allow_remote_access=True"),
                          QStringLiteral("--ServerApp.iopub_data_rate_limit=1000000000")};
    } else {
        detail.command = {QStringLiteral("node"), QStringLiteral("server.js")};
    }
    detail.entrypoint = {QStringLiteral("/usr/local/bin/docker-entrypoint.sh")};
    detail.workingDirectory = QStringLiteral("/app");
    detail.user = QStringLiteral("node");
    detail.hostname = QStringLiteral("a1b2c3d4e5f6");
    detail.labels = {{QStringLiteral("com.example.stack"), QStringLiteral("frontend")},
                     {QStringLiteral("com.example.version"), QStringLiteral("2.4.1")}};
    // A second "paused" detail for the container-detail-paused page, to check the Resume button
    ContainerDetail pausedDetail = detail;
    pausedDetail.id = QStringLiteral("3333333333333333333333333333333333333333333333333333333333333333");
    pausedDetail.name = QStringLiteral("worker-paused");
    pausedDetail.state = ContainerState::Paused;
    backend.setContainerDetail(detail);
    backend.setContainerDetailForId(pausedDetail.id, pausedDetail);

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
    Pick the icon theme.

    Offscreen rendering starts with no icon theme (with an empty platform theme Qt looks in
    hicolor only), so every Kirigami.Icon and icon button renders blank — yet icon + color + text
    is exactly the ARCH_V3 §1.6/§1.8 acceptance criterion. Setting Breeze explicitly keeps the
    render representative of real sessions.
*/
void applyIconTheme(bool dark)
{
    const QString theme = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");

    // QIcon side (QQC2 icon buttons take this path)
    QIcon::setThemeName(theme);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    // Kirigami.Icon goes through KDE's KIconLoader, which caches the theme (KIconTheme reads
    // QIcon::themeName()), so it must be told to reload after QIcon is set.
    if (KIconLoader *loader = KIconLoader::global()) {
        loader->reconfigure(QStringLiteral("kontainer-render-ui"));
    }
}

} // namespace

namespace
{

/*!
 * Read `po/<lang>/kcm_docker.po` into a msgid → msgstr map.
 *
 * Not via QTranslator: Qt cannot read gettext .mo files (`QTranslator::load()` returns false),
 * and ki18n's own lookup lives inside KQuickConfigModule. The tool only needs to know which text
 * reaches the UI, and the .po is what translators actually commit. Without
 * KCM_DOCKER_RENDER_LANG the map stays empty, keeping English renders comparable to older ones.
 */
QVariantMap loadTranslations()
{
    const QString language = qEnvironmentVariable("KCM_DOCKER_RENDER_LANG");
    if (language.isEmpty()) {
        return {};
    }
    QFile file(QStringLiteral(KCM_DOCKER_SOURCE_DIR "/po/%1/kcm_docker.po").arg(language));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("cannot read translations for %s", qPrintable(language));
        return {};
    }

    const QString content = QString::fromUtf8(file.readAll());
    // Strip the surrounding quotes; for lines like `msgstr[0] "..."` drop the key itself first
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
            continue; // header metadata
        }
        // zh_CN has a single plural form; use it for both singular and plural
        const QString value = field(block, QStringLiteral("msgstr[0]")).isEmpty()
            ? field(block, QStringLiteral("msgstr"))
            : field(block, QStringLiteral("msgstr[0]"));
        if (value.isEmpty()) {
            continue; // untranslated: keep English, as the real UI does
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

    // The theme must be set before the engine is created: Kirigami derives it from the QPalette
    applyIconTheme(dark);

    // Must use KDE's QQC2 style (what kcmshell6 uses in real sessions): under the default
    // Fusion/Basic style Label colors come from QPalette while Kirigami.AbstractCard is
    // `Theme.inherit: false` + `colorSet: View`; the mismatch produces "dark card + black text",
    // an offscreen-only combination. KDE's style routes all colors through Kirigami.Theme.
    // Setting QQuickStyle needs another include path, so use the equivalent env var instead
    qputenv("QT_QUICK_CONTROLS_STYLE", "org.kde.desktop");

    setupTranslationDomain();
    registerKontainerQmlTypes();

    auto backend = std::make_unique<MockDockerBackend>();
    fillFixture(*backend);
    // Screenshots must show the phase-4 write entry points: point the endpoint at a temp socket
    // file this process can write, so the capability gate (DockerCapabilities) lets it through.
    // Nothing connects to it; it only makes the socket count as writable.
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

    // KCM_DOCKER_RENDER_STORED_CREDENTIALS=1: two stored credentials to check the auth page's
    // list rows (in-memory backend, real KWallet untouched)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_STORED_CREDENTIALS")) {
        auto *wallet = stub->credentialBackend();
        Kontainer::CredentialStore store(wallet);
        store.open();
        Kontainer::RegistryCredential first;
        first.serverAddress = QStringLiteral("ghcr.io");
        first.username = QStringLiteral("alice");
        first.password = QStringLiteral("not-a-real-secret");
        store.store(first);
        Kontainer::RegistryCredential second;
        second.serverAddress = QStringLiteral("registry.example.com:5000");
        second.identityToken = QStringLiteral("ci-token");
        store.store(second);
        stub->controller()->registryAuth()->refresh();
    }

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("kcm"), stub.get());
    // The i18n stub must substitute %N, otherwise the rendered text stays literally
    // "%1 · created %2 ago · ID %3" instead of what KLocalizedString produces at runtime.
    // The translation map comes from the .po (KCM_DOCKER_RENDER_LANG); without it only English
    // renders, while real sessions run zh_CN — longer text, and wrapping/elision only show up in
    // a Chinese screenshot. It must live on the JS global object: functions defined inside
    // engine.evaluate() cannot see context properties (a ReferenceError: ktTranslations is not
    // defined makes all UI text vanish)
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


    // Let the controller finish one refresh first so pages have data to render
    stub->controller()->refresh();
    // KCM_DOCKER_RENDER_PULLS=1: one in-progress plus one failed pull, to check that the
    // progress bar, cancel button and failure reason are visible (ARCH_V4 §2.4)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_PULLS")) {
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

        // Fail only the second pull; the first stays in progress so all three states are visible
        backend->completeMutation(QStringLiteral("image:registry.example.com/team/app:2.4.1"),
                                  DockerBackendInterface::MutationOutcome::Failed,
                                  DockerError(DockerError::Kind::Timeout, QStringLiteral("no response headers within 10000 ms")));
    }

    // Volume list (phase 6 §3.5): in use / unused / usage unknown
    {
        QList<Kontainer::Volume> volumes;
        auto makeVolume = [](const QString &name, const QString &driver, qint64 size, int refs, bool usageKnown) {
            Kontainer::Volume volume;
            volume.name = name;
            volume.driver = driver;
            volume.mountpoint = QStringLiteral("/var/lib/docker/volumes/%1/_data").arg(name);
            volume.createdAt = QDateTime::currentDateTimeUtc().addSecs(-3600 * 12);
            volume.scope = QStringLiteral("local");
            volume.sizeBytes = usageKnown ? size : -1;
            volume.refCount = usageKnown ? refs : -1;
            if (name == QStringLiteral("app_data")) {
                volume.labels.append({QStringLiteral("com.docker.compose.project"), QStringLiteral("app")});
            }
            return volume;
        };
        volumes.append(makeVolume(QStringLiteral("app_data"), QStringLiteral("local"), 220200960, 2, true));
        volumes.append(makeVolume(QStringLiteral("app_cache"), QStringLiteral("local"), 52428800, 0, true));
        volumes.append(makeVolume(QStringLiteral("backup_2026"), QStringLiteral("local"), 0, 0, false));
        backend->setVolumes(volumes);
    }

    // Network list (phase 6 §3.2): the three built-ins plus one compose-created network with members
    {
        QList<Kontainer::Network> networks;
        auto makeNetwork = [](const QString &name, const QString &driver, const QString &subnet, int members) {
            Kontainer::Network network;
            network.id = QString(64, name.at(0));
            network.name = name;
            network.driver = driver;
            network.scope = QStringLiteral("local");
            network.created = QDateTime::currentDateTimeUtc().addSecs(-3600 * 30);
            if (!subnet.isEmpty()) {
                network.ipamConfigs.append({subnet, QStringLiteral("172.18.0.1")});
            }
            for (int i = 0; i < members; ++i) {
                Kontainer::NetworkMember member;
                member.containerId = QString(64, QLatin1Char('1'));
                member.name = i == 0 ? QStringLiteral("alpine") : QStringLiteral("app-%1").arg(i);
                member.ipv4Address = QStringLiteral("172.18.0.%1").arg(i + 2);
                member.macAddress = QStringLiteral("02:42:ac:12:00:0%1").arg(i + 2);
                network.members.append(member);
            }
            return network;
        };
        networks.append(makeNetwork(QStringLiteral("bridge"), QStringLiteral("bridge"), QStringLiteral("172.17.0.0/16"), 0));
        networks.append(makeNetwork(QStringLiteral("host"), QStringLiteral("host"), QString(), 0));
        networks.append(makeNetwork(QStringLiteral("none"), QStringLiteral("null"), QString(), 0));
        networks.append(makeNetwork(QStringLiteral("app_default"), QStringLiteral("bridge"), QStringLiteral("172.18.0.0/16"), 2));
        backend->setNetworks(networks);
    }

    backend->completeRefresh();

    const QString sourceDir = QStringLiteral(KCM_DOCKER_SOURCE_DIR "/src/ui/");
    QString qmlFile;
    QVariantMap initialProperties;
    if (page == QLatin1String("main") || page == QLatin1String("engine")) {
        qmlFile = QStringLiteral("MainPage.qml");
    } else if (page == QLatin1String("container-detail") || page == QLatin1String("container-detail-paused")) {
        qmlFile = QStringLiteral("ContainerDetail.qml");
        // container-detail-paused: the fixture's paused container, to check the Resume button
        initialProperties.insert(QStringLiteral("containerId"),
                                 page == QLatin1String("container-detail-paused")
                                     ? QStringLiteral("3333333333333333333333333333333333333333333333333333333333333333")
                                     : QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"));
    } else if (page == QLatin1String("image-detail")) {
        qmlFile = QStringLiteral("ImageDetail.qml");
        initialProperties.insert(QStringLiteral("imageId"), QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    } else if (page == QLatin1String("create-container")) {
        qmlFile = QStringLiteral("CreateContainer.qml");
    } else if (page == QLatin1String("network-detail")) {
        qmlFile = QStringLiteral("NetworkDetail.qml");
        // In the fixture, app_default's id is 64 'a' characters
        initialProperties.insert(QStringLiteral("networkId"), QString(64, QLatin1Char('a')));
    } else if (page == QLatin1String("registry-auth")) {
        qmlFile = QStringLiteral("RegistryAuthPage.qml");
    } else if (page == QLatin1String("daemon-config-user")) {
        qmlFile = QStringLiteral("DaemonConfigPage.qml");
        initialProperties.insert(QStringLiteral("scope"), QStringLiteral("user"));
    } else if (page == QLatin1String("daemon-config")) {
        // Daemon config page (ARCH_V5_V8 §2.3): content comes from the real filesystem, so
        // pointing HOME at a temp dir fakes the user-writable rootless shape (see render_ui.sh)
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

    // Optional: switch to a given section/tab for page-by-page review (e.g. container "network")
    // KCM_DOCKER_RENDER_REMOVE_NETWORK=1: open the network-removal confirmation dialog
    // (checks that the consequences are spelled out)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_REMOVE_NETWORK")) {
        QObject *removeDialog = item->findChild<QObject *>(QStringLiteral("removeNetworkDialog"));
        if (removeDialog) {
            QMetaObject::invokeMethod(removeDialog, "open");
        }
    }

    // KCM_DOCKER_RENDER_LOGS=1: feed the log console (checks the monospace console and status bar)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_LOGS")) {
        QList<Kontainer::LogLine> lines;
        const QStringList samples = {
            QStringLiteral("$ docker-entrypoint.sh node server.js"),
            QStringLiteral("info: listening on 0.0.0.0:8080"),
            QStringLiteral("info: connected to redis at redis:6379"),
            QStringLiteral("warn: cache miss for /api/v1/projects"),
            QStringLiteral("info: GET /api/v1/projects 200 12ms"),
            QStringLiteral("info: GET /api/v1/projects/42 200 7ms"),
            QStringLiteral("warn: retrying upstream request (1/3)"),
            QStringLiteral("error: upstream returned 503, serving cached response"),
            QStringLiteral("info: GET /healthz 200 1ms"),
            QStringLiteral("    at Object.<anonymous> (/app/server.js:118:22)"),
        };
        for (int round = 0; round < 3; ++round) {
            for (const QString &sample : samples) {
                Kontainer::LogLine line;
                line.text = sample;
                line.complete = true;
                line.stream = sample.startsWith(QLatin1String("error")) ? Kontainer::LogLine::Stream::Stderr
                                                                        : Kontainer::LogLine::Stream::Stdout;
                lines.append(line);
            }
        }
        backend->emitLogLines(QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"), lines);
    }

    // Runtime QML warnings must stay visible: a "missing piece" in a screenshot is usually a
    // delegate that never instantiated, and qWarning can be swallowed offscreen (print to stderr)
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings) {
            std::fprintf(stderr, "QML: %s\n", qPrintable(warning.toString()));
        }
    });

    // Dialogs (popups) **must open only after the window is ready**: their content is created
    // on open and is never laid out without a window, leaving a blank screenshot (hit in practice)
    QTimer::singleShot(600, &app, [&]() {
        // Mount presets, shared by the wizard's mount step and the presets tab
        // (one bind + one named volume, one favorite)
    stub->controller()->mountPresets()->add(QStringLiteral("/srv/data"), QStringLiteral("/data"),
                                            QStringLiteral("bind"), true, QStringLiteral("数据目录"));
    stub->controller()->mountPresets()->add(QStringLiteral("pgdata"), QStringLiteral("/var/lib/postgresql/data"),
                                            QStringLiteral("volume"), false, QString());
    stub->controller()->mountPresets()->setFavorite(QStringLiteral("preset-1"), true);

    // KCM_DOCKER_RENDER_WIZARD_STEP=<step key>: jump the creation wizard to a step (form layout)
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_WIZARD_STEP")) {
        auto *controller = stub->controller()->createContainer();
        const QString step = qEnvironmentVariable("KCM_DOCKER_RENDER_WIZARD_STEP");
        controller->setImage(QStringLiteral("postgres:17-alpine"));
        controller->setName(QStringLiteral("demo-container"));
        if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_PORT_CONFLICT")) {
            // Inline conflict hints: one row hits a running container (demo-app holds 8080), one is free
            controller->setPortRows({QVariantMap {{QStringLiteral("containerPort"), 5432},
                                                  {QStringLiteral("hostPort"), 8080},
                                                  {QStringLiteral("protocol"), QStringLiteral("tcp")}},
                                     QVariantMap {{QStringLiteral("containerPort"), 8080},
                                                  {QStringLiteral("hostPort"), 9000},
                                                  {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
        } else {
            controller->setPortRows({QVariantMap {{QStringLiteral("containerPort"), 5432},
                                                  {QStringLiteral("hostPort"), 15432},
                                                  {QStringLiteral("hostIp"), QStringLiteral("127.0.0.1")},
                                                  {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
        }
        controller->setEnvironmentRows({QVariantMap {{QStringLiteral("key"), QStringLiteral("POSTGRES_PASSWORD")},
                                                     {QStringLiteral("value"), QStringLiteral("not-a-real-secret")}}});
        controller->setMountRows({QVariantMap {{QStringLiteral("type"), QStringLiteral("volume")},
                                               {QStringLiteral("source"), QStringLiteral("pgdata")},
                                               {QStringLiteral("destination"), QStringLiteral("/var/lib/postgresql/data")},
                                               {QStringLiteral("readOnly"), false}}});
        controller->setNetwork(QStringLiteral("app_default"));
        const QString target = (step == QLatin1String("summary") || step == QLatin1String("all")) ? QStringLiteral("summary") : step;
        const bool moved = controller->goToStep(target);
        std::fprintf(stderr, "DBG wizard target=%s moved=%d now=%s error=%s\n", qPrintable(target), int(moved),
                     qPrintable(controller->stepKey()), qPrintable(controller->stepErrorKey()));
        // Step through to see which step blocks progress
        for (const QString &key : Kontainer::CreateContainerController::stepKeys()) {
            std::fprintf(stderr, "DBG   step %s error=%s\n", qPrintable(key), qPrintable(controller->stepErrorKeyForStep(key)));
        }
    }

    // KCM_DOCKER_RENDER_WIZARD_STEP=ports fills port rows, to check the node-graph style editor
    if (qEnvironmentVariable("KCM_DOCKER_RENDER_WIZARD_STEP") == QLatin1String("ports")) {
        auto *controller = stub->controller()->createContainer();
        controller->clearPortRows();
        controller->addPortRow(80, 8080, QStringLiteral("0.0.0.0"), QStringLiteral("tcp"));
        controller->addPortRow(443, 0, QStringLiteral("127.0.0.1"), QStringLiteral("tcp"));
        controller->addPortRow(53, 5353, QString(), QStringLiteral("udp"));
    }

    // KCM_DOCKER_RENDER_OPEN_BUILD=1: expand the image build form, add one running and one failed build
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_OPEN_BUILD")) {
        auto *controller = stub->controller();
        QQuickItem *entry = nullptr;
        std::function<void(QQuickItem *)> walkBuild = [&](QQuickItem *node) {
            if (!node || entry) {
                return;
            }
            if (node->objectName() == QLatin1String("buildImageEntryButton")) {
                entry = node;
                return;
            }
            for (QQuickItem *child : node->childItems()) {
                walkBuild(child);
            }
        };
        walkBuild(item);
        if (entry) {
            QMetaObject::invokeMethod(entry, "clicked");
        }
        // Two records: one running (step 3/7), one failed with its failing step
        QTemporaryDir *contextDir = new QTemporaryDir(); // lives until the process exits
        QFile dockerfile(QDir(contextDir->path()).filePath(QStringLiteral("Dockerfile")));
        if (dockerfile.open(QIODevice::WriteOnly)) {
            dockerfile.write("FROM alpine:3.19\nRUN make\n");
            dockerfile.close();
        }
        controller->operations()->buildImage(contextDir->path(), {QStringLiteral("demo-app:1.0")});
        controller->operations()->buildImage(contextDir->path(),
                                             {QStringLiteral("demo-app:2.0")},
                                             QStringLiteral("Dockerfile"),
                                             {},
                                             {},
                                             {},
                                             true);
        const QList<ImageBuildEntry> builds = controller->operations()->builds()->entries();
        if (builds.size() >= 2) {
            ImageBuildUpdate running;
            running.statusText = QStringLiteral("Step 3/7 : RUN make");
            running.stepIndex = 3;
            running.totalSteps = 7;
            running.stepCommand = QStringLiteral("RUN make");
            running.progress = 3.0 / 7.0;
            running.progressKnown = true;
            backend->emitBuildProgress(builds.first().id, running);

            ImageBuildUpdate failed;
            failed.statusText = QStringLiteral("Step 2/4 : RUN npm run build");
            failed.stepIndex = 2;
            failed.totalSteps = 4;
            failed.stepCommand = QStringLiteral("RUN npm run build");
            failed.errorText = QStringLiteral("Step 2/4 (RUN npm run build) failed: The command '/bin/sh -c npm run build' returned a non-zero code: 1");
            backend->emitBuildProgress(builds.last().id, failed);
            backend->emitBuildFinished(builds.last().id,
                                               DockerBackendInterface::MutationOutcome::Failed,
                                               DockerError(DockerError::Kind::EngineError, QStringLiteral("exit code 1")));
        }
    }

    // KCM_DOCKER_RENDER_OPEN_PRESETS=1: expand the wizard's preset management panel
    if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_OPEN_PRESETS")) {
        QQuickItem *manage = nullptr;
        std::function<void(QQuickItem *)> walkPresets = [&](QQuickItem *node) {
            if (!node || manage) {
                return;
            }
            if (node->objectName() == QLatin1String("wizardManagePresetsButton")) {
                manage = node;
                return;
            }
            const QList<QQuickItem *> children = node->childItems();
            for (QQuickItem *child : children) {
                walkPresets(child);
            }
        };
        walkPresets(item);
        if (manage) {
            QMetaObject::invokeMethod(manage, "clicked");
        }
    }

    // KCM_DOCKER_RENDER_CONNECT_NETWORK=1: expand the inline "connect to network" panel
        if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_CONNECT_NETWORK")) {
            QQuickItem *connectEntry = nullptr;
            std::function<void(QQuickItem *)> walkConnect = [&](QQuickItem *node) {
                if (!node || connectEntry) {
                    return;
                }
                if (node->objectName() == QLatin1String("connectNetworkEntryButton")) {
                    connectEntry = node;
                    return;
                }
                const QList<QQuickItem *> children = node->childItems();
                for (QQuickItem *child : children) {
                    walkConnect(child);
                }
            };
            walkConnect(item);
            if (connectEntry) {
                QMetaObject::invokeMethod(connectEntry, "clicked");
            }
        }

        // KCM_DOCKER_RENDER_CREATE_NETWORK=1: open the create-network dialog (form layout, validation)
        if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_CREATE_NETWORK")) {
            QQuickItem *createButton = nullptr;
            std::function<void(QQuickItem *)> walkCreate = [&](QQuickItem *node) {
                if (!node || createButton) {
                    return;
                }
                if (node->objectName() == QLatin1String("createNetworkEntryButton")) {
                    createButton = node;
                    return;
                }
                const QList<QQuickItem *> children = node->childItems();
                for (QQuickItem *child : children) {
                    walkCreate(child);
                }
            };
            walkCreate(item);
            if (createButton) {
                QMetaObject::invokeMethod(createButton, "clicked");
            }
        }


        // KCM_DOCKER_RENDER_OPEN_LOGIN=1: open the auth page's login dialog (checks its layout)
        if (qEnvironmentVariableIsSet("KCM_DOCKER_RENDER_OPEN_LOGIN")) {
            QMetaObject::invokeMethod(item, "openLoginDialog", Q_ARG(QString, QString()));
        }

        // KCM_DOCKER_RENDER_PORT_VIEW=map: switch the ports page to the range map (clustering, capping)
        if (qEnvironmentVariable("KCM_DOCKER_RENDER_PORT_VIEW") == QLatin1String("map")) {
            item->setProperty("portViewMode", QStringLiteral("map"));
        }

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

        // Switching sections triggers a refresh (e.g. the network page); let the fake backend
        // finish it, or the screenshot freezes on "no data yet" (real refreshes are async)
        backend->completeRefresh();

    });

    // Wait for layout and delegates (one event loop pass plus a short delay is enough)
    QTimer::singleShot(1200, &app, [&]() {
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
