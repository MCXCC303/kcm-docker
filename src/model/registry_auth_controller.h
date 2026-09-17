/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"
#include "backend/docker_backend_interface.h"
#include "backend/docker_cli_auth_importer.h"

#include <QObject>
#include <QString>
#include <QStringList>

// moc 需要完整类型才能为指针属性生成元类型代码；这里只给 moc 看，
// 使用方（.cpp）自己 include，避免头文件把模型实现拖进来
Q_MOC_INCLUDE("model/registry_credential_model.h")

namespace Kontainer
{

class RegistryCredentialModel;

/*!
 * 仓库认证的界面控制器（ARCH_V5_V8 §2.6/§2.7）。
 *
 * 规则集中在这里，QML 只负责显示与收集输入：
 *
 *  - **先校验后保存**：登录必须经过 `POST /auth` 成功才写入钱包（写进去的都是"验过的"）；
 *    校验失败不落盘，也不会把密码留在任何地方；
 *  - **钱包不可用只降级、不回退**：不写明文、不假装成功，界面据 `walletUnavailableReason` 说明原因；
 *  - **CLI 导入只读且不覆盖**：扫描与导入都交给 `DockerCliAuthImporter`，这里只做状态与汇报；
 *  - **密码/令牌不出控制器**：`login()` 收进去、`POST /auth` 用完就丢，模型与信号里都没有它。
 *
 * 用户可见文案不在 C++ 里：这里只给稳定的 key（`invalidCredentials` / `registryUnreachable`…），
 * 由 QML 侧映射（ARCH_V3 §2.6）。
 */
class RegistryAuthController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::RegistryCredentialModel *credentials READ credentials CONSTANT)
    /*! 钱包状态：`closed` / `opening` / `ready` / `unavailable`。 */
    Q_PROPERTY(QString walletStateKey READ walletStateKey NOTIFY changed)
    /*! 不可用原因 key：`walletDisabled` / `walletOpenFailed` / `walletFolderFailed`。 */
    Q_PROPERTY(QString walletUnavailableReason READ walletUnavailableReason NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /*! 最近一次动作的结果 key（成功为空则看 lastErrorKey）。 */
    Q_PROPERTY(QString lastResultKey READ lastResultKey NOTIFY resultChanged)
    Q_PROPERTY(QString lastErrorKey READ lastErrorKey NOTIFY resultChanged)
    /*! 引擎原文，仅用于"技术细节"（可能含仓库返回的文本），不做用户文案。 */
    Q_PROPERTY(QString lastErrorDetail READ lastErrorDetail NOTIFY resultChanged)

    /* ---------------- docker CLI 导入（只读扫描 + 逐条导入） ---------------- */
    Q_PROPERTY(bool cliConfigPresent READ cliConfigPresent NOTIFY importScanChanged)
    Q_PROPERTY(QString cliConfigPath READ cliConfigPath NOTIFY importScanChanged)
    /*! CLI 里存在、钱包里还没有的仓库（可勾选导入）。 */
    Q_PROPERTY(QStringList importableAddresses READ importableAddresses NOTIFY importScanChanged)
    /*! 由 credsStore/credHelpers 管理的条目：我们**不执行**外部凭据程序，只如实说明。 */
    Q_PROPERTY(QStringList helperManagedKeys READ helperManagedKeys NOTIFY importScanChanged)
    /*! 读到但用不上的条目（缺 auth、内容坏掉、与别的条目指向同一仓库）。 */
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

    /*! 打开钱包、刷新列表、只读扫描 CLI 配置（页面进入时调用，幂等）。 */
    Q_INVOKABLE void refresh();
    /*!
     * 登录：先 `POST /auth` 校验，成功才写入钱包。
     *
     * `token` 非空时按令牌登录（不再发送用户名密码）。
     */
    Q_INVOKABLE void login(const QString &serverAddress, const QString &username, const QString &password, const QString &token = {});
    /*! 用已保存的凭据测试连接（不修改任何东西）。 */
    Q_INVOKABLE void testCredential(const QString &serverAddress);
    /*! 移除凭据（界面负责二次确认）。 */
    Q_INVOKABLE void removeCredential(const QString &serverAddress);
    /*! 重新只读扫描 CLI 配置。 */
    Q_INVOKABLE void scanCliConfig();
    /*! 导入指定仓库（空列表 = 导入全部可导入项）；已有条目一律不覆盖。 */
    Q_INVOKABLE void importFromCli(const QStringList &serverAddresses = {});
    /*! 清掉最近一次结果（关闭提示条时用）。 */
    Q_INVOKABLE void clearResult();

Q_SIGNALS:
    void changed();
    void resultChanged();
    void importScanChanged();
    /*! 凭据集合变化（含导入完成），界面据此提示。 */
    void credentialsChanged();

private:
    void setResultKeys(const QString &resultKey, const QString &errorKey, const QString &detail = {});
    void setBusy(bool busy);
    /*! 把 `DockerBackendInterface::AuthCheckResult` 翻成界面 key。 */
    static QString keyForAuthResult(DockerBackendInterface::AuthCheckResult result);
    void handleAuthCheckFinished(const QString &serverAddress, DockerBackendInterface::AuthCheckResult result, const QString &detail);

    DockerBackendInterface *m_backend = nullptr;
    CredentialStore *m_store = nullptr;
    RegistryCredentialModel *m_credentials = nullptr;

    /*! 待校验的登录请求（校验通过后才写入钱包）。 */
    struct PendingLogin {
        RegistryCredential credential;
        bool active = false;
    };
    PendingLogin m_pendingLogin;
    /*! 正在进行的是"测试连接"而不是"登录"。 */
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
