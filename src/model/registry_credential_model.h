/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QAbstractListModel>

namespace Kontainer
{

/*!
 * 已保存的仓库凭据列表（ARCH_V5_V8 §2.7）。
 *
 * **只暴露仓库地址与用户名**：密码/令牌永远不进模型——它们会顺着 model role 流到
 * QML、日志、报表里。界面要显示"有没有凭据""用哪种方式登录"，这些字段就够了。
 */
class RegistryCredentialModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        /*! 规范化后的仓库地址（索引键，也是界面上的主标识）。 */
        ServerAddressRole = Qt::UserRole + 1,
        /*! 登录方式：`password` / `token`。 */
        AuthKindRole,
        /*! 用户名；令牌登录时为空。 */
        UsernameRole,
    };
    Q_ENUM(Roles)

    /*!
     * `store` 为凭据来源（可为空 = 空列表）。
     *
     * 模型只从存储读，不写：写入一律走控制器，这样"校验成功后才保存"的规则只有一处。
     */
    explicit RegistryCredentialModel(CredentialStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    /*! 是否已经存有该仓库的凭据（界面据此决定按钮可用性）。 */
    Q_INVOKABLE bool contains(const QString &serverAddress) const;
    /*! 行号；没有返回 -1。 */
    Q_INVOKABLE int rowForAddress(const QString &serverAddress) const;

    /*! 从存储重新加载（凭据只读，模型只做展示）。 */
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
