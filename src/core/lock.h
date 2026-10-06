// Lock file that keeps two computers from editing the synced database at once.
//
// The lock is per computer. Sync latency means this is best-effort, not a hard
// guarantee. The file format is shared with the Python version of the planner.
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

    QDateTime heartbeatTime() const;
    bool isStale(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
    bool isThisHost() const;
    QString describe() const;
};

// The lock file's contents, or nothing if it is missing or unreadable.
std::optional<LockInfo> read(const QString &path);

// The lock that stops this computer from writing, if any.
std::optional<LockInfo> blockingLock(const QString &path);

class Lock
{
public:
    explicit Lock(const QString &path);

    bool owned() const { return m_owned; }

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
    bool m_owned = false;
};

}  // namespace lock
