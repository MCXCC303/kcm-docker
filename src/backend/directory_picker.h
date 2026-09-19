/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kontainer
{

/*!
 * 让用户挑一个目录（挂载预设的宿主路径、构建上下文目录都用它）。
 *
 * 做成接口而不是直接调 `QFileDialog`：
 * - 测试与离屏渲染不能弹出真实对话框（会阻塞），需要注入替身；
 * - 挑目录的"原生"做法（见下）是**异步**的，界面必须能等结果回来再写回对应的输入框。
 *
 * ## 为什么是异步的
 *
 * Plasma 6 的原生文件对话框由 **xdg-desktop-portal** 提供
 * （`org.freedesktop.portal.FileChooser`）。它是 D-Bus 调用 + `Response` 信号，
 * 天然异步。之前的同步实现走 `QFileDialog`，在 kcmshell6 里选目录会段错误——
 * 栈落在 KIO 的 `KFileWidget` 绘制上（`libKF6KIOWidgets` SEGV，见 ARCH §5.14）。
 * 因此接口改成"请求 + 信号"，实现优先走门户，门户不可用时退回 Qt 自带对话框
 * （那两个都**不经过** KIO 的进程内控件）。
 *
 * ## 用法（QML）
 *
 * ```qml
 * onClicked: directoryPicker.chooseDirectory("preset-new", newSource.text)
 * Connections {
 *     target: directoryPicker
 *     function onDirectoryChosen(requestId, path) {
 *         if (path.length > 0 && requestId === "preset-new") newSource.text = path;
 *     }
 * }
 * ```
 *
 * `requestId` 是调用方自己给的标签：同一时刻可能有好几行都在等结果
 * （每个预设行都有自己的「浏览…」），靠它把结果写回**发起请求的那一行**。
 */
class DirectoryPicker : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryPicker(QObject *parent = nullptr);
    ~DirectoryPicker() override;

    /*!
     * 请求挑一个目录。
     *
     * @param requestId 调用方标签，原样回传（用来把结果写回发起请求的位置）
     * @param startPath 建议的起始目录（可以是空串）
     */
    Q_INVOKABLE virtual void chooseDirectory(const QString &requestId, const QString &startPath) = 0;

Q_SIGNALS:
    /*! 结果（`path` 为空表示用户取消或失败）。 */
    void directoryChosen(const QString &requestId, const QString &path);
};

/*!
 * 门户 / Qt 对话框的纯辅助函数（与 D-Bus 无关，因此可以单独测）。
 *
 * 单独拎出来是因为它们承载了**协议细节**：门户的选项名、handle token 的规则、
 * `file://` URI 到本地路径的转换——这些一旦写错，表现是"对话框不弹/选了没反应"，
 * 很难从现场看出来。
 */
namespace DirectoryPickerProtocol
{
/*! `OpenFile` 的选项表：只挑目录、单选、模态，并带上 handle token。 */
QVariantMap openFileOptions(const QString &handleToken);
/*!
 * 门户返回的 handle：`/org/freedesktop/portal/desktop/request/<sender>/<token>`。
 *
 * `sender` 是**本连接的唯一名去掉前导冒号、再把点换成下划线**（门户规范要求）。
 */
QString requestHandle(const QString &uniqueName, const QString &handleToken);
/*!
 * `file://` URI → 本地路径（百分号解码）。
 *
 * 非 `file:` 方案、或指向别的主机（`file://host/...`）时返回空串——那样的情况下
 * 我们宁可"什么都没选"，也不要把一个不可用的路径写进配置。
 */
QString localPathFromUri(const QString &uri);
/*! 门户 `Response` 的 `results` → 选中的路径（`response != 0`、缺字段、转换失败都是空串）。 */
QString chosenPathFromResponse(uint response, const QVariantMap &results);
} // namespace DirectoryPickerProtocol

/*!
 * 真实实现：优先 xdg-desktop-portal（Plasma 的原生对话框），不可用时退回 Qt 自带对话框。
 *
 * 门户不可用（没有会话总线、没装 portal、服务没注册）时**不报错**，
 * 直接走 `QFileDialog`（`DontUseNativeDialog`，避开 KIO 的进程内实现）。
 */
class PortalDirectoryPicker : public DirectoryPicker
{
    Q_OBJECT

public:
    explicit PortalDirectoryPicker(QObject *parent = nullptr);

    void chooseDirectory(const QString &requestId, const QString &startPath) override;

    /*! 门户是否可用（只读判断，真正的结论来自实际调用）。 */
    static bool portalAvailable();

private Q_SLOTS:
    /*! 门户的 `Response` 信号（`org.freedesktop.portal.Request`）。 */
    void handlePortalResponse(uint response, const QVariantMap &results);

private:
    void fallBackToQtDialog(const QString &requestId, const QString &startPath);

    /*! 等待结果的门户请求：requestId → 门户 handle。 */
    QHash<QString, QString> m_pending;
    /*! 已订阅的 Response 路径 → requestId（一个连接名对应一行）。 */
    QHash<QString, QString> m_subscriptions;
};

} // namespace Kontainer
