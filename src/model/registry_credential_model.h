/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QAbstractListModel>

namespace Kontainer
{

/*!
 * List of saved registry credentials (ARCH_V5_V8 §2.7).
 *
 * **Exposes the registry address and user name only**: passwords/tokens never enter the model,
 * because model roles flow into QML, logs and reports. Those fields suffice for the UI to show
 * whether a credential exists and which login method it uses.
 */
class RegistryCredentialModel : public QAbstractListModel
{
    Q_OBJECT

    /*! Row count / empty: the UI derives its empty state from them (QML cannot see plain C++ methods). */
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        /*! Normalized registry address (the lookup key and the main UI identifier). */
        ServerAddressRole = Qt::UserRole + 1,
        /*! Login method: `password` / `token`. */
        AuthKindRole,
        /*! User name; empty for token logins. */
        UsernameRole,
    };
    Q_ENUM(Roles)

    /*!
     * `store` is the credential source (may be null = empty list).
     *
     * The model only reads from the store: every write goes through the controller, so "save only
     * after a successful check" lives in one place.
     */
    explicit RegistryCredentialModel(CredentialStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    /*! Whether a credential exists for the registry (the UI enables its buttons accordingly). */
    Q_INVOKABLE bool contains(const QString &serverAddress) const;
    /*! Row for the address, -1 if absent. */
    Q_INVOKABLE int rowForAddress(const QString &serverAddress) const;

    /*! Reload from the store (credentials are read-only; the model only presents them). */
    void reload();

Q_SIGNALS:
    void countChanged();

private:
    struct Entry {
        QString serverAddress;
        QString authKind;
        QString username;
    };

    CredentialStore *m_store = nullptr;
    QList<Entry> m_entries;
};

} // namespace Kontainer
