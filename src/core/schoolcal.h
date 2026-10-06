// School calendar: school days, week numbers and week <-> date math.
#pragma once

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

namespace schoolcal {

// No-school days and their reasons.
using NoSchool = QMap<QDate, QString>;

// Formats with English day/month names whatever the system language, e.g.
// "ddd MMM d" -> "Mon Oct 5".
QString format(QDate day, const QString &pattern);

QDate mondayOf(QDate day);
// Monday of the current week; on weekends, the upcoming week.
QDate currentMonday(QDate today = QDate::currentDate());
int weekNumber(QDate monday, QDate week1Monday);
QDate weekMonday(int number, QDate week1Monday);
QList<QDate> weekDays(QDate monday);
QString weekLabel(QDate monday, QDate week1Monday);

bool isSchoolDay(QDate day, const NoSchool &noSchool);
QDate nextSchoolDay(QDate day, const NoSchool &noSchool);
QDate prevSchoolDay(QDate day, const NoSchool &noSchool);

}  // namespace schoolcal
