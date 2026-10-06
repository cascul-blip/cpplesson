// Per-machine settings. Only the database folder lives here; everything else
// is stored in the database so it syncs between computers.
#pragma once

#include <QString>

#include <optional>

namespace config {

inline const QString APP_NAME = QStringLiteral("LessonPlanner");
inline const QString DB_FILENAME = QStringLiteral("lessonplanner.db");
inline const QString LOCK_FILENAME = QStringLiteral("lessonplanner.lock");

// Overrides the config file; handy for tests.
inline constexpr char ENV_DB_FOLDER[] = "LESSONPLANNER_DB_FOLDER";

QString configPath();

std::optional<QString> dbFolder();
void setDbFolder(const QString &folder);

// Replaces a leading "~" with the home folder.
QString expandUser(const QString &path);

QString dbPath(const QString &folder);
QString lockPath(const QString &folder);

}  // namespace config
