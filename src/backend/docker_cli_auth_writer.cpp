/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_cli_auth_writer.h"

#include "backend/docker_cli_auth_importer.h"
#include "backend/registry_auth.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Kontainer
{

namespace
{
constexpr auto kAuths = "auths";
constexpr auto kAuth = "auth";

/*! 读现有配置；`ok` 为假表示"文件存在但读不动/解析不了"，调用方必须放弃写入。 */
QJsonObject readExisting(const QString &path, bool *ok, QString *errorKey)
{
    *ok = true;
    QFile file(path);
    if (!file.exists()) {
        return {}; // 还没有这个文件：从空配置开始
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot read docker cli config:" << path << file.errorString();
        return {};
    }
    const QByteArray payload = file.readAll();
    if (payload.trimmed().isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 关键安全点：用户的配置文件我们不理解时**一个字都不写**，否则会把他的登录状态弄丢
        *ok = false;
        if (errorKey) {
            *errorKey = QStringLiteral("invalidJson");
        }
        qCWarning(kontainerModel) << "docker cli config is not valid json; refusing to touch it:" << path;
        return {};
    }
    return document.object();
}

/*! 原子写入（QSaveFile）+ 0600：文件里等价于明文凭据，权限必须收紧。 */
bool writeConfig(const QString &path, const QJsonObject &root, QString *errorKey)
{
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        return false;
    }
    // 目录 0700（已存在时不动它，避免改掉用户自己的权限设置）
    QFile::setPermissions(dir.absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot write docker cli config:" << path << file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot commit docker cli config:" << path << file.errorString();
        return false;
    }
    return true;
}

/*!
 * 两个地址是否指向同一个仓库。
 *
 * 必须用 `normalizeServerAddress()`（它处理的是**仓库地址**）；
 * `serverAddressForImage()` 是给"镜像引用"用的，它会把不认识的东西都归到 Docker Hub——
 * 拿它比较会把新仓库的凭据写进 Hub 那一条里（实测踩到过）。
 */
bool sameRegistry(const QString &lhs, const QString &rhs)
{
    const QString left = RegistryAuth::normalizeServerAddress(lhs);
    const QString right = RegistryAuth::normalizeServerAddress(rhs);
    return !left.isEmpty() && left == right;
}
} // namespace

QString DockerCliAuthWriter::defaultConfigPath()
{
    return DockerCliAuthImporter::defaultConfigPath();
}

QString DockerCliAuthWriter::configKeyFor(const QString &path, const QString &serverAddress)
{
    bool ok = false;
    const QJsonObject existing = readExisting(path.isEmpty() ? defaultConfigPath() : path, &ok, nullptr);
    if (ok) {
        const QJsonObject auths = existing.value(QString::fromLatin1(kAuths)).toObject();
        for (auto it = auths.constBegin(); it != auths.constEnd(); ++it) {
            // 沿用文件里已有的写法，避免同一个仓库出现两个键
            if (sameRegistry(it.key(), serverAddress)) {
                return it.key();
            }
        }
    }
    return serverAddress;
}

bool DockerCliAuthWriter::upsert(const QString &path,
                                 const QString &serverAddress,
                                 const RegistryCredential &credential,
                                 QString *errorKey)
{
    if (serverAddress.isEmpty() || credential.username.isEmpty()) {
        if (errorKey) {
            *errorKey = QStringLiteral("noCredentials");
        }
        return false;
    }
    const QString configPath = path.isEmpty() ? defaultConfigPath() : path;

    bool ok = false;
    QJsonObject root = readExisting(configPath, &ok, errorKey);
    if (!ok) {
        return false;
    }

    QJsonObject auths = root.value(QString::fromLatin1(kAuths)).toObject();
    const QString key = configKeyFor(configPath, serverAddress);
    QJsonObject entry = auths.value(key).toObject(); // 保留 identitytoken 等已有字段
    /*
     * 秘密取 password；令牌登录时取 identityToken（CLI 的 `docker login` 也是把令牌
     * 当作密码写进 `auth`）。两者都空说明凭据不完整，前面已经拦掉了。
     */
    const QString secret = !credential.password.isEmpty() ? credential.password : credential.identityToken;
    entry.insert(QString::fromLatin1(kAuth), RegistryAuth::encodeConfigAuth(credential.username, secret));
    auths.insert(key, entry);
    root.insert(QString::fromLatin1(kAuths), auths);

    return writeConfig(configPath, root, errorKey);
}

bool DockerCliAuthWriter::remove(const QString &path, const QString &serverAddress, QString *errorKey)
{
    const QString configPath = path.isEmpty() ? defaultConfigPath() : path;

    bool ok = false;
    QJsonObject root = readExisting(configPath, &ok, errorKey);
    if (!ok) {
        return false;
    }

    QJsonObject auths = root.value(QString::fromLatin1(kAuths)).toObject();
    QStringList toRemove;
    for (auto it = auths.constBegin(); it != auths.constEnd(); ++it) {
        if (sameRegistry(it.key(), serverAddress)) {
            toRemove.append(it.key());
        }
    }
    if (toRemove.isEmpty()) {
        return true; // 本来就没有：不算失败
    }
    for (const QString &key : toRemove) {
        auths.remove(key);
    }
    root.insert(QString::fromLatin1(kAuths), auths);
    return writeConfig(configPath, root, errorKey);
}

} // namespace Kontainer
