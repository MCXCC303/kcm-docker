/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Write-access gate (ARCH_V4 §2.2.3 / §5.1).
 *
 * The check must be cheap, predictable and **biased to read-only when unsure**: hiding a write
 * entry point once is only inconvenient, showing one wrongly makes users without permission
 * keep hitting a wall.
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

/*! Create a real file to stand in for a socket (the check only reads permission bits). */
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
    const QString path = makeSocketFile(dir, QFile::ReadOwner); // read permission only
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
 * Read-only invariant for remote endpoints (ARCH_V3 §1.3): phase 4 does not implement remote
 * connections, but the rule is pinned now so a future TCP endpoint inherits it.
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
    // QML gates the write entry points on these three keys; renaming must be deliberate
    QCOMPARE(writeAccessKey(WriteAccess::Allowed), QStringLiteral("allowed"));
    QCOMPARE(writeAccessKey(WriteAccess::SocketMissing), QStringLiteral("denied"));
    QCOMPARE(writeAccessKey(WriteAccess::SocketNotWritable), QStringLiteral("denied"));
    QCOMPARE(writeAccessKey(WriteAccess::UnsupportedEndpoint), QStringLiteral("unsupported"));
}

QTEST_GUILESS_MAIN(DockerCapabilitiesTest)

#include "tst_docker_capabilities.moc"
