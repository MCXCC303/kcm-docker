/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/directory_picker.h"

#include "logging.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

#include <cstring>
#include <QUuid>

namespace Kontainer
{

namespace
{
constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";
} // namespace

DirectoryPicker::DirectoryPicker(QObject *parent)
    : QObject(parent)
{
}

DirectoryPicker::~DirectoryPicker() = default;

QVariantMap DirectoryPickerProtocol::openFileOptions(const QString &handleToken)
{
    QVariantMap options;
    // directory=true：这是挑目录。少了它门户会弹"选文件"的对话框（用户会以为功能坏了）
    options.insert(QStringLiteral("directory"), true);
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("handle_token"), handleToken);
    return options;
}

QString DirectoryPickerProtocol::requestHandle(const QString &uniqueName, const QString &handleToken)
{
    if (uniqueName.isEmpty() || handleToken.isEmpty()) {
        return {};
    }
    // 门户规范：把本连接唯一名的前导冒号去掉，再把 '.' 换成 '_'
    QString sender = uniqueName;
    if (sender.startsWith(QLatin1Char(':'))) {
        sender.remove(0, 1);
    }
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + QLatin1Char('/') + handleToken;
}

QString DirectoryPickerProtocol::localPathFromUri(const QString &uri)
{
    if (!uri.startsWith(QLatin1String("file:"))) {
        return {}; // 非 file 方案（smb://、ftp://…）本机用不了
    }
    // RFC 8089 的本地形式：file://localhost/path 等价于 file:///path。
    // Qt 会把 "localhost" 留在路径里（toLocalFile() 得到 "//localhost/tmp"），因此先归一化。
    if (uri.startsWith(QLatin1String("file://localhost/"))) {
        // 去掉 "file://localhost"，剩下的就是路径（注意它已经不是 URL 了，不能再喂给 QUrl）
        return QUrl::fromPercentEncoding(uri.mid(int(strlen("file://localhost"))).toUtf8());
    }
    const QUrl url(uri);
    if (!url.isValid()) {
        return {};
    }
    // 其余带主机的 file:// URL 指向别的机器：宁可当作"没选"
    if (!url.host().isEmpty()) {
        return {};
    }
    return url.toLocalFile();
}

QString DirectoryPickerProtocol::chosenPathFromResponse(uint response, const QVariantMap &results)
{
    if (response != 0) {
        return {}; // 1 = 用户取消，2 = 其它错误
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty()) {
        return {};
    }
    return localPathFromUri(uris.first());
}

PortalDirectoryPicker::PortalDirectoryPicker(QObject *parent)
    : DirectoryPicker(parent)
{
    /*
     * 门户服务掉线（进程崩溃/重启）时不会有 Response 回来，等待方会永远挂着。
     * 实测本机的 xdg-desktop-portal-kde 会崩在 KIO 的文件控件里（ARCH §5.16），
     * 因此这里必须自己兜住：服务主人变了 → 用 Qt 对话框把这次请求补完。
     */
    if (QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface()) {
        connect(iface, &QDBusConnectionInterface::serviceOwnerChanged, this, [this](const QString &name, const QString &, const QString &newOwner) {
            if (name != QLatin1String(kPortalService) || !newOwner.isEmpty() || m_pending.isEmpty()) {
                return;
            }
            qCWarning(kontainerBackend) << "the desktop portal disappeared while a directory request was pending -"
                                           " falling back to the Qt dialog";
            const QList<QString> pending = m_pending.keys();
            m_pending.clear();
            m_subscriptions.clear();
            for (const QString &requestId : pending) {
                fallBackToQtDialog(requestId, QString());
            }
        });
    }

    // 宽松的安全网：用户挑目录本来就可能花几分钟，但"永远等不到"必须有个头
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(false);
    m_watchdog->setInterval(60 * 1000);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        if (m_pending.isEmpty()) {
            m_watchdog->stop();
            return;
        }
        const QList<QString> pending = m_pending.keys();
        m_pending.clear();
        m_subscriptions.clear();
        m_watchdog->stop();
        qCWarning(kontainerBackend) << "no answer from the desktop portal for" << pending.size() << "request(s) - giving up";
        for (const QString &requestId : pending) {
            // 给界面一个明确的结束（空路径 = 没选），避免"点了没反应"的状态一直挂着
            Q_EMIT directoryChosen(requestId, QString());
        }
    });
    m_watchdog->setInterval(5 * 60 * 1000);
}

bool PortalDirectoryPicker::portalAvailable()
{
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QString::fromLatin1(kPortalService));
}

void PortalDirectoryPicker::chooseDirectory(const QString &requestId, const QString &startPath)
{
    if (requestId.isEmpty()) {
        return; // 没有标签就无法把结果写回正确的位置
    }
    if (!portalAvailable()) {
        // 没有门户（精简会话、测试）：退回 Qt 自带对话框。
        // 那条路径**不经过** KIO 的进程内实现，因此不会重现 §5.14 的崩溃。
        fallBackToQtDialog(requestId, startPath);
        return;
    }

    const QString token = QStringLiteral("kontainer_") + QUuid::createUuid().toString(QUuid::Id128);
    const QString handle = DirectoryPickerProtocol::requestHandle(QDBusConnection::sessionBus().baseService(), token);
    m_pending.insert(requestId, handle);

    /*
     * 规范要求**先订阅再调用**：门户可能在方法返回之前就发出 Response。
     *
     * 用带捕获的 lambda 而不是槽：requestId 直接闭包进来，收到结果时就能准确知道
     * 该写回哪一行（同时有好几行在等结果时不能张冠李戴），也不依赖"从信号路径反查"。
     */
    const auto subscribe = [this, requestId](const QString &path) {
        const bool ok = QDBusConnection::sessionBus().connect(QString::fromLatin1(kPortalService),
                                                              path,
                                                              QString::fromLatin1(kRequestInterface),
                                                              QStringLiteral("Response"),
                                                              this,
                                                              SLOT(handlePortalResponse(uint, QVariantMap)));
        Q_UNUSED(ok)
        m_subscriptions.insert(path, requestId);
    };
    subscribe(handle);

    QVariantMap options = DirectoryPickerProtocol::openFileOptions(token);
    if (!startPath.isEmpty() && QFileInfo(startPath).isDir()) {
        // 门户用 current_folder（本地编码的字节串路径），不是"起始 URL"
        options.insert(QStringLiteral("current_folder"), QFile::encodeName(startPath));
    }

    QDBusInterface chooser(QString::fromLatin1(kPortalService),
                           QString::fromLatin1(kPortalPath),
                           QString::fromLatin1(kFileChooserInterface),
                           QDBusConnection::sessionBus());
    // 父窗口传空：拿 QWindow 的 wayland handle 需要额外的 xdg-foreign 交互，
    // 对"挑一个目录"没有实际影响
    const QDBusMessage reply = chooser.call(QStringLiteral("OpenFile"), QString(), tr("Choose a directory"), options);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(kontainerBackend) << "portal OpenFile failed:" << reply.errorMessage() << "- using the Qt dialog instead";
        m_pending.remove(requestId);
        fallBackToQtDialog(requestId, startPath);
        return;
    }

    // 门户返回的 handle 一般等于我们算出来的那个；若不同则以返回值为准
    const QString returned = reply.arguments().value(0).toString();
    if (!returned.isEmpty() && returned != handle) {
        m_pending.insert(requestId, returned);
        subscribe(returned);
    }
    if (!m_watchdog->isActive()) {
        m_watchdog->start();
    }
}

void PortalDirectoryPicker::handlePortalResponse(uint response, const QVariantMap &results)
{
    /*
     * 槽里拿不到信号路径，用订阅表反查。
     *
     * 多个请求同时等待时按"最早订阅的"处理是可以接受的：门户对同一个 handle 只会回一次，
     * 而每个 handle 只属于一个 requestId；真正的多路复用由"每个请求各自订阅自己的 handle"保证。
     */
    if (m_subscriptions.isEmpty()) {
        return;
    }
    const QString path = m_subscriptions.constBegin().key();
    const QString requestId = m_subscriptions.constBegin().value();
    m_subscriptions.remove(path);
    m_pending.remove(requestId);
    Q_EMIT directoryChosen(requestId, DirectoryPickerProtocol::chosenPathFromResponse(response, results));
}

void PortalDirectoryPicker::fallBackToQtDialog(const QString &requestId, const QString &startPath)
{
    QString start = startPath;
    if (start.isEmpty() || !QFileInfo(start).isDir()) {
        start = QDir::homePath();
    }
    /*
     * `DontUseNativeDialog` 是必须的（ARCH §5.14）：Plasma 下 QFileDialog 的"原生"实现
     * 是 KIO 的 KFileWidget，在 kcmshell6 里绘制会段错误。这里只作为门户不可用时的退路。
     */
    const auto options = QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog;
    const QString chosen = QFileDialog::getExistingDirectory(nullptr, tr("Choose a directory"), start, options);
    Q_EMIT directoryChosen(requestId, chosen);
}

} // namespace Kontainer
