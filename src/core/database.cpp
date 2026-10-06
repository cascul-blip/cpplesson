#include "core/database.h"

#include <QDateTime>

#include <sqlite3.h>

namespace {

const char *const DEFAULT_CLASSES[] = {
    "Math 87", "Algebra 1", "Algebra 2", "Consumer Math", "Science 8", "Biology", "Physics",
};

const QMap<QString, QString> &defaultSettings()
{
    static const QMap<QString, QString> settings = {
        {QStringLiteral("week1_monday"), QStringLiteral("2026-09-07")},
        {QStringLiteral("backup_retention_days"), QStringLiteral("30")},
    };
    return settings;
}

const char *const SCHEMA = R"sql(
CREATE TABLE IF NOT EXISTS classes (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    sort_order INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS entries (
    id INTEGER PRIMARY KEY,
    class_id INTEGER NOT NULL REFERENCES classes(id),
    date TEXT NOT NULL,
    lesson TEXT NOT NULL DEFAULT '',
    special TEXT NOT NULL DEFAULT '',
    homework TEXT NOT NULL DEFAULT '',
    updated_at TEXT NOT NULL,
    UNIQUE (class_id, date)
);
CREATE INDEX IF NOT EXISTS entries_date ON entries(date);
CREATE TABLE IF NOT EXISTS no_school (
    date TEXT PRIMARY KEY,
    reason TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)sql";
const int SCHEMA_VERSION = 1;
const int BUSY_TIMEOUT_MS = 10000;

[[noreturn]] void fail(sqlite3 *db)
{
    throw DbError(db ? sqlite3_errmsg(db) : "the database is closed");
}

QString now()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

Entry entryFromRow(const Statement &row)
{
    return Entry{row.integer(0), row.date(1), row.text(2), row.text(3), row.text(4)};
}

}  // namespace

bool Entry::isEmpty() const
{
    return lesson.trimmed().isEmpty() && special.trimmed().isEmpty()
        && homework.trimmed().isEmpty();
}

// --- Statement ---------------------------------------------------------------

Statement::Statement(sqlite3 *db, const char *sql) : m_db(db)
{
    if (!db || sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
        fail(db);
}

Statement::~Statement()
{
    sqlite3_finalize(m_stmt);
}

Statement &Statement::bind(int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    if (sqlite3_bind_text(m_stmt, index, utf8.constData(), int(utf8.size()), SQLITE_TRANSIENT)
        != SQLITE_OK)
        fail(m_db);
    return *this;
}

Statement &Statement::bind(int index, int value)
{
    if (sqlite3_bind_int(m_stmt, index, value) != SQLITE_OK)
        fail(m_db);
    return *this;
}

Statement &Statement::bind(int index, QDate value)
{
    return bind(index, value.toString(Qt::ISODate));
}

bool Statement::step()
{
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
        return true;
    if (rc != SQLITE_DONE)
        fail(m_db);
    return false;
}

void Statement::run()
{
    while (step()) {
    }
}

QString Statement::text(int column) const
{
    const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column));
    return QString::fromUtf8(data, sqlite3_column_bytes(m_stmt, column));
}

int Statement::integer(int column) const
{
    return sqlite3_column_int(m_stmt, column);
}

QDate Statement::date(int column) const
{
    return QDate::fromString(text(column), Qt::ISODate);
}

// --- Transaction -------------------------------------------------------------

Transaction::Transaction(Database &db) : m_db(db)
{
    m_db.exec("BEGIN");
}

Transaction::~Transaction()
{
    if (!m_done && m_db.handle())
        sqlite3_exec(m_db.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
}

void Transaction::commit()
{
    m_db.exec("COMMIT");
    m_done = true;
}

// --- Database ----------------------------------------------------------------

Database::Database(const QString &path) : m_path(path)
{
    if (sqlite3_open(path.toUtf8().constData(), &m_db) != SQLITE_OK) {
        const DbError error(m_db ? sqlite3_errmsg(m_db) : "out of memory");
        close();
        throw error;
    }
    try {
        sqlite3_busy_timeout(m_db, BUSY_TIMEOUT_MS);
        exec("PRAGMA journal_mode=DELETE");
        exec("PRAGMA foreign_keys=ON");
        initSchema();
    } catch (...) {
        close();
        throw;
    }
}

Database::~Database()
{
    close();
}

void Database::close()
{
    sqlite3_close(m_db);
    m_db = nullptr;
}

void Database::exec(const char *sql)
{
    if (!m_db || sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        fail(m_db);
}

void Database::initSchema()
{
    Transaction transaction(*this);
    exec(SCHEMA);
    int version = 0;
    {
        Statement statement(m_db, "PRAGMA user_version");
        statement.step();
        version = statement.integer(0);
    }
    if (version == 0) {
        const auto &defaults = defaultSettings();
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            Statement(m_db, "INSERT OR IGNORE INTO settings(key, value) VALUES (?, ?)")
                .bind(1, it.key()).bind(2, it.value()).run();
        }
        if (!Statement(m_db, "SELECT 1 FROM classes").step()) {
            int order = 0;
            for (const char *name : DEFAULT_CLASSES) {
                Statement(m_db, "INSERT INTO classes(name, sort_order) VALUES (?, ?)")
                    .bind(1, QString::fromUtf8(name)).bind(2, order++).run();
            }
        }
        exec(qPrintable(QStringLiteral("PRAGMA user_version=%1").arg(SCHEMA_VERSION)));
    }
    transaction.commit();
}

int Database::dataVersion()
{
    Statement statement(m_db, "PRAGMA data_version");
    statement.step();
    return statement.integer(0);
}

// --- settings ----------------------------------------------------------------

QString Database::setting(const QString &key)
{
    Statement statement(m_db, "SELECT value FROM settings WHERE key=?");
    statement.bind(1, key);
    return statement.step() ? statement.text(0) : defaultSettings().value(key);
}

void Database::setSetting(const QString &key, const QString &value)
{
    Statement(m_db, "INSERT INTO settings(key, value) VALUES (?, ?) "
                    "ON CONFLICT(key) DO UPDATE SET value=excluded.value")
        .bind(1, key).bind(2, value).run();
}

QDate Database::week1Monday()
{
    return QDate::fromString(setting(QStringLiteral("week1_monday")), Qt::ISODate);
}

int Database::backupRetentionDays()
{
    return setting(QStringLiteral("backup_retention_days")).toInt();
}

// --- classes -----------------------------------------------------------------

QList<ClassInfo> Database::classes(bool includeInactive)
{
    Statement statement(m_db, includeInactive
        ? "SELECT id, name, sort_order, active FROM classes ORDER BY sort_order, id"
        : "SELECT id, name, sort_order, active FROM classes WHERE active=1 "
          "ORDER BY sort_order, id");
    QList<ClassInfo> result;
    while (statement.step()) {
        result.append({statement.integer(0), statement.text(1), statement.integer(2),
                       statement.integer(3) != 0});
    }
    return result;
}

int Database::addClass(const QString &name)
{
    Transaction transaction(*this);
    int order = 0;
    {
        Statement next(m_db, "SELECT COALESCE(MAX(sort_order), -1) + 1 FROM classes");
        next.step();
        order = next.integer(0);
    }
    Statement(m_db, "INSERT INTO classes(name, sort_order) VALUES (?, ?)")
        .bind(1, name).bind(2, order).run();
    const int id = int(sqlite3_last_insert_rowid(m_db));
    transaction.commit();
    return id;
}

void Database::updateClass(int classId, const QString &name, int sortOrder, bool active)
{
    Statement(m_db, "UPDATE classes SET name=?, sort_order=?, active=? WHERE id=?")
        .bind(1, name).bind(2, sortOrder).bind(3, int(active)).bind(4, classId).run();
}

// --- entries -----------------------------------------------------------------

Entry Database::entry(int classId, QDate day)
{
    Statement statement(m_db, "SELECT class_id, date, lesson, special, homework FROM entries "
                              "WHERE class_id=? AND date=?");
    statement.bind(1, classId).bind(2, day);
    if (!statement.step())
        return Entry{classId, day, {}, {}, {}};
    return entryFromRow(statement);
}

QList<Entry> Database::entriesBetween(QDate start, QDate end)
{
    Statement statement(m_db, "SELECT class_id, date, lesson, special, homework FROM entries "
                              "WHERE date BETWEEN ? AND ? ORDER BY date");
    statement.bind(1, start).bind(2, end);
    QList<Entry> result;
    while (statement.step())
        result.append(entryFromRow(statement));
    return result;
}

void Database::saveEntry(const Entry &entry)
{
    Transaction transaction(*this);
    saveEntryNoCommit(entry);
    transaction.commit();
}

void Database::saveEntryNoCommit(const Entry &entry)
{
    if (entry.isEmpty()) {
        Statement(m_db, "DELETE FROM entries WHERE class_id=? AND date=?")
            .bind(1, entry.classId).bind(2, entry.date).run();
        return;
    }
    Statement(m_db,
              "INSERT INTO entries(class_id, date, lesson, special, homework, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(class_id, date) DO UPDATE SET "
              "lesson=excluded.lesson, special=excluded.special, homework=excluded.homework, "
              "updated_at=excluded.updated_at")
        .bind(1, entry.classId).bind(2, entry.date).bind(3, entry.lesson)
        .bind(4, entry.special).bind(5, entry.homework).bind(6, now()).run();
}

QList<QDate> Database::datesWithEntries()
{
    Statement statement(m_db, "SELECT DISTINCT date FROM entries ORDER BY date");
    QList<QDate> result;
    while (statement.step())
        result.append(statement.date(0));
    return result;
}

// --- no-school days ----------------------------------------------------------

QMap<QDate, QString> Database::noSchoolDays()
{
    Statement statement(m_db, "SELECT date, reason FROM no_school");
    QMap<QDate, QString> result;
    while (statement.step())
        result.insert(statement.date(0), statement.text(1));
    return result;
}
