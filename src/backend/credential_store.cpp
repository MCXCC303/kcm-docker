/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/credential_store.h"

#include "logging.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace Kontainer
{

namespace
{
constexpr auto kUsername = "username";
constexpr auto kPassword = "password";
constexpr auto kIdentityToken = "identitytoken";
} // namespace

CredentialStore::CredentialStore(CredentialBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
}

CredentialStore::~CredentialStore() = default;

void CredentialStore::open()
{
    if (!m_backend || m_state == State::Ready || m_state == State::Opening) {
        return;
    }
    if (m_backend->isAvailable()) {
        setState(State::Ready);
        return;
    }

    setState(State::Opening);
    // The backend may call back synchronously (in-memory) or asynchronously (KWallet waits for
    // unlock); assume nothing here and trust only the callback.
    m_backend->open([this](bool success) {
        if (success) {
            setState(State::Ready);
            return;
        }
        m_unavailableReason = m_backend->unavailableReason().isEmpty() ? QStringLiteral("walletOpenFailed")
                                                                       : m_backend->unavailableReason();
        setState(State::Unavailable);
    });
}

CredentialStore::State CredentialStore::state() const
{
    return m_state;
}

QString CredentialStore::unavailableReason() const
{
    return m_unavailableReason;
}

void CredentialStore::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
    if (state == State::Ready) {
        Q_EMIT changed();
    }
}

QStringList CredentialStore::serverAddresses() const
{
    if (!m_backend || m_state != State::Ready) {
        return {};
    }
    // Keys were normalized on write, but normalize and dedup again here: wallet entries may come
    // from older versions or manual edits and cannot be assumed clean
    QStringList addresses;
    const QStringList keys = m_backend->keys();
    for (const QString &key : keys) {
        const QString normalized = RegistryAuth::normalizeServerAddress(key);
        if (!normalized.isEmpty() && !addresses.contains(normalized)) {
            addresses.append(normalized);
        }
    }
    addresses.sort();
    return addresses;
}

bool CredentialStore::hasCredential(const QString &serverAddress) const
{
    const QString normalized = RegistryAuth::normalizeServerAddress(serverAddress);
    if (normalized.isEmpty() || !m_backend || m_state != State::Ready) {
        return false;
    }
    return !m_backend->read(normalized).isEmpty();
}

RegistryCredential CredentialStore::credential(const QString &serverAddress) const
{
    const QString normalized = RegistryAuth::normalizeServerAddress(serverAddress);
    if (normalized.isEmpty() || !m_backend || m_state != State::Ready) {
        return {};
    }
    const QByteArray value = m_backend->read(normalized);
    if (value.isEmpty()) {
        return {};
    }
    QString errorKey;
    const RegistryCredential decoded = decodeEntry(normalized, value, &errorKey);
    if (decoded.isEmpty()) {
        // Log the key and reason only, never the content (§40)
        qCWarning(kontainerModel) << "credential entry is unusable:" << normalized << errorKey;
    }
    return decoded;
}

RegistryCredential CredentialStore::credentialForImage(const QString &imageReference) const
{
    const QString address = RegistryAuth::serverAddressForImage(imageReference);
    if (address.isEmpty()) {
        return {};
    }
    return credential(address);
}

bool CredentialStore::store(const RegistryCredential &credential, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return false;
    };

    const QString normalized = RegistryAuth::normalizeServerAddress(credential.serverAddress);
    if (normalized.isEmpty()) {
        return fail("invalidServerAddress");
    }
    if (credential.isEmpty()) {
        return fail("noCredentials");
    }
    if (!m_backend || m_state != State::Ready) {
        return fail("unavailable");
    }
    if (!m_backend->write(normalized, encodeEntry(credential))) {
        return fail("writeFailed");
    }
    if (errorKey) {
        errorKey->clear();
    }
    Q_EMIT changed();
    return true;
}

bool CredentialStore::remove(const QString &serverAddress, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return false;
    };

    const QString normalized = RegistryAuth::normalizeServerAddress(serverAddress);
    if (normalized.isEmpty()) {
        return fail("invalidServerAddress");
    }
    if (!m_backend || m_state != State::Ready) {
        return fail("unavailable");
    }
    const bool existed = !m_backend->read(normalized).isEmpty();
    if (!m_backend->remove(normalized)) {
        return fail("removeFailed");
    }
    if (errorKey) {
        errorKey->clear();
    }
    if (existed) {
        Q_EMIT changed();
    }
    return true;
}

QByteArray CredentialStore::encodeEntry(const RegistryCredential &credential)
{
    QJsonObject object;
    if (credential.usesIdentityToken()) {
        object.insert(QLatin1String(kIdentityToken), credential.identityToken);
    } else {
        object.insert(QLatin1String(kUsername), credential.username);
        object.insert(QLatin1String(kPassword), credential.password);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

RegistryCredential CredentialStore::decodeEntry(const QString &serverAddress, const QByteArray &value, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return RegistryCredential();
    };

    if (value.isEmpty()) {
        return fail("empty");
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail("invalidJson");
    }

    const QJsonObject object = document.object();
    RegistryCredential credential;
    credential.serverAddress = RegistryAuth::normalizeServerAddress(serverAddress);
    credential.username = object.value(QLatin1String(kUsername)).toString();
    credential.password = object.value(QLatin1String(kPassword)).toString();
    credential.identityToken = object.value(QLatin1String(kIdentityToken)).toString();
    if (credential.isEmpty()) {
        return fail("noCredentials");
    }
    if (errorKey) {
        errorKey->clear();
    }
    return credential;
}

} // namespace Kontainer
