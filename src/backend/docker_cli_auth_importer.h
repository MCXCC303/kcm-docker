/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"
#include "backend/registry_auth.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*! One credential read from `~/.docker/config.json`. */
struct ImportableCredential {
    RegistryCredential credential;
    /*! Raw key in the config file (e.g. `https://index.docker.io/v1/`), shown as the source. */
    QString sourceKey;
};

/*! Result of one import scan. */
struct DockerCliAuthScan {
    /*! Path actually read. */
    QString path;
    /*! Failure key: empty = success; `missingFile` / `unreadable` / `invalidJson`. */
    QString errorKey;
    /*! Importable credentials. */
    QList<ImportableCredential> credentials;
    /*!
     * Entries managed by credential helpers (`credsStore` / `credHelpers`).
     *
     * Helpers are **never invoked** (ARCH_V5_V8 §2.9): they hand credentials to external programs,
     * which exceeds read-only import and pops up keychain dialogs behind the user's back. But the
     * entries must stay visible, or users wonder why their registry was not imported.
     */
    QStringList helperManagedKeys;
    /*!
     * Entries read but unusable: missing `auth`, broken base64/fields, or a duplicate of another
     * entry pointing at the same registry.
     *
     * The UI must report these honestly ("N entries cannot be imported") instead of pretending the
     * file only holds the importable ones.
     */
    QStringList skippedKeys;
    /*!
     * Docker's own token cache entries (`<server>/access-token` / `<server>/refresh-token`).
     *
     * They are **not** registry credentials (usually cached OAuth tokens). Measured: the local
     * `~/.docker/config.json` holds two; stripping the path part collides with the real Hub entry
     * and would import a cached token as the password. So they are matched explicitly and ignored.
     */
    QStringList tokenCacheKeys;
};

/*!
 * One-shot **read-only** import of credentials from the Docker CLI `config.json`
 * (ARCH_V5_V8 §2.6/§2.7).
 *
 * Three deliberate restrictions:
 *   1. **Read-only**: never write the file back (it belongs to the CLI, credentials belong to
 *      KWallet);
 *   2. **No credential helper calls**;
 *   3. **Never overwrite existing wallet entries** (see `importInto()`) — silent overwrite loses a
 *      password the user just set.
 *
 * Note: `auth` there is standard base64 `user:password`, i.e. plaintext-equivalent; scan results
 * therefore stay in memory only — never logged, never in error messages.
 */
class DockerCliAuthImporter
{
public:
    /*! CLI config path: `$DOCKER_CONFIG/config.json`, else `~/.docker/config.json`. */
    static QString defaultConfigPath();

    /*! Scan the config (read-only). A missing file is not an error, just no importable entries. */
    static DockerCliAuthScan scan(const QString &path);

    /*! Result of one import, for honest UI reporting. */
    struct ImportOutcome {
        /*! Entries actually written to the wallet. */
        int imported = 0;
        /*! Entries already in the wallet and therefore **not** overwritten. */
        int alreadyPresent = 0;
        /*! Entries that failed to store. */
        int failed = 0;
    };

    /*!
     * Import scan results into the wallet, skipping existing entries.
     *
     * This keeps import idempotent and avoids reverting a password the user just changed in the UI
     * back to the old value from the file.
     */
    static ImportOutcome importInto(CredentialStore &store, const DockerCliAuthScan &scan);
};

} // namespace Kontainer
