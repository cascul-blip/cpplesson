// SQLite storage.
//
// The database sits in a Nextcloud-synced folder, so it uses the rollback
// journal (not WAL, whose -wal/-shm side files would sync separately) and every
// change is committed immediately.
#pragma once

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

#include <stdexcept>

struct sqlite3;
struct sqlite3_stmt;

class DbError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct ClassInfo
{
    int id = 0;
    QString name;
    int sortOrder = 0;
    bool active = true;
};

// One row of an Options save. An id of 0 adds a class.
struct ClassUpdate
{
    int id = 0;
    QString name;
    int sortOrder = 0;
    bool active = true;
};

struct Entry
{
    int classId = 0;
    QDate date;
    QString lesson;
    QString special;
    QString homework;

    bool isEmpty() const;
};

// A prepared statement. Parameters are numbered from 1, columns from 0.
class Statement
{
public:
    Statement(sqlite3 *db, const char *sql);
    ~Statement();
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    Statement &bind(int index, const QString &value);
    Statement &bind(int index, int value);
    Statement &bind(int index, QDate value);
    bool step();  // true while there is a row to read
    void run();   // for statements that return nothing
    QString text(int column) const;
    int integer(int column) const;
    QDate date(int column) const;

private:
    sqlite3 *m_db;
    sqlite3_stmt *m_stmt = nullptr;
};

// Existing never creates a file. Create is for a new planner, the smoke test,
// and tests.
enum class DbOpen { Existing, Create };

class Database
{
public:
    explicit Database(const QString &path, DbOpen mode = DbOpen::Existing);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void close();
    QString path() const { return m_path; }
    sqlite3 *handle() const { return m_db; }
    void exec(const char *sql);

    // Changes whenever another connection commits.
    int dataVersion();

    QString setting(const QString &key);
    void setSetting(const QString &key, const QString &value);
    QDate week1Monday();
    int backupRetentionDays();

    QList<ClassInfo> classes(bool includeInactive = false);
    int addClass(const QString &name);
    void updateClass(int classId, const QString &name, int sortOrder, bool active);
    // Settings and the class list, in one commit. A name swap cannot fail halfway.
    void saveOptions(QDate week1Monday, int retentionDays, const QList<ClassUpdate> &classes);

    Entry entry(int classId, QDate day);
    QList<Entry> entriesBetween(QDate start, QDate end);
    // Insert/update the entry, or delete it when all fields are empty.
    void saveEntry(const Entry &entry);
    // The same, for callers that already hold a Transaction.
    void saveEntryNoCommit(const Entry &entry);
    QList<QDate> datesWithEntries();

    QMap<QDate, QString> noSchoolDays();

    // How many committed changes this file contains. One transaction is one step.
    // Opening the file does not change it. A rolled-back transaction does not either.
    qint64 generation();

private:
    friend class Transaction;
    void initSchema();
    void bumpGeneration();

    QString m_path;
    sqlite3 *m_db = nullptr;
};

// Commits on commit(); rolls back if destroyed without it (e.g. by an exception).
class Transaction
{
public:
    explicit Transaction(Database &db);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    void commit();

private:
    Database &m_db;
    int m_changes = 0;
    bool m_done = false;
};
