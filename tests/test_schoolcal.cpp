#include "core/schoolcal.h"

#include <QTest>

class TestSchoolcal : public QObject
{
    Q_OBJECT

private slots:
    void weekNumbers()
    {
        const QDate week1(2026, 9, 7);
        QCOMPARE(schoolcal::weekNumber(QDate(2026, 9, 28), week1), 4);
        QCOMPARE(schoolcal::weekNumber(QDate(2026, 9, 30), week1), 4);
        QCOMPARE(schoolcal::weekNumber(QDate(2026, 9, 7), week1), 1);
        QCOMPARE(schoolcal::weekNumber(QDate(2026, 9, 4), week1), 0);  // the Friday before
        QCOMPARE(schoolcal::weekNumber(QDate(2026, 8, 24), week1), -1);
        QCOMPARE(schoolcal::weekLabel(QDate(2026, 9, 28), week1), QStringLiteral("Week 4 · Sep 28"));
    }

    void mondayOfEveryWeekday()
    {
        for (int i = 0; i < 7; ++i)  // Mon Sep 28 ... Sun Oct 4
            QCOMPARE(schoolcal::mondayOf(QDate(2026, 9, 28).addDays(i)), QDate(2026, 9, 28));
    }

    void currentWeekOnWeekendIsUpcoming()
    {
        QCOMPARE(schoolcal::currentMonday(QDate(2026, 9, 30)), QDate(2026, 9, 28));
        QCOMPARE(schoolcal::currentMonday(QDate(2026, 10, 3)), QDate(2026, 10, 5));
        QCOMPARE(schoolcal::currentMonday(QDate(2026, 10, 4)), QDate(2026, 10, 5));
    }

    void schoolDayNavigationSkipsWeekendsAndHolidays()
    {
        const schoolcal::NoSchool off{{QDate(2026, 10, 5), QString()}};
        QCOMPARE(schoolcal::nextSchoolDay(QDate(2026, 10, 2), off), QDate(2026, 10, 6));
        QCOMPARE(schoolcal::prevSchoolDay(QDate(2026, 10, 6), off), QDate(2026, 10, 2));
    }
};

QTEST_GUILESS_MAIN(TestSchoolcal)
#include "test_schoolcal.moc"
