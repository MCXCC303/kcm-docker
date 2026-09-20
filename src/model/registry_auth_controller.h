/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"
#include "backend/docker_backend_interface.h"
#include "backend/docker_cli_auth_importer.h"

#include <QObject>
#include <QString>
#include <QStringList>

// moc needs the complete type to generate metatype code for the pointer property; this include is
// for moc only — users (.cpp) include it themselves so the model implementation is not dragged in
Q_MOC_INCLUDE("model/registry_credential_model.h")

namespace Kontainer
{

class RegistryCredentialModel;

/*!
 * UI controller for registry authentication (ARCH_V5_V8 §2.6/§2.7).
 *
 * The rules live here; QML only displays and collects input:
 *
 *  - **validate before saving**: a login reaches the wallet only after `POST /auth` succeeds
 *    (everything stored has been verified); a failed check stores nothing and keeps the password
 *    nowhere;
 *  - **an unavailable wallet only degrades, never falls back**: no plain-text write, no pretending
 *    success; the UI explains the cause from `walletUnavailableReason`;
 *  - **CLI import is read-only and never overwrites**: scanning and importing belong to
 *    `DockerCliAuthImporter`, this class only tracks state and reports;
 *  - **the password/token never leaves this controller**: `login()` takes it, `POST /auth` consumes
 *    it, and it appears in no model or signal.
 *
 * User-visible text is not in C++: this returns stable keys only (`invalidCredentials` /
 * `registryUnreachable`…) which QML maps (ARCH_V3 §2.6).
 */
class RegistryAuthController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::RegistryCredentialModel *credentials READ credentials CONSTANT)
    /*! Wallet state: `closed` / `opening` / `ready` / `unavailable`. */
    Q_PROPERTY(QString walletStateKey READ walletStateKey NOTIFY changed)
    /*! Unavailable-reason key: `walletDisabled` / `walletOpenFailed` / `walletFolderFailed`. */
    Q_PROPERTY(QString walletUnavailableReason READ walletUnavailableReason NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /*! Result key of the last action (empty on success — look at lastErrorKey then). */
    Q_PROPERTY(QString lastResultKey READ lastResultKey NOTIFY resultChanged)
    Q_PROPERTY(QString lastErrorKey READ lastErrorKey NOTIFY resultChanged)
    /*! Raw engine text for "technical details" only (may contain registry text); never user wording. */
    Q_PROPERTY(QString lastErrorDetail READ lastErrorDetail NOTIFY resultChanged)

    /* ---------------- docker CLI import (read-only scan + per-entry import) ---------------- */
    Q_PROPERTY(bool cliConfigPresent READ cliConfigPresent NOTIFY importScanChanged)
    Q_PROPERTY(QString cliConfigPath READ cliConfigPath NOTIFY importScanChanged)
    /*! Registries present in the CLI but not yet in the wallet (selectable for import). */
    Q_PROPERTY(QStringList importableAddresses READ importableAddresses NOTIFY importScanChanged)
    /*! Entries managed by credsStore/credHelpers: we **never run** external credential programs. */
    Q_PROPERTY(QStringList helperManagedKeys READ helperManagedKeys NOTIFY importScanChanged)
    /*! Entries read but unusable (missing auth, corrupt, or pointing at the same registry as another). */
    Q_PROPERTY(QStringList skippedImportKeys READ skippedImportKeys NOTIFY importScanChanged)
    Q_PROPERTY(int lastImportedCount READ lastImportedCount NOTIFY resultChanged)
    Q_PROPERTY(int lastAlreadyPresentCount READ lastAlreadyPresentCount NOTIFY resultChanged)
    Q_PROPERTY(int lastImportFailedCount READ lastImportFailedCount NOTIFY resultChanged)

public:
    RegistryAuthController(DockerBackendInterface *backend, CredentialStore *store, QObject *parent = nullptr);

    RegistryCredentialModel *credentials() const;

    QString walletStateKey() const;
    QString walletUnavailableReason() const;
    bool busy() const;
    QString lastResultKey() const;
    QString lastErrorKey() const;
    QString lastErrorDetail() const;
    bool cliConfigPresent() const;
    QString cliConfigPath() const;
    QStringList importableAddresses() const;
    QStringList helperManagedKeys() const;
    QStringList skippedImportKeys() const;
    int lastImportedCount() const;
    int lastAlreadyPresentCount() const;
    int lastImportFailedCount() const;

    /*! Open the wallet, refresh the list, scan the CLI config read-only (on page entry, idempotent). */
    Q_INVOKABLE void refresh();
    /*!
     * Log in: validate with `POST /auth` first and write to the wallet only on success.
     *
     * A non-empty `token` logs in by token (no username or password is sent).
     */
    Q_INVOKABLE void login(const QString &serverAddress, const QString &username, const QString &password, const QString &token = {});
    /*! Test the connection with stored credentials (changes nothing). */
    Q_INVOKABLE void testCredential(const QString &serverAddress);
    /*! Remove a credential (the UI asks for confirmation). */
    Q_INVOKABLE void removeCredential(const QString &serverAddress);
    /*! Rescan the CLI config read-only (internal; the UI no longer has a "sync" action). */
    void scanCliConfig();
    /*! Clear the last result (used when the notice bar is dismissed). */
    Q_INVOKABLE void clearResult();
    /*!
     * Whether credentials exist for the registry of this image (for the pre-pull hint).
     *
     * Registry resolution goes through `RegistryAuth::serverAddressForImage`: the UI must not parse
     * references itself.
     */
    Q_INVOKABLE bool hasCredentialForImage(const QString &imageReference) const;
    /*! Registry address for the image (pre-filled into the login dialog). */
    Q_INVOKABLE QString serverAddressForImage(const QString &imageReference) const;

Q_SIGNALS:
    void changed();
    void resultChanged();
    void importScanChanged();
    /*! Credential set changed (imports included); the UI notifies from it. */
    void credentialsChanged();

private:
    /*!
     * Silently pick up CLI config entries into the wallet (existing entries are never overwritten).
     *
     * Runs on every `refresh()`: a registry the user logged into with `docker login` is already in
     * the list when this page opens, so no "import" button is needed.
     */
    void importFromCliSilently();
    /*! Put the CLI config path into `lastErrorDetail` (a failed sync must name the file). */
    void setCliConfigPathForMessages();
    /*! Write one credential back to the CLI config; on failure fills `errorKey` and returns false. */
    static bool writeBackToCli(const RegistryCredential &credential, QString *errorKey);

    void setResultKeys(const QString &resultKey, const QString &errorKey, const QString &detail = {});
    void setBusy(bool busy);
    /*! Translate `DockerBackendInterface::AuthCheckResult` into a UI key. */
    static QString keyForAuthResult(DockerBackendInterface::AuthCheckResult result);
    void handleAuthCheckFinished(const QString &serverAddress, DockerBackendInterface::AuthCheckResult result, const QString &detail);

    DockerBackendInterface *m_backend = nullptr;
    CredentialStore *m_store = nullptr;
    RegistryCredentialModel *m_credentials = nullptr;

    /*! Login request awaiting validation (written to the wallet only after it passes). */
    struct PendingLogin {
        RegistryCredential credential;
        bool active = false;
    };
    PendingLogin m_pendingLogin;
    /*! A "test connection" is running rather than a "login". */
    bool m_testingCredential = false;

    DockerCliAuthScan m_scan;
    bool m_scanDone = false;
    int m_importedCount = 0;
    int m_alreadyPresentCount = 0;
    int m_importFailedCount = 0;

    bool m_busy = false;
    QString m_lastResultKey;
    QString m_lastErrorKey;
    QString m_lastErrorDetail;
};

} // namespace Kontainer
