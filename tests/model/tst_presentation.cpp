/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * The "seems pure logic" part of the presentation helper (ARCH_V2 §12).
 *
 * Only the port-topology line colour index: it must be a **pure function** (one seed, one index,
 * else a container's topology recolours on refresh) and spread evenly across the palette.
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
    // 200 distinct seeds must reach all 6 slots (FNV-1a modulo uniformity)
    QSet<int> used;
    for (int i = 0; i < 200; ++i) {
        used.insert(presentation.connectionColorIndex(QStringLiteral("container-%1|80/tcp|0.0.0.0:%2").arg(i).arg(20000 + i), 6));
    }
    QCOMPARE(used.size(), 6);

    // A different port must recolour: the seed includes the mapping itself, not only the container id
    const int first = presentation.connectionColorIndex(QStringLiteral("cid-1|3000/tcp|0.0.0.0:20000"), 6);
    const int second = presentation.connectionColorIndex(QStringLiteral("cid-1|3001/tcp|0.0.0.0:20001"), 6);
    QVERIFY2(first != second, "different mappings must be able to take different slots");
}

void PresentationTest::colorIndexHandlesDegenerateInput()
{
    const Presentation presentation;
    // Empty seed (no container id yet) or a bad palette size must neither crash nor index out of range
    QCOMPARE(presentation.connectionColorIndex(QString(), 6), 0);
    QCOMPARE(presentation.connectionColorIndex(QStringLiteral("cid"), 0), 0);
    QCOMPARE(presentation.connectionColorIndex(QStringLiteral("cid"), -3), 0);
    QCOMPARE(presentation.connectionColorIndex(QString(), 0), 0);
}

/*!
 * State texts must follow the translation domain (detail page's linked-container/member lists).
 */
void PresentationTest::stateTextFollowsTheDomain()
{
    /*
     * The language must be set **before the first i18n call**: ki18n caches lookups per domain+language,
     * an English lookup followed by a language switch is not retranslated (hit while writing this test).
     */
    // Same order as the KCM startup: set the domain first, then translate (else i18n looks up no domain)
    setupTranslationDomain();

    Presentation presentation;
    // The key is Docker's state string; an unknown key must not return an empty string (blank UI)
    QVERIFY(!presentation.stateText(QStringLiteral("running")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("paused")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("exited")).isEmpty());
    QVERIFY(!presentation.stateText(QStringLiteral("no-such-state")).isEmpty());
    // Same texts as the container list (same C++ helper); whether they turn Chinese
    // is tst_i18n_consistency's job (only it runs with the catalogs and a zh_CN locale)
    QCOMPARE(presentation.stateText(QStringLiteral("running")), containerStateText(ContainerState::Running));

}


QTEST_GUILESS_MAIN(PresentationTest)

#include "tst_presentation.moc"
