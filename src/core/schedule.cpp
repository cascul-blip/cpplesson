#include "core/schedule.h"

#include "core/database.h"
#include "core/schoolcal.h"

namespace schedule {

namespace {

using schoolcal::NoSchool;

QList<Entry> classEntriesFrom(Database &db, int classId, QDate start)
{
    Statement statement(db.handle(), "SELECT class_id, date, lesson, special, homework "
                                     "FROM entries WHERE class_id=? AND date>=? ORDER BY date");
    statement.bind(1, classId).bind(2, start);
    QList<Entry> result;
    while (statement.step()) {
        result.append({statement.integer(0), statement.date(1), statement.text(2),
                       statement.text(3), statement.text(4)});
    }
    return result;
}

void deleteEntry(Database &db, int classId, QDate day)
{
    Statement(db.handle(), "DELETE FROM entries WHERE class_id=? AND date=?")
        .bind(1, classId).bind(2, day).run();
}

// Rewrite entries one school day later or earlier. Caller holds the transaction.
void move(Database &db, const QList<Entry> &entries, const NoSchool &noSchool, bool forward)
{
    for (const Entry &e : entries)
        deleteEntry(db, e.classId, e.date);
    for (Entry e : entries) {
        e.date = forward ? schoolcal::nextSchoolDay(e.date, noSchool)
                         : schoolcal::prevSchoolDay(e.date, noSchool);
        db.saveEntryNoCommit(e);
    }
}

void requireSchoolDay(QDate day, const NoSchool &noSchool)
{
    if (!schoolcal::isSchoolDay(day, noSchool)) {
        throw ScheduleError(
            (day.toString(Qt::ISODate) + QStringLiteral(" is not a school day")).toStdString());
    }
}

int pushForward(Database &db, int classId, QDate start, const NoSchool &noSchool)
{
    const QList<Entry> entries = classEntriesFrom(db, classId, start);
    move(db, entries, noSchool, true);
    return int(entries.size());
}

int pullBack(Database &db, int classId, QDate start, const NoSchool &noSchool)
{
    const QList<Entry> entries = classEntriesFrom(db, classId, start);
    move(db, entries, noSchool, false);
    return int(entries.size());
}

}  // namespace

int insertLesson(Database &db, int classId, QDate day)
{
    const NoSchool noSchool = db.noSchoolDays();
    requireSchoolDay(day, noSchool);
    Transaction transaction(db);
    const int moved = pushForward(db, classId, day, noSchool);
    transaction.commit();
    return moved;
}

int removeLesson(Database &db, int classId, QDate day)
{
    const NoSchool noSchool = db.noSchoolDays();
    requireSchoolDay(day, noSchool);
    Transaction transaction(db);
    deleteEntry(db, classId, day);
    const int moved = pullBack(db, classId, schoolcal::nextSchoolDay(day, noSchool), noSchool);
    transaction.commit();
    return moved;
}

int markNoSchool(Database &db, QDate day, const QString &reason)
{
    NoSchool noSchool = db.noSchoolDays();
    if (noSchool.contains(day)) {
        Statement(db.handle(), "UPDATE no_school SET reason=? WHERE date=?")
            .bind(1, reason).bind(2, day).run();
        return 0;
    }
    requireSchoolDay(day, noSchool);
    noSchool.insert(day, reason);
    int moved = 0;
    Transaction transaction(db);
    for (const ClassInfo &c : db.classes(true))
        moved += pushForward(db, c.id, day, noSchool);
    Statement(db.handle(), "INSERT INTO no_school(date, reason) VALUES (?, ?)")
        .bind(1, day).bind(2, reason).run();
    transaction.commit();
    return moved;
}

int unmarkNoSchool(Database &db, QDate day)
{
    NoSchool noSchool = db.noSchoolDays();
    if (!noSchool.contains(day))
        return 0;
    noSchool.remove(day);
    int moved = 0;
    Transaction transaction(db);
    Statement(db.handle(), "DELETE FROM no_school WHERE date=?").bind(1, day).run();
    const QDate after = schoolcal::nextSchoolDay(day, noSchool);
    for (const ClassInfo &c : db.classes(true))
        moved += pullBack(db, c.id, after, noSchool);
    transaction.commit();
    return moved;
}

}  // namespace schedule
