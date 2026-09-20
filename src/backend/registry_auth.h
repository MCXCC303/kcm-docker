/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>

namespace Kontainer
{

/*!
 * One registry credential (ARCH_V5_V8 §2.6).
 *
 * Lives only in memory and KWallet: never written back to `~/.docker/config.json`, never logged,
 * never shown in error text (§40's redaction rules apply here too).
 */
struct RegistryCredential {
    /*! Normalized registry address (see `RegistryAuth::normalizeServerAddress()`), also the index key. */
    QString serverAddress;
    /*! Username + password (or the alternative identity token below). */
    QString username;
    QString password;
    /*! Token login (e.g. a CI-issued identity token): when set, username/password are not sent. */
    QString identityToken;

    /*!
     * Whether this is a **complete** credential.
     *
     * Username without password does not count: it cannot log in (the engine returns 401) and must
     * not reach the wallet, where the user would later see an entry that looks usable but is not.
     */
    bool isEmpty() const
    {
        if (serverAddress.isEmpty()) {
            return true;
        }
        if (!identityToken.isEmpty()) {
            return false;
        }
        return username.isEmpty() || password.isEmpty();
    }
    /*! Whether login uses an identity token. */
    bool usesIdentityToken() const
    {
        return !identityToken.isEmpty();
    }
};

/*!
 * `X-Registry-Auth` header codec (ARCH_V5_V8 §2.6).
 *
 * Docker Engine reads pull/build/login credentials from this header; the format is
 * `base64url(JSON)` with the fields `username` / `password` / `serveraddress`, or `identitytoken`.
 *
 * Why a dedicated class: three easy traps here — the URL-safe alphabet, padding, and Docker Hub's
 * legacy `serveraddress` spelling (`https://index.docker.io/v1/`). Keeping the rules in one tested
 * place beats re-assembling them at every call site.
 */
class RegistryAuth
{
public:
    /*! Normalized Docker Hub host name (credential index key). */
    static constexpr auto hubHost = "index.docker.io";
    /*! `serveraddress` value for Docker Hub in the header (legacy Docker convention, must not change). */
    static constexpr auto hubServerAddress = "https://index.docker.io/v1/";

    /*!
     * Normalize a registry address for use as the credential index key.
     *
     * Rules: trim, drop the scheme, drop path and trailing slashes, lowercase the host; every
     * Docker Hub spelling (`docker.io` / `index.docker.io` / `registry-1.docker.io`, with or
     * without scheme) collapses to `index.docker.io`.
     */
    static QString normalizeServerAddress(const QString &value);

    /*! Value for the header JSON's `serveraddress` (legacy form for Hub, else the normalized address). */
    static QString headerServerAddress(const QString &normalizedServerAddress);

    /*! Registry index key from an image reference (`alpine:3.19` -> `index.docker.io`). */
    static QString serverAddressForImage(const QString &imageReference);

    /*!
     * Encode into an `X-Registry-Auth` value: base64url(JSON), matching the docker CLI
     * (URL-safe alphabet + kept `=`; Docker accepts both, but we stay identical to the official client).
     */
    static QByteArray encode(const RegistryCredential &credential);

    /*!
     * Decode a header value: accepts URL-safe and standard base64, with or without padding
     * (the `auths` of `~/.docker/config.json` uses standard base64).
     *
     * On failure returns an empty credential and sets errorKey: `empty` / `invalidBase64` /
     * `invalidJson` / `noCredentials`.
     */
    static RegistryCredential decode(const QByteArray &headerValue, QString *errorKey = nullptr);

    /*!
     * Decode the `auth` field of `auths` in `~/.docker/config.json` (standard base64 `user:password`).
     *
     * Separate from `decode()`: one is a Docker API header, the other a CLI config field, and their
     * formats differ (the latter has no JSON, only `user:password`).
     */
    static RegistryCredential decodeConfigAuth(const QString &serverAddress, const QByteArray &base64UserPassword, QString *errorKey = nullptr);

    /*!
     * `user:password` -> the `auth` value in `config.json` (standard base64, matching the CLI).
     *
     * Symmetric to `decodeConfigAuth()`: used when writing the CLI config, so after `docker login`
     * the CLI reads back exactly the credential we wrote.
     */
    static QString encodeConfigAuth(const QString &username, const QString &password);
};

} // namespace Kontainer
