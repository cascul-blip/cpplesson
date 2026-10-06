// Operations that shift lessons along the school calendar.
// Every operation runs in one transaction.
#pragma once

#include <QDate>
#include <QString>

#include <stdexcept>

class Database;

namespace schedule {

class ScheduleError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Open an empty slot at `day`: this class's lessons from `day` on move one
// school day later. Returns the number of lessons moved.
int insertLesson(Database &db, int classId, QDate day);

// Delete this class's lesson at `day` and pull its later lessons one school
// day earlier. Returns the number of lessons moved.
int removeLesson(Database &db, int classId, QDate day);

// Cancel school on `day`; every class's lessons from that day on move one
// school day later. Returns the number of lessons moved.
int markNoSchool(Database &db, QDate day, const QString &reason = {});

// Restore school on `day`; every class's later lessons move one school day
// earlier. Returns the number of lessons moved.
int unmarkNoSchool(Database &db, QDate day);

}  // namespace schedule
