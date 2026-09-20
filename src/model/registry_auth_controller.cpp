/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/registry_auth_controller.h"

#include "backend/docker_cli_auth_writer.h"

#include "logging.h"
#include "model/registry_credential_model.h"

#include <algorithm>

namespace Kontainer
{

namespace
{
/*! Wallet state → UI key. */
QString walletStateKeyFor(CredentialStore::State state)
{
    switch (state) {
    case CredentialStore::State::Closed:
        return QStringLiteral("closed");
    case CredentialStore::State::Opening:
        return QStringLiteral("opening");
    case CredentialStore::State::Ready:
        return QStringLiteral("ready");
    case CredentialStore::State::Unavailable:
        break;
    }
    return QStringLiteral("unavailable");
}
} // namespace

RegistryAuthController::RegistryAuthController(DockerBackendInterface *backend, CredentialStore *store, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_store(store)
    , m_credentials(new RegistryCredentialModel(store, this))
{
    Q_ASSERT(m_backend);
    Q_ASSERT(m_store);

    connect(m_backend, &DockerBackendInterface::registryAuthChecked, this, &RegistryAuthController::handleAuthCheckFinished);
    connect(m_store, &CredentialStore::changed, this, [this] {
        Q_EMIT changed();
        Q_EMIT credentialsChanged();
    });
    connect(m_store, &CredentialStore::stateChanged, this, &RegistryAuthController::changed);
}

RegistryCredentialModel *RegistryAuthController::credentials() const
{
    return m_credentials;
}

QString RegistryAuthController::walletStateKey() const
{
    return walletStateKeyFor(m_store->state());
}

QString RegistryAuthController::walletUnavailableReason() const
{
    return m_store->unavailableReason();
}

bool RegistryAuthController::busy() const
{
    return m_busy;
}

QString RegistryAuthController::lastResultKey() const
{
    return m_lastResultKey;
}

QString RegistryAuthController::lastErrorKey() const
{
    return m_lastErrorKey;
}

QString RegistryAuthController::lastErrorDetail() const
{
    return m_lastErrorDetail;
}

bool RegistryAuthController::cliConfigPresent() const
{
    return m_scanDone && m_scan.errorKey.isEmpty() && !m_scan.path.isEmpty();
}

QString RegistryAuthController::cliConfigPath() const
{
    return m_scan.path;
}

QStringList RegistryAuthController::importableAddresses() const
{
    QStringList addresses;
    for (const ImportableCredential &importable : m_scan.credentials) {
        // skip registries already in the wallet (importing would not overwrite them either)
        if (!m_store->hasCredential(importable.credential.serverAddress)) {
            addresses.append(importable.credential.serverAddress);
        }
    }
    addresses.removeDuplicates();
    addresses.sort();
    return addresses;
}

QStringList RegistryAuthController::helperManagedKeys() const
{
    return m_scan.helperManagedKeys;
}

QStringList RegistryAuthController::skippedImportKeys() const
{
    return m_scan.skippedKeys;
}

int RegistryAuthController::lastImportedCount() const
{
    return m_importedCount;
}

int RegistryAuthController::lastAlreadyPresentCount() const
{
    return m_alreadyPresentCount;
}

int RegistryAuthController::lastImportFailedCount() const
{
    return m_importFailedCount;
}

bool RegistryAuthController::hasCredentialForImage(const QString &imageReference) const
{
    const QString address = RegistryAuth::serverAddressForImage(imageReference);
    return !address.isEmpty() && m_store->hasCredential(address);
}

QString RegistryAuthController::serverAddressForImage(const QString &imageReference) const
{
    return RegistryAuth::serverAddressForImage(imageReference);
}

void RegistryAuthController::refresh()
{
    m_store->open();
    m_credentials->reload();
    scanCliConfig();
    // silent pickup: entries in the CLI config but not in the wallet (existing ones are never
    // overwritten). The UI has no import/sync button any more — as users requested (ARCH_V5_V8 §5.19).
    importFromCliSilently();
    Q_EMIT changed();
}

void RegistryAuthController::importFromCliSilently()
{
    if (m_store->state() != CredentialStore::State::Ready) {
        return; // wallet not ready: leave it alone and retry on the next refresh
    }
    const DockerCliAuthImporter::ImportOutcome outcome = DockerCliAuthImporter::importInto(*m_store, m_scan);
    m_importedCount = outcome.imported;
    m_alreadyPresentCount = outcome.alreadyPresent;
    m_importFailedCount = outcome.failed;
    if (outcome.imported > 0) {
        m_credentials->reload();
        qCDebug(kontainerModel) << "recognized" << outcome.imported << "credential(s) from the docker cli config";
        Q_EMIT credentialsChanged();
    }
}

void RegistryAuthController::clearResult()
{
    setResultKeys(QString(), QString());
}

void RegistryAuthController::setResultKeys(const QString &resultKey, const QString &errorKey, const QString &detail)
{
    if (m_lastResultKey == resultKey && m_lastErrorKey == errorKey && m_lastErrorDetail == detail) {
        return;
    }
    m_lastResultKey = resultKey;
    m_lastErrorKey = errorKey;
    m_lastErrorDetail = detail;
    Q_EMIT resultChanged();
}

void RegistryAuthController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT changed();
}

QString RegistryAuthController::keyForAuthResult(DockerBackendInterface::AuthCheckResult result)
{
    switch (result) {
    case DockerBackendInterface::AuthCheckResult::Succeeded:
        return QStringLiteral("succeeded");
    case DockerBackendInterface::AuthCheckResult::InvalidCredentials:
        return QStringLiteral("invalidCredentials");
    case DockerBackendInterface::AuthCheckResult::RegistryUnreachable:
        return QStringLiteral("registryUnreachable");
    case DockerBackendInterface::AuthCheckResult::Failed:
        break;
    }
    return QStringLiteral("failed");
}

void RegistryAuthController::login(const QString &serverAddress, const QString &username, const QString &password, const QString &token)
{
    RegistryCredential credential;
    credential.serverAddress = RegistryAuth::normalizeServerAddress(serverAddress);
    if (credential.serverAddress.isEmpty()) {
        setResultKeys(QString(), QStringLiteral("invalidServerAddress"));
        return;
    }
    if (!token.isEmpty()) {
        credential.identityToken = token;
    } else {
        credential.username = username;
        credential.password = password;
    }
    if (credential.isEmpty()) {
        setResultKeys(QString(), QStringLiteral("noCredentials"));
        return;
    }
    if (m_store->state() != CredentialStore::State::Ready) {
        // wallet unavailable: validate nothing and save nothing — a passed check could not be stored anyway
        setResultKeys(QString(), QStringLiteral("unavailable"));
        return;
    }

    m_pendingLogin.credential = credential;
    m_pendingLogin.active = true;
    m_testingCredential = false;
    setBusy(true);
    setResultKeys(QString(), QString());
    m_backend->checkRegistryAuth(credential.serverAddress, credential);
}

void RegistryAuthController::testCredential(const QString &serverAddress)
{
    const QString address = RegistryAuth::normalizeServerAddress(serverAddress);
    if (address.isEmpty()) {
        setResultKeys(QString(), QStringLiteral("invalidServerAddress"));
        return;
    }
    const RegistryCredential credential = m_store->credential(address);
    if (credential.isEmpty()) {
        setResultKeys(QString(), QStringLiteral("noCredentialStored"));
        return;
    }
    m_pendingLogin.active = false;
    m_testingCredential = true;
    setBusy(true);
    setResultKeys(QString(), QString());
    m_backend->checkRegistryAuth(address, credential);
}

void RegistryAuthController::handleAuthCheckFinished(const QString &serverAddress,
                                                     DockerBackendInterface::AuthCheckResult result,
                                                     const QString &detail)
{
    const bool wasTesting = m_testingCredential;
    const bool wasLogin = m_pendingLogin.active;
    m_testingCredential = false;
    m_pendingLogin.active = false;
    setBusy(false);

    if (!wasTesting && !wasLogin) {
        return; // not a check we started (the backend may be reused elsewhere)
    }

    const QString key = keyForAuthResult(result);
    if (result != DockerBackendInterface::AuthCheckResult::Succeeded) {
        // check failed: save nothing; the password lived only inside this request and is discarded
        setResultKeys(QString(), key, detail);
        return;
    }

    if (wasTesting) {
        setResultKeys(QStringLiteral("testSucceeded"), QString());
        return;
    }

    QString errorKey;
    if (!m_store->store(m_pendingLogin.credential, &errorKey)) {
        // check passed but the write failed (wallet read-only or refused): report it, never fake success
        setResultKeys(QString(), errorKey.isEmpty() ? QStringLiteral("writeFailed") : errorKey, detail);
        return;
    }
    // the model and credentialsChanged() are driven by CredentialStore::changed() (see the
    // constructor), so do not emit here as well: a duplicate signal makes the notice bar blink twice
    QString cliError;
    if (!writeBackToCli(m_pendingLogin.credential, &cliError)) {
        // stored in the wallet, only the docker CLI sync failed — say so, or the user assumes the CLI works
        setResultKeys(QStringLiteral("loginSucceeded"), QStringLiteral("cliWriteFailed"), detail);
        setCliConfigPathForMessages();
        return;
    }
    setResultKeys(QStringLiteral("loginSucceeded"), QString(), serverAddress);
}

void RegistryAuthController::removeCredential(const QString &serverAddress)
{
    QString errorKey;
    if (!m_store->remove(serverAddress, &errorKey)) {
        setResultKeys(QString(), errorKey);
        return;
    }
    // remove the CLI config entry as well: deleting only ours leaves the CLI holding a dead credential
    QString cliError;
    if (!DockerCliAuthWriter::remove(DockerCliAuthWriter::defaultConfigPath(), serverAddress, &cliError)) {
        setResultKeys(QStringLiteral("removed"), QStringLiteral("cliWriteFailed"));
        setCliConfigPathForMessages();
        return;
    }
    setResultKeys(QStringLiteral("removed"), QString());
}

void RegistryAuthController::setCliConfigPathForMessages()
{
    // the notice must name the file that failed to sync, so put the path in detail (paths are not secret)
    m_lastErrorDetail = DockerCliAuthWriter::defaultConfigPath();
    Q_EMIT resultChanged();
}

void RegistryAuthController::scanCliConfig()
{
    m_scan = DockerCliAuthImporter::scan(DockerCliAuthImporter::defaultConfigPath());
    m_scanDone = true;
    Q_EMIT importScanChanged();
    Q_EMIT changed();
}

bool RegistryAuthController::writeBackToCli(const RegistryCredential &credential, QString *errorKey)
{
    /*
     * Sync the credential into the Docker CLI config file (users asked for silent maintenance; the
     * UI has no "sync" action any more).
     *
     * A failure does **not** change the KWallet result (that write succeeded), but it must be
     * reported — otherwise the user hits "why does this registry ask me to log in again?" in the
     * docker CLI.
     */
    const QString path = DockerCliAuthWriter::defaultConfigPath();
    if (DockerCliAuthWriter::upsert(path, credential.serverAddress, credential, errorKey)) {
        return true;
    }
    qCWarning(kontainerModel) << "could not write the credential back to the docker cli config:" << path
                              << (errorKey ? *errorKey : QString());
    return false;
}
} // namespace Kontainer
