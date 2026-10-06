#include "core/backup.h"
#include "core/database.h"
#include "core/lock.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &data = {})
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

// A lock in exactly the form the Python planner writes:
// 2026-10-05T12:34:56.123456+00:00
void writeForeign(const QString &path, int minutesAgo)
{
    const QDateTime when = QDateTime::currentDateTimeUtc().addSecs(-60 * minutesAgo);
    const QString hb = when.date().toString(Qt::ISODate) + QLatin1Char('T')
        + when.time().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(".123456+00:00");
    const QJsonObject data{{"host", "other-pc"}, {"user", "x"}, {"pid", 1},
                           {"started", hb}, {"heartbeat", hb}};
    writeFile(path, QJsonDocument(data).toJson());
}

}  // namespace

class TestLockBackup : public QObject
{
    Q_OBJECT

private slots:
    void pythonTimestampsParse()
    {
        lock::LockInfo info;
        info.heartbeat = QStringLiteral("2026-10-05T12:34:56.123456+00:00");
        QCOMPARE(info.heartbeatTime().toUTC(),
                 QDateTime(QDate(2026, 10, 5), QTime(12, 34, 56, 123), QTimeZone::UTC));
        info.heartbeat = QStringLiteral("2026-10-05T12:34:56+00:00");  // exactly on the second
        QCOMPARE(info.heartbeatTime().toUTC(),
                 QDateTime(QDate(2026, 10, 5), QTime(12, 34, 56), QTimeZone::UTC));
    }

    void foreignLiveLockBlocks()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("x.lock");
        writeForeign(p, 1);
        lock::Lock lk(p);
        const auto holder = lk.acquire();
        QVERIFY(holder.has_value());
        QCOMPARE(holder->host, QStringLiteral("other-pc"));
        QVERIFY(!lk.owned());
        QVERIFY(lock::blockingLock(p).has_value());
    }

    void staleAndForcedTakeover()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("x.lock");
        writeForeign(p, 30);
        lock::Lock lk(p);
        QVERIFY(!lk.acquire().has_value());
        QVERIFY(lk.owned());
        writeForeign(p, 1);
        QVERIFY(lk.heartbeat().has_value());  // someone else took over
        QVERIFY(!lk.owned());
        QVERIFY(!lk.acquire(true).has_value());
        QVERIFY(lk.owned());
    }

    void sameHostSharesAndRelease()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("x.lock");
        lock::Lock a(p);
        QVERIFY(!a.acquire().has_value());
        QVERIFY(!lock::blockingLock(p).has_value());  // this computer may write
        const auto written = lock::read(p);
        QVERIFY(written.has_value());
        QVERIFY(written->heartbeat.endsWith(QStringLiteral("+00:00")));
        QVERIFY(!written->isStale());
        QVERIFY(!QFile::exists(p + ".tmp"));
        a.release();
        QVERIFY(!QFile::exists(p));
    }

    void garbageLockIsIgnored()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("x.lock");
        writeFile(p, "not json");
        QVERIFY(!lock::read(p).has_value());
        lock::Lock lk(p);
        QVERIFY(!lk.acquire().has_value());
        QVERIFY(lk.owned());
    }

    void backupNames()
    {
        QCOMPARE(backup::backupName(QDate(2026, 9, 3)), QStringLiteral("lessons_09032026.db"));
        QCOMPARE(backup::backupDate("lessons_09032026.db"), QDate(2026, 9, 3));
        QVERIFY(!backup::backupDate("lessons_13402026.db").has_value());
        QVERIFY(!backup::backupDate("lessonplanner.db").has_value());
    }

    void backupAndPrune()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("lessonplanner.db"));
        db.saveEntry({1, QDate(2026, 9, 30), QStringLiteral("Lesson 1"), {}, {}});
        const QString old = dir.filePath("lessons_08012026.db");
        const QString keep = dir.filePath("lessons_09152026.db");
        writeFile(old);
        writeFile(keep);

        const QDate today(2026, 9, 30);
        const auto created = backup::runDailyBackup(db, dir.path(), 30, today);
        QVERIFY(created.has_value());
        QCOMPARE(*created, dir.filePath("lessons_09302026.db"));
        QVERIFY(!backup::runDailyBackup(db, dir.path(), 30, today).has_value());
        QVERIFY(!QFile::exists(old));
        QVERIFY(QFile::exists(keep));
        QVERIFY(!QFile::exists(dir.filePath("lessons_09302026.tmp")));

        Database copy(*created);
        QCOMPARE(copy.entry(1, QDate(2026, 9, 30)).lesson, QStringLiteral("Lesson 1"));
    }

    void secondsUntilMidnight()
    {
        const QDateTime now(QDate(2026, 9, 30), QTime(23, 59, 0));
        QCOMPARE(backup::secondsUntilMidnight(now), 65);
    }
};

QTEST_GUILESS_MAIN(TestLockBackup)
#include "test_lock_backup.moc"
