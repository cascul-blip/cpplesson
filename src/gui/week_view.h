// Grid for one week: days down the side, classes across the top.
#pragma once

#include "core/database.h"

#include <QLabel>
#include <QMap>
#include <QScrollArea>

#include <optional>
#include <utility>

class CellWidget;
class QMenu;

class NoSchoolBanner : public QLabel
{
    Q_OBJECT

public:
    NoSchoolBanner(QDate day, const QString &reason, QWidget *parent = nullptr);

signals:
    void menuRequested(QDate day, QPoint globalPos);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QDate m_date;
};

class WeekView : public QScrollArea
{
    Q_OBJECT

public:
    WeekView(Database *db, QDate monday, QWidget *parent = nullptr);

    QDate monday() const { return m_monday; }
    bool loaded() const { return m_structure.has_value(); }
    void setDatabase(Database *db) { m_db = db; }
    CellWidget *cell(int classId, QDate day) const;

    // Refresh contents, rebuilding the grid only if its shape changed.
    // Cells being edited keep their text unless `force`.
    void reload(bool force = false);
    void flush();
    void setReadOnly(bool readOnly);

signals:
    void entryChanged(const Entry &entry);
    void cellMenuRequested(int classId, QDate day, QPoint globalPos, QMenu *standardMenu);
    void noSchoolMenuRequested(QDate day, QPoint globalPos);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // What decides the grid's shape: the class columns and the no-school rows.
    struct Structure
    {
        QList<std::pair<int, QString>> classes;
        QList<std::pair<QDate, QString>> noSchool;
        bool operator==(const Structure &) const = default;
    };

    Structure currentStructure();
    void build(const Structure &structure);

    Database *m_db;
    QDate m_monday;
    bool m_readOnly = false;
    QMap<std::pair<int, QDate>, CellWidget *> m_cells;
    std::optional<Structure> m_structure;
};
