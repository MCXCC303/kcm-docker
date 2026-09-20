/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/format.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * Presentation-layer formatting tests (ARCH_V1 §39).
 * Time is domain data, but the UI text is generated here, so this behavior must be testable.
 */
class FormatTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void invalidDateTimesFormatToEmpty();
    void recentTimestampIsElapsed();
    void longDurationsAreSpelledOut();
    void futureTimestampsDoNotProduceNegativeDurations();
    void byteSizeIsHumanReadable();
};

void FormatTest::initTestCase()
{
    setupTranslationDomain();
}

void FormatTest::invalidDateTimesFormatToEmpty()
{
    const Format format;
    QVERIFY(format.elapsed(QDateTime()).isEmpty());
    QVERIFY(format.absoluteTime(QDateTime()).isEmpty());
}

void FormatTest::recentTimestampIsElapsed()
{
    const Format format;
    const QString text = format.elapsed(QDateTime::currentDateTimeUtc().addSecs(-5));
    QVERIFY(!text.isEmpty());
    // Under a minute: no "0 seconds" noise
    QVERIFY(!text.contains(QLatin1Char('0')));
}

void FormatTest::longDurationsAreSpelledOut()
{
    const Format format;
    const QString text = format.elapsed(QDateTime::currentDateTimeUtc().addDays(-3));
    QVERIFY(!text.isEmpty());
    // Natural-language duration ("3 days" / "3 天"), not a numeric "72:00:00"
    QVERIFY(!text.contains(QLatin1Char(':')));
}

void FormatTest::futureTimestampsDoNotProduceNegativeDurations()
{
    const Format format;
    const QString text = format.elapsed(QDateTime::currentDateTimeUtc().addSecs(3600));
    QVERIFY(!text.isEmpty());
    QVERIFY(!text.contains(QLatin1Char('-')));
}

void FormatTest::byteSizeIsHumanReadable()
{
    const Format format;
    QVERIFY(!format.byteSize(0).isEmpty());
    QVERIFY(!format.byteSize(Q_INT64_C(840000000)).isEmpty());
    QVERIFY(!format.byteSize(Q_INT64_C(34500000000)).isEmpty());
    // Negative sizes (e.g. -1 when the engine omits the value) must not yield nonsense units
    QVERIFY(format.byteSize(-1).isEmpty());
}

QTEST_GUILESS_MAIN(FormatTest)

#include "tst_format.moc"
