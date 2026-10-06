// Options: database folder, calendar, backups and the class list.
#pragma once

#include <QDialog>

#include <optional>

class Database;
class QDateEdit;
class QLineEdit;
class QListWidget;
class QSpinBox;

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    OptionsDialog(Database &db, const QString &folder, bool readOnly, QWidget *parent = nullptr);

    bool folderChanged() const { return m_folderChanged; }

private:
    void browse();
    void addClass();
    void renameClass();
    void moveClass(int delta);
    void save();

    Database &m_db;
    bool m_readOnly;
    bool m_folderChanged = false;
    QLineEdit *m_folderEdit;
    QDateEdit *m_week1;
    QSpinBox *m_retention;
    QListWidget *m_classList;
};

// First run: explain, then ask for the database folder. Nothing if cancelled.
std::optional<QString> chooseInitialFolder(QWidget *parent = nullptr);
