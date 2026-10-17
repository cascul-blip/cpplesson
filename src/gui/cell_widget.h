// One class on one day: Lesson / Special / Homework editors with autosave.
#pragma once

#include "core/database.h"

#include <QFrame>
#include <QPlainTextEdit>

class QLabel;
class QMenu;
class QTimer;

// Plain text editor that grows with its content.
class FieldEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit FieldEdit(int minLines, QWidget *parent = nullptr);

    void fit();

signals:
    void focusLost();

protected:
    void focusOutEvent(QFocusEvent *event) override;

private:
    int m_minLines;
};

// Emits changed(entry) after edits settle and menuRequested on right-click.
class CellWidget : public QFrame
{
    Q_OBJECT

public:
    CellWidget(int classId, QDate day, QWidget *parent = nullptr);

    Entry entry() const;
    // Show `entry` unless the user is in the middle of editing this cell.
    void setEntry(const Entry &entry, bool force = false);
    bool hasEditFocus() const;
    void setReadOnly(bool readOnly);
    bool isDirty() const { return m_dirty; }
    void flush();
    void focusLesson();

signals:
    // `saved` is set when a receiver has stored the entry. It stays false when
    // the save is refused (read-only) or throws.
    void changed(const Entry &entry, bool *saved);
    // The receiver takes over `standardMenu` (the editor's own cut/copy/paste menu).
    void menuRequested(int classId, QDate day, QPoint globalPos, QMenu *standardMenu);

private:
    enum Field { Lesson, Special, Homework, FieldCount };

    void onTextChanged();
    void updateSpecialStyle();
    void showContextMenu(FieldEdit *edit, QPoint pos);

    int m_classId;
    QDate m_date;
    bool m_dirty = false;
    FieldEdit *m_edits[FieldCount] = {};
    QLabel *m_labels[FieldCount] = {};
    QTimer *m_timer;
};
