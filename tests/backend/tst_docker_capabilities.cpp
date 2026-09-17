/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_capabilities.h"
#include "backend/docker_endpoint.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * 写权限门（ARCH_V4 §2.2.3 / §5.1）。
 *
 * 判定必须便宜、可预测，并且**在拿不准时倾向于只读**：
 * 写入口少出现一次只是不方便，多出现一次可能让用户在没权限的环境里反复撞墙。
 */
class DockerCapabilitiesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writableSocketIsAllowed();
    void readOnlySocketIsDenied();
    void missingSocketIsDenied();
    void remoteOrInvalidEndpointIsUnsupported();
    void remoteEndpointsNeverBecomeWritable();
    void keysAreStable();
};

namespace
{

/*! 建一个真实文件当 socket 用（判定只看权限位，不看文件类型）。 */
QString makeSocketFile(const QTemporaryDir &dir, QFile::Permissions permissions)
{
    const QString path = dir.path() + QStringLiteral("/docker.sock");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write("x");
    file.close();
    QFile::setPermissions(path, permissions);
    return path;
}

} // namespace

void DockerCapabilitiesTest::writableSocketIsAllowed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeSocketFile(dir, QFile::ReadOwner | QFile::WriteOwner);
    QVERIFY(!path.isEmpty());

    QCOMPARE(writeAccessFor(DockerEndpoint::unixSocket(path)), WriteAccess::Allowed);
    QVERIFY(writeAccessAllowed(writeAccessFor(DockerEndpoint::unixSocket(path))));
}

void DockerCapabilitiesTest::readOnlySocketIsDenied()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeSocketFile(dir, QFile::ReadOwner); // 只有读权限
    QVERIFY(!path.isEmpty());

    QCOMPARE(writeAccessFor(DockerEndpoint::unixSocket(path)), WriteAccess::SocketNotWritable);
    QCOMPARE(writeAccessKey(writeAccessFor(DockerEndpoint::unixSocket(path))), QStringLiteral("denied"));
    QVERIFY(!writeAccessAllowed(writeAccessFor(DockerEndpoint::unixSocket(path))));
}

void DockerCapabilitiesTest::missingSocketIsDenied()
{
    const QString path = QDir::tempPath() + QStringLiteral("/kontainer-does-not-exist.sock");
    QFile::remove(path);
    QCOMPARE(writeAccessFor(DockerEndpoint::unixSocket(path)), WriteAccess::SocketMissing);
    QCOMPARE(writeAccessKey(writeAccessFor(DockerEndpoint::unixSocket(path))), QStringLiteral("denied"));
}

void DockerCapabilitiesTest::remoteOrInvalidEndpointIsUnsupported()
{
    QCOMPARE(writeAccessFor(DockerEndpoint()), WriteAccess::UnsupportedEndpoint);
    QCOMPARE(writeAccessKey(writeAccessFor(DockerEndpoint())), QStringLiteral("unsupported"));
}

/*!
 * 远程 endpoint 的只读不变式（ARCH_V3 §1.3）：四期不实现远程连接，
 * 但这条判断必须先写死，未来加 TCP endpoint 时自动继承。
 */
void DockerCapabilitiesTest::remoteEndpointsNeverBecomeWritable()
{
    for (const QString &value : {QStringLiteral("tcp://127.0.0.1:2375"), QStringLiteral("ssh://user@host"), QStringLiteral("npipe:////./pipe/docker")}) {
        qputenv("DOCKER_HOST", value.toUtf8());
        const DockerEndpoint endpoint = DockerEndpoint::fromEnvironment();
        if (endpoint.isValid()) {
            QVERIFY2(!writeAccessAllowed(writeAccessFor(endpoint)), qPrintable(value));
        }
    }
    qunsetenv("DOCKER_HOST");
}

void DockerCapabilitiesTest::keysAreStable()
{
    // QML 依赖这三个 key 决定是否渲染写入口，改名必须是显式决定
    QCOMPARE(writeAccessKey(WriteAccess::Allowed), QStringLiteral("allowed"));
    QCOMPARE(writeAccessKey(WriteAccess::SocketMissing), QStringLiteral("denied"));
    QCOMPARE(writeAccessKey(WriteAccess::SocketNotWritable), QStringLiteral("denied"));
    QCOMPARE(writeAccessKey(WriteAccess::UnsupportedEndpoint), QStringLiteral("unsupported"));
}

QTEST_GUILESS_MAIN(DockerCapabilitiesTest)

#include "tst_docker_capabilities.moc"
