#include "core/database.h"
#include "core/schedule.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

using Lessons = QMap<QDate, QString>;

const QDate THU(2026, 10, 1), FRI(2026, 10, 2), MON(2026, 10, 5), TUE(2026, 10, 6);

class TestSchedule : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Database> m_db;

    Lessons lessons(int classId)
    {
        Lessons result;
        for (const Entry &e : m_db->entriesBetween(QDate(2026, 1, 1), QDate(2027, 12, 31))) {
            if (e.classId == classId)
                result.insert(e.date, e.lesson);
        }
        return result;
    }

    void seed(int classId, const QList<QDate> &days, const QString &prefix = QStringLiteral("Lesson"))
    {
        int i = 1;
        for (QDate d : days)
            m_db->saveEntry({classId, d, QStringLiteral("%1 %2").arg(prefix).arg(i++), {}, {}});
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_db = std::make_unique<Database>(m_dir->filePath(QStringLiteral("lessonplanner.db")));
    }

    void cleanup()
    {
        m_db.reset();
        m_dir.reset();
    }

    void newDatabaseHasDefaults()
    {
        QCOMPARE(m_db->classes().size(), 7);
        QCOMPARE(m_db->classes().first().name, QStringLiteral("Math 87"));
        QCOMPARE(m_db->classes().first().id, 1);
        QCOMPARE(m_db->week1Monday(), QDate(2026, 9, 7));
        QCOMPARE(m_db->backupRetentionDays(), 30);
    }

    void insertPushesAcrossWeekBoundary()
    {
        seed(1, {THU, FRI, MON});
        seed(2, {THU, FRI}, QStringLiteral("Other"));
        QCOMPARE(schedule::insertLesson(*m_db, 1, THU), 3);
        QCOMPARE(lessons(1), (Lessons{{FRI, "Lesson 1"}, {MON, "Lesson 2"}, {TUE, "Lesson 3"}}));
        QCOMPARE(lessons(2), (Lessons{{THU, "Other 1"}, {FRI, "Other 2"}}));  // untouched
    }

    void removePullsBackAndRoundTrips()
    {
        seed(1, {THU, FRI, MON});
        const Lessons original = lessons(1);
        schedule::insertLesson(*m_db, 1, THU);
        schedule::removeLesson(*m_db, 1, THU);  // removes the empty slot
        QCOMPARE(lessons(1), original);
        schedule::removeLesson(*m_db, 1, FRI);
        QCOMPARE(lessons(1), (Lessons{{THU, "Lesson 1"}, {FRI, "Lesson 3"}}));
    }

    void gapsArePreserved()
    {
        seed(1, {THU, MON});  // Friday intentionally empty
        schedule::insertLesson(*m_db, 1, THU);
        QCOMPARE(lessons(1), (Lessons{{FRI, "Lesson 1"}, {TUE, "Lesson 2"}}));
    }

    void noSchoolShiftsEveryClassAndRoundTrips()
    {
        seed(1, {THU, FRI, MON});
        seed(2, {FRI, MON}, QStringLiteral("Other"));
        schedule::markNoSchool(*m_db, FRI, QStringLiteral("Snow day"));
        QCOMPARE(m_db->noSchoolDays(), (QMap<QDate, QString>{{FRI, "Snow day"}}));
        QCOMPARE(lessons(1), (Lessons{{THU, "Lesson 1"}, {MON, "Lesson 2"}, {TUE, "Lesson 3"}}));
        QCOMPARE(lessons(2), (Lessons{{MON, "Other 1"}, {TUE, "Other 2"}}));
        // later inserts skip the no-school day
        schedule::insertLesson(*m_db, 1, THU);
        QCOMPARE(lessons(1), (Lessons{{MON, "Lesson 1"}, {TUE, "Lesson 2"},
                                      {QDate(2026, 10, 7), "Lesson 3"}}));
        schedule::removeLesson(*m_db, 1, THU);
        schedule::unmarkNoSchool(*m_db, FRI);
        QVERIFY(m_db->noSchoolDays().isEmpty());
        QCOMPARE(lessons(1), (Lessons{{THU, "Lesson 1"}, {FRI, "Lesson 2"}, {MON, "Lesson 3"}}));
        QCOMPARE(lessons(2), (Lessons{{FRI, "Other 1"}, {MON, "Other 2"}}));
    }

    void rejectsNonSchoolDays()
    {
        QVERIFY_THROWS_EXCEPTION(schedule::ScheduleError,
                                 schedule::insertLesson(*m_db, 1, QDate(2026, 10, 3)));  // Saturday
        schedule::markNoSchool(*m_db, FRI);
        QVERIFY_THROWS_EXCEPTION(schedule::ScheduleError, schedule::removeLesson(*m_db, 1, FRI));
    }

    void emptyEntriesAreDeleted()
    {
        m_db->saveEntry({1, THU, QStringLiteral("Lesson 1"), {}, {}});
        m_db->saveEntry({1, THU, QStringLiteral("  "), {}, {}});
        QVERIFY(lessons(1).isEmpty());
    }

    void failedOperationRollsBack()
    {
        seed(1, {THU});
        // class 99 does not exist, so the foreign key rejects the second write
        {
            Transaction transaction(*m_db);
            m_db->saveEntryNoCommit({1, FRI, QStringLiteral("kept only if committed"), {}, {}});
            QVERIFY_THROWS_EXCEPTION(DbError, m_db->saveEntryNoCommit(
                                                  {99, FRI, QStringLiteral("x"), {}, {}}));
        }
        QCOMPARE(lessons(1), (Lessons{{THU, "Lesson 1"}}));
    }

    void reopeningKeepsData()
    {
        seed(1, {THU});
        m_db->setSetting(QStringLiteral("backup_retention_days"), QStringLiteral("7"));
        const QString path = m_db->path();
        m_db.reset();
        m_db = std::make_unique<Database>(path);
        QCOMPARE(lessons(1), (Lessons{{THU, "Lesson 1"}}));
        QCOMPARE(m_db->backupRetentionDays(), 7);
        QCOMPARE(m_db->classes().size(), 7);  // defaults are not added twice
    }
};

QTEST_GUILESS_MAIN(TestSchedule)
#include "test_schedule.moc"
