/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*! State of a host path (ARCH_V4 §2.1.1). */
enum class HostPathState {
    /*! No host path (tmpfs, anonymous mount): the UI offers no open action. */
    NotApplicable,
    Missing,
    /*! Path exists but is not a directory: happens when a bind mount points at a file. */
    NotADirectory,
    Directory,
};

/*! Why opening a directory failed (the backend gives the cause, never UI text). */
enum class HostPathError {
    None,
    Missing,
    NotADirectory,
    LaunchFailed,
};

/*!
 * Host path service (ARCH_V4 §2.1.1).
 *
 * Why a separate interface: this is the only external action not going through Docker, and
 * splitting it out means
 *  - probe and open can be replaced by a Fake in tests (no real file manager pops up)
 *  - production KIO references stay confined to one file (asserted by tst_source_conventions)
 *
 * Safety: stat only, never read file contents or recurse; mount source paths are potentially
 * sensitive, so neither implementations nor callers may log them (ARCH_V2 §40).
 */
class HostPathService : public QObject
{
    Q_OBJECT

public:
    explicit HostPathService(QObject *parent = nullptr);
    ~HostPathService() override;

    /*! Probe the path state with one stat, nothing more. */
    virtual HostPathState probe(const QString &path) const = 0;
    /*!
     * Open the directory in the system file manager.
     *
     * false means the request was rejected locally (nothing was launched); the real outcome
     * arrives asynchronously via openFinished.
     */
    virtual bool openDirectory(const QString &path) = 0;

Q_SIGNALS:
    /*! `detail` is technical detail for logs/debugging, not user-facing text. */
    void openFinished(Kontainer::HostPathError error, const QString &detail);
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::HostPathState)
Q_DECLARE_METATYPE(Kontainer::HostPathError)
