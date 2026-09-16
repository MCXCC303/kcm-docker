/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/format.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * presentation layer 格式化测试（ARCH_V1 §39）。
 * 时间是 domain data，UI 文案由这里生成，因此这里的行为需要可测。
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
    // 不到一分钟不应显示 "0 seconds" 这类噪声
    QVERIFY(!text.contains(QLatin1Char('0')));
}

void FormatTest::longDurationsAreSpelledOut()
{
    const Format format;
    const QString text = format.elapsed(QDateTime::currentDateTimeUtc().addDays(-3));
    QVERIFY(!text.isEmpty());
    // 自然语言时长（"3 days" / "3 天"），不是 "72:00:00" 这种数字格式
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
    // 负数（引擎未提供时可能是 -1）不应展示成奇怪的单位
    QVERIFY(format.byteSize(-1).isEmpty());
}

QTEST_GUILESS_MAIN(FormatTest)

#include "tst_format.moc"
