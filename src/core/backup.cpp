#include "core/backup.h"

#include "core/database.h"
#include "core/replace.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <sqlite3.h>

namespace backup {

QString backupName(QDate day)
{
    return QStringLiteral("lessons_%1.db").arg(day.toString(QStringLiteral("MMddyyyy")));
}

std::optional<QDate> backupDate(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^lessons_(\\d{2})(\\d{2})(\\d{4})\\.db$"));
    const QRegularExpressionMatch match = pattern.match(name);
    if (!match.hasMatch())
        return std::nullopt;
    const QDate day(match.captured(3).toInt(), match.captured(1).toInt(),
                    match.captured(2).toInt());
    return day.isValid() ? std::optional<QDate>(day) : std::nullopt;
}

std::optional<QString> runDailyBackup(Database &db, const QString &folder, int retentionDays,
                                      QDate today)
{
    const QDir dir(folder);
    const QString target = dir.filePath(backupName(today));
    std::optional<QString> created;
    if (!QFile::exists(target)) {
        const QString tmp = target.chopped(3) + QStringLiteral(".tmp");
        QFile::remove(tmp);  // left over from an interrupted backup
        sqlite3 *dest = nullptr;
        int rc = sqlite3_open(tmp.toUtf8().constData(), &dest);
        if (rc == SQLITE_OK) {
            sqlite3_backup *job = sqlite3_backup_init(dest, "main", db.handle(), "main");
            if (job) {
                sqlite3_backup_step(job, -1);
                rc = sqlite3_backup_finish(job);
            } else {
                rc = sqlite3_errcode(dest);
            }
        }
        const std::string message = dest ? sqlite3_errmsg(dest) : "out of memory";
        sqlite3_close(dest);
        if (rc != SQLITE_OK) {
            QFile::remove(tmp);
            throw DbError(message);
        }
        replaceFile(tmp, target);
        created = target;
    }
    prune(folder, retentionDays, today);
    return created;
}

void prune(const QString &folder, int retentionDays, QDate today)
{
    // Same default as the database setting. A bad value must not delete everything.
    if (retentionDays < 1)
        retentionDays = 30;
    const QDate cutoff = today.addDays(-retentionDays);
    const QDir dir(folder);
    for (const QString &name : dir.entryList({QStringLiteral("lessons_*.db")}, QDir::Files)) {
        const std::optional<QDate> day = backupDate(name);
        if (day && *day < cutoff)
            QFile::remove(dir.filePath(name));
    }
}

int secondsUntilMidnight(const QDateTime &now)
{
    const QDateTime tomorrow(now.date().addDays(1), QTime(0, 0));
    return int(now.secsTo(tomorrow)) + 5;
}

}  // namespace backup
