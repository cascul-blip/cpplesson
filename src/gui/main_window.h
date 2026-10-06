// Main window: week tabs, read-only banner, menus and background timers.
#pragma once

#include "core/database.h"
#include "core/fileid.h"
#include "core/lock.h"

#include <QMainWindow>
#include <QMap>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QTabWidget;
class QTimer;
class WeekView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `holder` is the other computer that has the lock, if any; the window
    // then opens read-only. `fileLock` must outlive the window.
    MainWindow(const QString &folder, std::unique_ptr<Database> db, lock::Lock &fileLock,
               const std::optional<lock::LockInfo> &holder);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildBanner();
    void buildMenus();

    // weeks / tabs
    QList<QDate> weekList();
    WeekView *currentView() const;
    WeekView *view(QDate monday);
    QString tabText(QDate monday);
    void rebuildTabs(QDate select = {});
    void goToCurrentWeek();
    void addNextWeek();
    void flushAll();
    void refreshAll(bool force = false);
    void afterChange(const QString &message = {});

    // editing
    void saveEntry(const Entry &entry);
    QString className(int classId);
    void cellMenu(int classId, QDate day, QPoint pos, QMenu *standardMenu);
    void noSchoolMenu(QDate day, QPoint pos);
    bool confirm(const QString &text);
    std::optional<int> run(const std::function<int(Database &)> &op);
    void insertLesson(int classId, QDate day);
    void removeLesson(int classId, QDate day);
    void clearCell(int classId, QDate day);
    void markNoSchool(QDate day);
    void editNoSchoolReason(QDate day);
    void unmarkNoSchool(QDate day);

    // lock / read-only
    void setReadOnly(const std::optional<lock::LockInfo> &holder);
    void takeOver();
    void heartbeat();

    // external changes, backup, export
    void poll();
    void dailyBackup();
    void scheduleTabletExport();
    void exportTablet();
    void printWeek();
    void exportPdf();
    void openOptions();

    QString m_folder;
    std::unique_ptr<Database> m_db;
    lock::Lock &m_lock;
    bool m_readOnly = false;
    bool m_closed = false;
    QMap<QDate, WeekView *> m_views;
    QSet<QDate> m_extraWeeks;
    QList<QDate> m_tabWeeks;
    int m_dataVersion = 0;
    std::optional<FileId> m_dbFile;

    QFrame *m_banner = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QPushButton *m_takeOverButton = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_tabletTimer = nullptr;
    QTimer *m_backupTimer = nullptr;
};
