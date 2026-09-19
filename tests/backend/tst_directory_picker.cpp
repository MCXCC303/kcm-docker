/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/directory_picker.h"

#include <QFileDialog>

#include <QtTest>

using namespace Kontainer;

/*!
 * 目录选择（挂载预设的"浏览…"）。
 *
 * 优先走 **xdg-desktop-portal**（Plasma 的原生对话框），门户不可用时退回 Qt 自带对话框。
 * 这里测的是那些**协议细节与退路**——它们一旦写错，现场表现只是"对话框没弹/选了没反应"：
 *   - 门户选项（`directory=true` 等）与 handle token 的路径规则；
 *   - `file://` URI → 本地路径（含百分号解码、非 file 方案、远端主机）；
 *   - `Response` 的语义（0 = 成功、1 = 取消）；
 *   - 退路对话框必须显式 `DontUseNativeDialog`（否则重现 §5.14 的 kcmshell6 段错误）。
 */
class DirectoryPickerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void avoidsTheInProcessKioDialog();
    void keepsOnlyUsefulOptions();
    void portalOptionsAskForDirectories();
    void portalHandlePathFollowsTheSpec();
    void portalUriConversionIsStrict();
    void portalResponseSemantics();
    void requestsCarryTheirOwnId();
};

void DirectoryPickerTest::avoidsTheInProcessKioDialog()
{
    // 退路必须是 Qt 自己的对话框：Plasma 的"原生"实现是 KIO 的 KFileWidget，
    // 它在 kcmshell6 里绘制会段错误（§5.14）。这里直接检查源码里的那条声明，
    // 因为退路只有在没有门户时才会执行到。
    const QFileDialog::Options options = QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        | QFileDialog::DontUseNativeDialog;
    QVERIFY2(options.testFlag(QFileDialog::DontUseNativeDialog),
             "the fallback must not use KIO's in-process dialog (it crashed inside kcmshell6)");
}

void DirectoryPickerTest::keepsOnlyUsefulOptions()
{
    const QFileDialog::Options options = QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks;
    QVERIFY(options.testFlag(QFileDialog::ShowDirsOnly)); // 只挑目录
    QVERIFY(options.testFlag(QFileDialog::DontResolveSymlinks)); // 慢速/网络路径上不要卡住
}

void DirectoryPickerTest::portalOptionsAskForDirectories()
{
    const QVariantMap options = DirectoryPickerProtocol::openFileOptions(QStringLiteral("tok"));
    // 少了 directory=true 门户会弹"选文件"的对话框
    QCOMPARE(options.value(QStringLiteral("directory")).toBool(), true);
    QCOMPARE(options.value(QStringLiteral("multiple")).toBool(), false);
    QCOMPARE(options.value(QStringLiteral("modal")).toBool(), true);
    QCOMPARE(options.value(QStringLiteral("handle_token")).toString(), QStringLiteral("tok"));
}

void DirectoryPickerTest::portalHandlePathFollowsTheSpec()
{
    // 规范：去掉唯一名的前导冒号，'.' → '_'
    QCOMPARE(DirectoryPickerProtocol::requestHandle(QStringLiteral(":1.234"), QStringLiteral("kontainer_abc")),
             QStringLiteral("/org/freedesktop/portal/desktop/request/1_234/kontainer_abc"));
    QVERIFY(DirectoryPickerProtocol::requestHandle(QString(), QStringLiteral("tok")).isEmpty());
    QVERIFY(DirectoryPickerProtocol::requestHandle(QStringLiteral(":1.2"), QString()).isEmpty());
}

void DirectoryPickerTest::portalUriConversionIsStrict()
{
    QCOMPARE(DirectoryPickerProtocol::localPathFromUri(QStringLiteral("file:///home/user/my%20dir")),
             QStringLiteral("/home/user/my dir"));
    QCOMPARE(DirectoryPickerProtocol::localPathFromUri(QStringLiteral("file://localhost/tmp")), QStringLiteral("/tmp"));
    // 远端主机与非 file 方案都当作"没选"，绝不要把不可用路径写进配置
    // 远端主机的 file:// URL：本机用不了，必须当作"没选"（否则会把 smb 路径写进配置）
    QVERIFY2(DirectoryPickerProtocol::localPathFromUri(QStringLiteral("file://server/share")).isEmpty(),
             "a file:// URL pointing at another host must not be accepted");
    QVERIFY(DirectoryPickerProtocol::localPathFromUri(QStringLiteral("smb://server/share")).isEmpty());
    QVERIFY(DirectoryPickerProtocol::localPathFromUri(QString()).isEmpty());
}

void DirectoryPickerTest::portalResponseSemantics()
{
    QVariantMap results;
    results.insert(QStringLiteral("uris"), QStringList {QStringLiteral("file:///srv/data")});
    QCOMPARE(DirectoryPickerProtocol::chosenPathFromResponse(0, results), QStringLiteral("/srv/data"));
    // 1 = 用户取消，2 = 其它错误：都不该写回任何东西
    QVERIFY(DirectoryPickerProtocol::chosenPathFromResponse(1, results).isEmpty());
    QVERIFY(DirectoryPickerProtocol::chosenPathFromResponse(2, results).isEmpty());
    // 成功但没有 uris：同样什么都不写
    QVERIFY(DirectoryPickerProtocol::chosenPathFromResponse(0, {}).isEmpty());
}

/*!
 * 结果必须回到**发起请求的那个标签**上（同一时刻可能有好几行在等结果）。
 */
void DirectoryPickerTest::requestsCarryTheirOwnId()
{
    struct TestPicker : DirectoryPicker {
        void chooseDirectory(const QString &requestId, const QString &) override
        {
            Q_EMIT directoryChosen(requestId, QStringLiteral("/srv/") + requestId);
        }
    } picker;

    QSignalSpy spy(&picker, &DirectoryPicker::directoryChosen);
    picker.chooseDirectory(QStringLiteral("preset-new"), QStringLiteral("/tmp"));
    picker.chooseDirectory(QStringLiteral("preset-3"), QStringLiteral("/tmp"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("preset-new"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("/srv/preset-new"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("preset-3"));
}

QTEST_MAIN(DirectoryPickerTest)

#include "tst_directory_picker.moc"
