/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/presentation.h"
#include "model/state_text.h"

#include <KLocalizedString>

#include <QSet>
#include <QtTest>

using namespace Kontainer;

/*!
 * 展示助手里"看起来像纯逻辑"的那部分（ARCH_V2 §12）。
 *
 * 目前只覆盖端口拓扑的连线取色下标：它必须是**纯函数**——同一个种子永远同一个下标，
 * 否则同一个容器的拓扑会在刷新后变色；同时分布要均匀，否则几十条连线会挤在一种颜色上。
 */
class PresentationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stateTextFollowsTheDomain();
    void colorIndexIsDeterministic();
    void colorIndexSpreadsAcrossThePalette();
    void colorIndexHandlesDegenerateInput();
};

void PresentationTest::colorIndexIsDeterministic()
{
    const Presentation presentation;
    for (const QString &seed : {QStringLiteral("cid-1|3000/tcp|0.0.0.0:20000"),
                                QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"),
                                QStringLiteral("容器-名字也可以是种子")}) {
        const int first = presentation.connectionColorIndex(seed, 6);
        QCOMPARE(presentation.connectionColorIndex(seed, 6), first);
        QVERIFY(first >= 0 && first < 6);
    }
}

void PresentationTest::colorIndexSpreadsAcrossThePalette()
{
    const Presentation presentation;
    // 200 个不同的种子应当把 6 个色位都用上（FNV-1a 取模的均匀性）
    QSet<int> used;
    for (int i = 0; i < 200; ++i) {
        used.insert(presentation.connectionColorIndex(QStringLiteral("container-%1|80/tcp|0.0.0.0:%2").arg(i).arg(20000 + i), 6));
    }
    QCOMPARE(used.size(), 6);

    // 端口号变一位也要换色：种子必须包含映射自身，而不是只有容器 id
    const int first = presentation.connectionColorIndex(QStringLiteral("cid-1|3000/tcp|0.0.0.0:20000"), 6);
    const int second = presentation.connectionColorIndex(QStringLiteral("cid-1|3001/tcp|0.0.0.0:20001"), 6);
    QVERIFY2(first != second, "different mappings must be able to take different slots");
}

void PresentationTest::colorIndexHandlesDegenerateInput()
{
    const Presentation presentation;
    // 空种子（例如还没拿到容器 id）与非法配色长度都不能崩、不能越界
    QCOMPARE(presentation.connectionColorIndex(QString(), 6), 0);
    QCOMPARE(presentation.connectionColorIndex(QStringLiteral("cid"), 0), 0);
    QCOMPARE(presentation.connectionColorIndex(QStringLiteral("cid"), -3), 0);
    QCOMPARE(presentation.connectionColorIndex(QString(), 0), 0);
}

/*!
 * 状态文案要跟着翻译域走（详情页的"关联容器/网络成员"列表用它）。
 */
void PresentationTest::stateTextFollowsTheDomain()
{
    /*
     * 语言必须在**第一次 i18n 调用之前**设好：ki18n 会缓存"域 + 语言"的查找结果，
     * 先取过英文再切语言不会重新翻译（这个坑在本用例里实测踩到过）。
     */
    // 与 KCM 的启动顺序一致：先设域再做任何翻译（少了这一步 i18n 会以"无域"查找）
    setupTranslationDomain();

    Presentation presentation;
    // 键就是 Docker 的状态字符串；未知键不能返回空串（界面会出现空白）
    QVERIFY(!presentation.stateText(QStringLiteral("running")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("paused")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("exited")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("no-such-state")).isEmpty());
    // 与容器列表用的是同一份文案（同一个 C++ 助手）；至于"是否翻成中文"——
    // 那是 tst_i18n_consistency 的事（只有它带着译文目录与 zh_CN 环境跑）
    QCOMPARE(presentation.stateText(QStringLiteral("running")), containerStateText(ContainerState::Running));

}


QTEST_GUILESS_MAIN(PresentationTest)

#include "tst_presentation.moc"
