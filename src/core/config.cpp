#include "core/config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace config {

namespace {

QJsonObject load()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void save(const QJsonObject &cfg)
{
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(cfg).toJson());
}

}  // namespace

QString configPath()
{
    // ~/.config/LessonPlanner on Linux, %LOCALAPPDATA%\LessonPlanner on Windows:
    // the same file the Python version used.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1Char('/') + APP_NAME + QStringLiteral("/config.json");
}

std::optional<QString> dbFolder()
{
    QString folder = qEnvironmentVariable(ENV_DB_FOLDER);
    if (folder.isEmpty())
        folder = load().value(QStringLiteral("db_folder")).toString();
    if (folder.isEmpty())
        return std::nullopt;
    return expandUser(folder);
}

void setDbFolder(const QString &folder)
{
    QJsonObject cfg = load();
    cfg[QStringLiteral("db_folder")] = QDir::toNativeSeparators(folder);
    save(cfg);
}

QString expandUser(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")) || path.startsWith(QLatin1String("~\\")))
        return QDir::homePath() + QLatin1Char('/') + path.mid(2);
    return path;
}

QString dbPath(const QString &folder)
{
    return QDir(folder).filePath(DB_FILENAME);
}

QString lockPath(const QString &folder)
{
    return QDir(folder).filePath(LOCK_FILENAME);
}

}  // namespace config
