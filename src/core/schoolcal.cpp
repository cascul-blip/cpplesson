#include "core/schoolcal.h"

#include <QLocale>

namespace schoolcal {

QString format(QDate day, const QString &pattern)
{
    return QLocale::c().toString(day, pattern);
}

QDate mondayOf(QDate day)
{
    return day.addDays(1 - day.dayOfWeek());  // dayOfWeek(): Monday = 1 ... Sunday = 7
}

QDate currentMonday(QDate today)
{
    if (today.dayOfWeek() >= 6)
        today = today.addDays(8 - today.dayOfWeek());
    return mondayOf(today);
}

int weekNumber(QDate monday, QDate week1Monday)
{
    const qint64 days = week1Monday.daysTo(mondayOf(monday));
    const qint64 weeks = days >= 0 ? days / 7 : -((-days + 6) / 7);  // floor, for weeks before week 1
    return int(weeks) + 1;
}

QList<QDate> weekDays(QDate monday)
{
    QList<QDate> days;
    for (int i = 0; i < 5; ++i)
        days.append(monday.addDays(i));
    return days;
}

QString weekLabel(QDate monday, QDate week1Monday)
{
    return QStringLiteral("Week %1 · %2")
        .arg(weekNumber(monday, week1Monday))
        .arg(format(monday, QStringLiteral("MMM d")));
}

bool isSchoolDay(QDate day, const NoSchool &noSchool)
{
    return day.dayOfWeek() <= 5 && !noSchool.contains(day);
}

QDate nextSchoolDay(QDate day, const NoSchool &noSchool)
{
    day = day.addDays(1);
    while (!isSchoolDay(day, noSchool))
        day = day.addDays(1);
    return day;
}

QDate prevSchoolDay(QDate day, const NoSchool &noSchool)
{
    day = day.addDays(-1);
    while (!isSchoolDay(day, noSchool))
        day = day.addDays(-1);
    return day;
}

}  // namespace schoolcal
