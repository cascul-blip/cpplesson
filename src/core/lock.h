// Lock file that keeps two computers from editing the synced database at once.
//
// The lock is per computer. Sync latency means this is best-effort, not a hard
// guarantee. `db_generation` is the database generation this computer had last
// saved, so the other one can wait until that copy arrives.
#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace lock {

inline constexpr int HEARTBEAT_SECONDS = 60;
inline constexpr int STALE_AFTER_SECONDS = 10 * 60;

QString thisHost();

struct LockInfo
{
    QString host;
    QString user;
    qint64 pid = 0;
    QString started;
    QString heartbeat;
    // Absent in a lock written before generations existed. That lock is treated
    // as already caught up.
    std::optional<qint64> dbGeneration;

    QDateTime heartbeatTime() const;
    bool isStale(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
    bool isThisHost() const;
    QString describe() const;
};

// The lock file's contents, or nothing if it is missing or unreadable.
std::optional<LockInfo> read(const QString &path);

// The lock that stops this computer from writing, if any.
std::optional<LockInfo> blockingLock(const QString &path);

// True when this database already contains the generation advertised by a lock.
// A lock with no generation does not hold editing back.
bool editsAreCurrent(qint64 localGeneration, const std::optional<qint64> &remoteGeneration);

class Lock
{
public:
    explicit Lock(const QString &path);

    bool owned() const { return m_owned; }
    QString path() const { return m_path; }
    std::optional<LockInfo> current() const { return read(m_path); }

    // Remember the open database's generation so the next write of the lock
    // publishes it.
    void noteGeneration(qint64 generation) { m_generation = generation; }

    // Take the lock. Returns the foreign holder instead if another computer
    // holds a live lock (unless `force`).
    std::optional<LockInfo> acquire(bool force = false);

    // Refresh the lock. Returns the new holder if another computer took it
    // over, in which case this instance no longer owns it.
    std::optional<LockInfo> heartbeat();

    void release();

private:
    void write();

    QString m_path;
    QString m_started;
    qint64 m_generation = 0;
    bool m_owned = false;
};

}  // namespace lock
