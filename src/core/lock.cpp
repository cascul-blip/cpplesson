#include "core/lock.h"

#include "core/schoolcal.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include <filesystem>
#include <stdexcept>

namespace lock {

namespace {

// UTC with an explicit "+00:00", the form every Python version can read back.
QString timestamp()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return now.date().toString(Qt::ISODate) + QLatin1Char('T')
        + now.time().toString(QStringLiteral("HH:mm:ss.zzz")) + QStringLiteral("+00:00");
}

QString thisUser()
{
    for (const char *name : {"LOGNAME", "USER", "LNAME", "USERNAME"}) {
        const QString value = qEnvironmentVariable(name);
        if (!value.isEmpty())
            return value;
    }
    return QStringLiteral("unknown");
}

}  // namespace

QString thisHost()
{
    return QSysInfo::machineHostName();
}

QDateTime LockInfo::heartbeatTime() const
{
    return QDateTime::fromString(heartbeat, Qt::ISODateWithMs);
}

bool LockInfo::isStale(const QDateTime &now) const
{
    return heartbeatTime().secsTo(now) > STALE_AFTER_SECONDS;
}

bool LockInfo::isThisHost() const
{
    return host == thisHost();
}

QString LockInfo::describe() const
{
    const QDateTime local = heartbeatTime().toLocalTime();
    return QStringLiteral("%1 on %2 (last active %3 %4)")
        .arg(user, host, schoolcal::format(local.date(), QStringLiteral("ddd MMM dd")),
             QLocale::c().toString(local.time(), QStringLiteral("hh:mm AP")));
}

std::optional<LockInfo> read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
    for (const char *key : {"host", "user", "pid", "started", "heartbeat"}) {
        if (!data.contains(QLatin1String(key)))
            return std::nullopt;
    }
    LockInfo info{data[QLatin1String("host")].toString(), data[QLatin1String("user")].toString(),
                  data[QLatin1String("pid")].toInteger(), data[QLatin1String("started")].toString(),
                  data[QLatin1String("heartbeat")].toString()};
    if (!info.heartbeatTime().isValid())
        return std::nullopt;
    return info;
}

std::optional<LockInfo> blockingLock(const QString &path)
{
    const std::optional<LockInfo> info = read(path);
    if (!info || info->isThisHost() || info->isStale())
        return std::nullopt;
    return info;
}

Lock::Lock(const QString &path) : m_path(path), m_started(timestamp()) {}

void Lock::write()
{
    const QJsonObject data{
        {QStringLiteral("host"), thisHost()},
        {QStringLiteral("user"), thisUser()},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("started"), m_started},
        {QStringLiteral("heartbeat"), timestamp()},
    };
    // write beside the lock, then swap it in so a reader never sees half a file
    const QString tmp = m_path + QStringLiteral(".tmp");
    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(QJsonDocument(data).toJson()) < 0) {
        throw std::runtime_error(
            QStringLiteral("Could not write %1: %2").arg(tmp, file.errorString()).toStdString());
    }
    file.close();
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(tmp.toStdU16String()),
                            std::filesystem::path(m_path.toStdU16String()), error);
    if (error) {
        throw std::runtime_error(QStringLiteral("Could not write %1: %2")
                                     .arg(m_path, QString::fromStdString(error.message()))
                                     .toStdString());
    }
}

std::optional<LockInfo> Lock::acquire(bool force)
{
    if (!force) {
        if (const std::optional<LockInfo> holder = blockingLock(m_path)) {
            m_owned = false;
            return holder;
        }
    }
    write();
    m_owned = true;
    return std::nullopt;
}

std::optional<LockInfo> Lock::heartbeat()
{
    if (!m_owned)
        return std::nullopt;
    const std::optional<LockInfo> current = read(m_path);
    if (current && !current->isThisHost() && !current->isStale()) {
        m_owned = false;
        return current;
    }
    write();
    return std::nullopt;
}

void Lock::release()
{
    if (!m_owned)
        return;
    const std::optional<LockInfo> current = read(m_path);
    if (current && current->isThisHost() && current->pid == QCoreApplication::applicationPid())
        QFile::remove(m_path);
    m_owned = false;
}

}  // namespace lock
