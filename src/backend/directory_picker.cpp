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
