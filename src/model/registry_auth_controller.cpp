/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/registry_auth_controller.h"

#include "logging.h"
#include "model/registry_credential_model.h"

#include <algorithm>

namespace Kontainer
{

namespace
{
/*! 钱包状态 → 界面 key。 */
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
        // 钱包里已经有这个仓库的凭据就不必再导入（导入也不会覆盖它）
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
    Q_EMIT changed();
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
        // 钱包不可用时不验证、不保存：验证通过了也存不下，说了反而误导
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
        return; // 不是我们发起的校验（例如别处复用后端）
    }

    const QString key = keyForAuthResult(result);
    if (result != DockerBackendInterface::AuthCheckResult::Succeeded) {
        // 校验失败：什么都不保存。密码只留在这次请求里，用完即弃
        setResultKeys(QString(), key, detail);
        return;
    }

    if (wasTesting) {
        setResultKeys(QStringLiteral("testSucceeded"), QString());
        return;
    }

    QString errorKey;
    if (!m_store->store(m_pendingLogin.credential, &errorKey)) {
        // 校验成功但写不进去（钱包只读/被拒）：如实报告，不能假装登录成功
        setResultKeys(QString(), errorKey.isEmpty() ? QStringLiteral("writeFailed") : errorKey, detail);
        return;
    }
    // 模型与 credentialsChanged() 由 CredentialStore::changed() 驱动（见构造函数），
    // 这里不再重复发一次：重复的信号会让界面提示条闪两下
    setResultKeys(QStringLiteral("loginSucceeded"), QString(), serverAddress);
}

void RegistryAuthController::removeCredential(const QString &serverAddress)
{
    QString errorKey;
    if (!m_store->remove(serverAddress, &errorKey)) {
        setResultKeys(QString(), errorKey);
        return;
    }
    setResultKeys(QStringLiteral("removed"), QString());
}

void RegistryAuthController::scanCliConfig()
{
    m_scan = DockerCliAuthImporter::scan(DockerCliAuthImporter::defaultConfigPath());
    m_scanDone = true;
    Q_EMIT importScanChanged();
    Q_EMIT changed();
}

void RegistryAuthController::importFromCli(const QStringList &serverAddresses)
{
    if (m_store->state() != CredentialStore::State::Ready) {
        setResultKeys(QString(), QStringLiteral("unavailable"));
        return;
    }

    // 只导入用户选中的项（空列表 = 全部可导入项）
    QStringList wanted;
    for (const QString &address : serverAddresses) {
        const QString normalized = RegistryAuth::normalizeServerAddress(address);
        if (!normalized.isEmpty()) {
            wanted.append(normalized);
        }
    }

    DockerCliAuthScan selection = m_scan;
    if (!wanted.isEmpty()) {
        selection.credentials.erase(std::remove_if(selection.credentials.begin(),
                                                  selection.credentials.end(),
                                                  [&wanted](const ImportableCredential &importable) {
                                                      return !wanted.contains(importable.credential.serverAddress);
                                                  }),
                                   selection.credentials.end());
    }

    const DockerCliAuthImporter::ImportOutcome outcome = DockerCliAuthImporter::importInto(*m_store, selection);
    m_importedCount = outcome.imported;
    m_alreadyPresentCount = outcome.alreadyPresent;
    m_importFailedCount = outcome.failed;

    if (outcome.failed > 0) {
        setResultKeys(QString(), QStringLiteral("importFailed"));
    } else if (outcome.imported == 0) {
        setResultKeys(QStringLiteral("importNothingToDo"), QString());
    } else {
        setResultKeys(QStringLiteral("importSucceeded"), QString());
    }
    Q_EMIT importScanChanged();
}

} // namespace Kontainer
