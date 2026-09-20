/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QString>

namespace Kontainer
{

/*!
 * Write credentials back to the Docker CLI `config.json` (follow-up to ARCH_V5_V8 §2.6).
 *
 * Credentials still **belong** to KWallet (no plaintext, no password leaving the controller), but
 * users want to log in once in Kontainer and have `docker login` / `docker pull` work, with no
 * "sync/import" buttons in the UI. Hence:
 *
 *   - CLI → us: `DockerCliAuthImporter` does it **silently** when the page opens;
 *   - us → CLI: done here — every added/changed/removed credential syncs one `auths` entry.
 *
 * Three safety constraints:
 *   1. **Never overwrite a broken file**: on parse failure return an error and write nothing (the
 *      user's config may hold more than we understand; clobbering it loses his login state);
 *   2. **Preserve unknown keys**: `credsStore` / `credHelpers` / other registry entries stay as is;
 *   3. **Permissions**: file 0600, directory 0700 (it holds plaintext-equivalent credentials).
 */
class DockerCliAuthWriter
{
public:
    /*! CLI config path: `$DOCKER_CONFIG/config.json`, else `~/.docker/config.json`. */
    static QString defaultConfigPath();

    /*!
     * Add or update one credential (keeping other fields in that entry, e.g. `identitytoken`).
     *
     * @param errorKey failure reason: `invalidJson` / `unwritable` / `noCredentials`.
     */
    static bool upsert(const QString &path,
                       const QString &serverAddress,
                       const RegistryCredential &credential,
                       QString *errorKey = nullptr);

    /*! Delete one credential (success even if the entry never existed). */
    static bool remove(const QString &path, const QString &serverAddress, QString *errorKey = nullptr);

    /*!
     * Key for this registry under `auths`: reuse the spelling already in the file when present (one
     * registry may appear as `https://index.docker.io/v1/` or `index.docker.io`), else the
     * normalized address.
     */
    static QString configKeyFor(const QString &path, const QString &serverAddress);
};

} // namespace Kontainer
