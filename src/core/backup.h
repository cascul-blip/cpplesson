// Daily database backups named lessons_MMDDYYYY.db, kept next to the database.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

class Database;

namespace backup {

QString backupName(QDate day);
std::optional<QDate> backupDate(const QString &name);

// Back up today's database if not done yet and prune old backups.
// Returns the new backup's path, or nothing if one already existed.
std::optional<QString> runDailyBackup(Database &db, const QString &folder, int retentionDays,
                                      QDate today = QDate::currentDate());

// Deletes backups older than `retentionDays`; returns the paths removed.
QStringList prune(const QString &folder, int retentionDays, QDate today = QDate::currentDate());

int secondsUntilMidnight(const QDateTime &now = QDateTime::currentDateTime());

}  // namespace backup
