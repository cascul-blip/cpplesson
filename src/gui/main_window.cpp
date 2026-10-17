#include "gui/main_window.h"

#include "core/backup.h"
#include "core/config.h"
#include "core/render.h"
#include "core/schedule.h"
#include "core/schoolcal.h"
#include "gui/cell_widget.h"
#include "gui/export.h"
#include "gui/options_dialog.h"
#include "gui/week_view.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const int POLL_MS = 3000;
const int TABLET_EXPORT_DELAY_MS = 30'000;
const QString TITLE = QStringLiteral("Lesson Planner");

QString shortDay(QDate day)  // Mon Oct 5
{
    return schoolcal::format(day, QStringLiteral("ddd MMM d"));
}

QString longDay(QDate day)  // Monday Oct 5
{
    return schoolcal::format(day, QStringLiteral("dddd MMM d"));
}

}  // namespace

MainWindow::MainWindow(const QString &folder, std::unique_ptr<Database> db, lock::Lock &fileLock,
                       const std::optional<lock::LockInfo> &holder)
    : m_folder(folder), m_db(std::move(db)), m_lock(fileLock)
{
    m_dataVersion = m_db->dataVersion();
    m_dbFile = fileIdentity(m_db->path());

    setWindowTitle(TITLE);
    resize(1400, 900);
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildBanner());
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    auto *addWeek = new QToolButton;
    addWeek->setText(QStringLiteral("+"));
    addWeek->setToolTip(QStringLiteral("Add the next week"));
    connect(addWeek, &QToolButton::clicked, this, &MainWindow::addNextWeek);
    m_tabs->setCornerWidget(addWeek, Qt::TopRightCorner);
    layout->addWidget(m_tabs, 1);
    setCentralWidget(central);
    buildMenus();
    statusBar();

    rebuildTabs(schoolcal::currentMonday());
    setReadOnly(holder);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::poll);
    m_pollTimer->start();
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(lock::HEARTBEAT_SECONDS * 1000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MainWindow::heartbeat);
    m_heartbeatTimer->start();
    m_tabletTimer = new QTimer(this);
    m_tabletTimer->setSingleShot(true);
    m_tabletTimer->setInterval(TABLET_EXPORT_DELAY_MS);
    connect(m_tabletTimer, &QTimer::timeout, this, &MainWindow::exportTablet);
    m_backupTimer = new QTimer(this);
    m_backupTimer->setSingleShot(true);
    connect(m_backupTimer, &QTimer::timeout, this, &MainWindow::dailyBackup);
    dailyBackup();
    scheduleTabletExport();
}

MainWindow::~MainWindow() = default;

// --- layout ------------------------------------------------------------------

QWidget *MainWindow::buildBanner()
{
    m_banner = new QFrame;
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame { background: #f7d774; } QLabel { color: #222; }"
        " QPushButton { color: #222; background: #fff3c4; border: 1px solid #b08d1a;"
        " padding: 3px 10px; border-radius: 3px; }"));
    auto *row = new QHBoxLayout(m_banner);
    row->setContentsMargins(10, 6, 10, 6);
    m_bannerLabel = new QLabel;
    m_bannerLabel->setWordWrap(true);
    m_takeOverButton = new QPushButton(QStringLiteral("Take over anyway…"));
    connect(m_takeOverButton, &QPushButton::clicked, this, &MainWindow::takeOver);
    row->addWidget(m_bannerLabel, 1);
    row->addWidget(m_takeOverButton);
    m_banner->hide();
    return m_banner;
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Print week…"), QKeySequence(QKeySequence::Print), this,
                        &MainWindow::printWeek);
    fileMenu->addAction(QStringLiteral("&Export week to PDF…"), QKeySequence(QStringLiteral("Ctrl+E")),
                        this, &MainWindow::exportPdf);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Options…"), QKeySequence(QStringLiteral("Ctrl+,")), this,
                        &MainWindow::openOptions);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Quit"), QKeySequence(QKeySequence::Quit), this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("&This week"), QKeySequence(QStringLiteral("Ctrl+T")), this,
                        &MainWindow::goToCurrentWeek);
    viewMenu->addAction(QStringLiteral("&Add next week"), QKeySequence(QStringLiteral("Ctrl+N")),
                        this, &MainWindow::addNextWeek);
    viewMenu->addAction(QStringLiteral("&Reload"), QKeySequence(QKeySequence::Refresh), this,
                        [this] {
                            flushAll();
                            refreshAll(true);
                        });
}

// --- weeks / tabs ------------------------------------------------------------

QList<QDate> MainWindow::weekList()
{
    QSet<QDate> mondays = m_extraWeeks;
    mondays.insert(schoolcal::currentMonday());
    for (QDate day : m_db->datesWithEntries())
        mondays.insert(schoolcal::mondayOf(day));
    QList<QDate> weeks(mondays.cbegin(), mondays.cend());
    std::sort(weeks.begin(), weeks.end(), std::greater<>());  // newest on the left
    return weeks;
}

WeekView *MainWindow::currentView() const
{
    return qobject_cast<WeekView *>(m_tabs->currentWidget());
}

WeekView *MainWindow::view(QDate monday)
{
    WeekView *&weekView = m_views[monday];
    if (!weekView) {
        weekView = new WeekView(m_db.get(), monday, this);
        weekView->setReadOnly(m_readOnly);
        connect(weekView, &WeekView::entryChanged, this, &MainWindow::saveEntry);
        connect(weekView, &WeekView::cellMenuRequested, this, &MainWindow::cellMenu);
        connect(weekView, &WeekView::noSchoolMenuRequested, this, &MainWindow::noSchoolMenu);
    }
    return weekView;
}

QString MainWindow::tabText(QDate monday)
{
    const QString text = schoolcal::weekLabel(monday, m_db->week1Monday());
    return monday == schoolcal::currentMonday() ? text + QStringLiteral("  (this week)") : text;
}

void MainWindow::rebuildTabs(QDate select)
{
    if (!select.isValid()) {
        const WeekView *current = currentView();
        select = current ? current->monday() : schoolcal::currentMonday();
    }
    const QList<QDate> weeks = weekList();
    if (weeks != m_tabWeeks) {
        m_tabs->blockSignals(true);
        m_tabs->clear();
        for (QDate monday : weeks)
            m_tabs->addTab(view(monday), tabText(monday));
        m_tabs->blockSignals(false);
        m_tabWeeks = weeks;
    } else {
        for (int i = 0; i < weeks.size(); ++i)
            m_tabs->setTabText(i, tabText(weeks.at(i)));
    }
    const qsizetype index = weeks.indexOf(select);
    if (index >= 0)
        m_tabs->setCurrentIndex(int(index));
}

void MainWindow::goToCurrentWeek()
{
    rebuildTabs(schoolcal::currentMonday());
}

void MainWindow::addNextWeek()
{
    const QDate next = weekList().first().addDays(7);
    m_extraWeeks.insert(next);
    rebuildTabs(next);
}

void MainWindow::flushAll()
{
    for (WeekView *weekView : std::as_const(m_views))
        weekView->flush();
}

bool MainWindow::hasUnsaved() const
{
    for (WeekView *weekView : std::as_const(m_views)) {
        if (weekView->hasUnsaved())
            return true;
    }
    return false;
}

void MainWindow::refreshAll(bool force)
{
    for (WeekView *weekView : std::as_const(m_views)) {
        if (weekView->loaded())
            weekView->reload(force);
    }
    rebuildTabs();
}

void MainWindow::afterChange(const QString &message)
{
    refreshAll(true);  // pending edits were flushed before the change
    m_dataVersion = m_db->dataVersion();
    scheduleTabletExport();
    if (!message.isEmpty())
        statusBar()->showMessage(message, 6000);
}

// --- editing -----------------------------------------------------------------

void MainWindow::saveEntry(const Entry &entry, bool *saved)
{
    if (saved)
        *saved = false;
    if (m_readOnly || m_closed)
        return;
    m_db->saveEntry(entry);
    if (saved)
        *saved = true;
    m_dataVersion = m_db->dataVersion();
    if (!m_tabWeeks.contains(schoolcal::mondayOf(entry.date)))
        rebuildTabs();
    scheduleTabletExport();
    statusBar()->showMessage(QStringLiteral("Saved"), 1500);
}

QString MainWindow::className(int classId)
{
    for (const ClassInfo &c : m_db->classes(true)) {
        if (c.id == classId)
            return c.name;
    }
    return QStringLiteral("?");
}

// The two right-click menus only note what was chosen and run it from the
// event loop afterwards: the choice can rebuild the grid, and the widget that
// was clicked must not be torn down while its own event is being handled.

void MainWindow::cellMenu(int classId, QDate day, QPoint pos, QMenu *standardMenu)
{
    // the editor owns its standard menu and may be deleted while ours is open
    const QPointer<QMenu> standard(standardMenu);
    std::function<void()> chosen;
    QMenu menu(this);
    menu.addAction(QStringLiteral("%1 — %2").arg(className(classId), shortDay(day)))
        ->setEnabled(false);
    menu.addSeparator();
    const auto add = [&](const QString &text, std::function<void()> op) {
        menu.addAction(text, this, [&chosen, op] { chosen = op; })->setEnabled(!m_readOnly);
    };
    add(QStringLiteral("Insert lesson here (push later lessons forward)"),
        [=, this] { insertLesson(classId, day); });
    add(QStringLiteral("Remove this lesson (pull later lessons back)"),
        [=, this] { removeLesson(classId, day); });
    add(QStringLiteral("Clear this cell"), [=, this] { clearCell(classId, day); });
    menu.addSeparator();
    add(QStringLiteral("Mark %1 as No School (all classes)…")
            .arg(schoolcal::format(day, QStringLiteral("dddd"))),
        [=, this] { markNoSchool(day); });
    menu.addSeparator();
    if (standard)
        menu.addActions(standard->actions());
    menu.exec(pos);
    if (standard)
        standard->deleteLater();
    if (chosen)
        QTimer::singleShot(0, this, chosen);
}

void MainWindow::noSchoolMenu(QDate day, QPoint pos)
{
    std::function<void()> chosen;
    QMenu menu(this);
    const auto add = [&](const QString &text, std::function<void()> op) {
        menu.addAction(text, this, [&chosen, op] { chosen = op; })->setEnabled(!m_readOnly);
    };
    add(QStringLiteral("Edit reason…"), [=, this] { editNoSchoolReason(day); });
    add(QStringLiteral("Remove No School (pull all classes back a day)"),
        [=, this] { unmarkNoSchool(day); });
    menu.exec(pos);
    if (chosen)
        QTimer::singleShot(0, this, chosen);
}

bool MainWindow::confirm(const QString &text)
{
    return QMessageBox::question(this, TITLE, text) == QMessageBox::Yes;
}

std::optional<int> MainWindow::run(const std::function<int(Database &)> &op)
{
    if (m_readOnly || m_closed)
        return std::nullopt;
    flushAll();
    try {
        return op(*m_db);
    } catch (const schedule::ScheduleError &error) {
        QMessageBox::warning(this, TITLE, QString::fromUtf8(error.what()));
        return std::nullopt;
    }
}

void MainWindow::insertLesson(int classId, QDate day)
{
    const auto moved = run([&](Database &db) { return schedule::insertLesson(db, classId, day); });
    if (!moved)
        return;
    afterChange(QStringLiteral("%1: moved %2 lesson(s) forward").arg(className(classId)).arg(*moved));
    const WeekView *weekView = m_views.value(schoolcal::mondayOf(day));
    if (CellWidget *cell = weekView ? weekView->cell(classId, day) : nullptr)
        cell->focusLesson();
}

void MainWindow::removeLesson(int classId, QDate day)
{
    if (m_readOnly || m_closed)
        return;
    flushAll();
    const Entry entry = m_db->entry(classId, day);
    if (!entry.isEmpty()) {
        QString first;
        for (const QString &text : {entry.lesson, entry.special, entry.homework}) {
            first = text.trimmed().section(QLatin1Char('\n'), 0, 0);
            if (!first.isEmpty())
                break;
        }
        if (!confirm(QStringLiteral("Remove “%1” from %2 on %3 and pull its later lessons back "
                                    "one school day?")
                         .arg(first, className(classId), shortDay(day))))
            return;
    }
    const auto moved = run([&](Database &db) { return schedule::removeLesson(db, classId, day); });
    if (moved)
        afterChange(QStringLiteral("%1: moved %2 lesson(s) back").arg(className(classId)).arg(*moved));
}

void MainWindow::clearCell(int classId, QDate day)
{
    if (m_readOnly || m_closed)
        return;
    flushAll();
    if (m_db->entry(classId, day).isEmpty())
        return;
    if (confirm(QStringLiteral("Clear %1 on %2? Other days are not moved.")
                    .arg(className(classId), shortDay(day)))) {
        m_db->saveEntry(Entry{classId, day, {}, {}, {}});
        afterChange(QStringLiteral("Cleared"));
    }
}

void MainWindow::markNoSchool(QDate day)
{
    bool ok = false;
    const QString reason = QInputDialog::getText(
        this, QStringLiteral("No School"),
        QStringLiteral("No school on %1.\nEvery class's lessons from this day on will move one "
                       "school day later.\n\nReason:").arg(longDay(day)),
        QLineEdit::Normal, QStringLiteral("No school"), &ok).trimmed();
    if (!ok)
        return;
    const auto moved = run([&](Database &db) { return schedule::markNoSchool(db, day, reason); });
    if (moved) {
        afterChange(QStringLiteral("No school %1: moved %2 lesson(s)").arg(shortDay(day)).arg(*moved));
    }
}

void MainWindow::editNoSchoolReason(QDate day)
{
    bool ok = false;
    const QString reason = QInputDialog::getText(
        this, QStringLiteral("No School"), QStringLiteral("Reason:"), QLineEdit::Normal,
        m_db->noSchoolDays().value(day), &ok).trimmed();
    if (ok && run([&](Database &db) { return schedule::markNoSchool(db, day, reason); }))
        afterChange();
}

void MainWindow::unmarkNoSchool(QDate day)
{
    if (!confirm(QStringLiteral("Hold school on %1 after all? Every class's later lessons move "
                                "one school day earlier.").arg(longDay(day))))
        return;
    const auto moved = run([&](Database &db) { return schedule::unmarkNoSchool(db, day); });
    if (moved)
        afterChange(QStringLiteral("Moved %1 lesson(s) back").arg(*moved));
}

// --- lock / read-only --------------------------------------------------------

void MainWindow::setReadOnly(const std::optional<lock::LockInfo> &holder)
{
    m_readOnly = holder.has_value();
    for (WeekView *weekView : std::as_const(m_views))
        weekView->setReadOnly(m_readOnly);
    if (!holder) {
        m_banner->hide();
        return;
    }
    m_bannerLabel->setText(
        QStringLiteral("<b>Read-only.</b> The lesson database is open on another computer: %1. "
                       "Close it there to edit here.").arg(holder->describe().toHtmlEscaped()));
    m_banner->show();
}

void MainWindow::takeOver()
{
    if (confirm(QStringLiteral(
            "Take over the database? Only do this if the other computer is not actually using "
            "Lesson Planner (for example it crashed or was left open and asleep)."))) {
        m_lock.acquire(true);
        setReadOnly(std::nullopt);
        dailyBackup();
    }
}

void MainWindow::heartbeat()
{
    if (m_closed)
        return;
    try {
        if (m_lock.owned()) {
            if (const auto holder = m_lock.heartbeat()) {
                flushAll();
                setReadOnly(holder);
            }
            return;
        }
        const auto holder = m_lock.acquire();  // succeeds once the other computer lets go
        setReadOnly(holder);
        if (!holder) {
            statusBar()->showMessage(
                QStringLiteral("The other computer closed the planner; editing is enabled."), 10000);
            dailyBackup();
        }
    } catch (const std::exception &error) {  // e.g. the folder is briefly unavailable
        statusBar()->showMessage(QString::fromUtf8(error.what()), 10000);
    }
}

// --- external changes --------------------------------------------------------

void MainWindow::poll()
{
    if (m_closed)
        return;
    const std::optional<FileId> file = fileIdentity(m_db->path());
    if (file && file != m_dbFile) {
        // the sync client swapped in a new file; reopen it, never creating one
        flushAll();
        std::unique_ptr<Database> fresh;
        try {
            fresh = std::make_unique<Database>(m_db->path(), DbOpen::Existing);
        } catch (const DbError &) {
            return;  // probably still being written; look again on the next tick
        }
        m_db = std::move(fresh);
        for (WeekView *weekView : std::as_const(m_views))
            weekView->setDatabase(m_db.get());
        m_dbFile = file;
        m_dataVersion = -1;
    }
    const int version = m_db->dataVersion();
    if (version != m_dataVersion) {
        m_dataVersion = version;
        refreshAll();
        scheduleTabletExport();
    }
}

// --- backup / export ---------------------------------------------------------

void MainWindow::dailyBackup()
{
    if (m_closed)
        return;
    if (m_lock.owned()) {
        try {
            const auto created =
                backup::runDailyBackup(*m_db, m_folder, m_db->backupRetentionDays());
            if (created) {
                statusBar()->showMessage(
                    QStringLiteral("Backed up to %1").arg(QFileInfo(*created).fileName()), 5000);
            }
        } catch (const std::exception &error) {  // a failed backup must not stop the app
            statusBar()->showMessage(
                QStringLiteral("Backup failed: %1").arg(QString::fromUtf8(error.what())), 10000);
        }
    }
    m_backupTimer->start(backup::secondsUntilMidnight() * 1000);
}

void MainWindow::scheduleTabletExport()
{
    m_tabletTimer->start();
}

void MainWindow::exportTablet()
{
    if (m_closed || !m_lock.owned())
        return;
    try {
        const QStringList written = exporter::exportTablet(*m_db, m_folder);
        if (!written.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Updated %1 tablet PDF(s) in %2/")
                                         .arg(written.size()).arg(exporter::TABLET_FOLDER), 4000);
        }
    } catch (const std::exception &error) {
        statusBar()->showMessage(
            QStringLiteral("Tablet export failed: %1").arg(QString::fromUtf8(error.what())), 10000);
    }
}

void MainWindow::printWeek()
{
    const WeekView *weekView = currentView();
    if (!weekView)
        return;
    flushAll();
    QPrinter printer(QPrinter::HighResolution);
    exporter::setupPrinter(printer);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    try {
        exporter::paintHtml(render::weekHtml(*m_db, weekView->monday()), printer);
    } catch (const std::exception &error) {
        QMessageBox::warning(this, TITLE, QString::fromUtf8(error.what()));
    }
}

void MainWindow::exportPdf()
{
    const WeekView *weekView = currentView();
    if (!weekView)
        return;
    flushAll();
    const QDate monday = weekView->monday();
    const QString suggested =
        QDir::home().filePath(exporter::tabletPdfName(monday, m_db->week1Monday()));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export week to PDF"), suggested, QStringLiteral("PDF files (*.pdf)"));
    if (path.isEmpty())
        return;
    try {
        exporter::exportWeekPdf(*m_db, monday, path);
        statusBar()->showMessage(
            QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(path)), 5000);
    } catch (const std::exception &error) {
        QMessageBox::warning(this, TITLE, QString::fromUtf8(error.what()));
    }
}

void MainWindow::openOptions()
{
    flushAll();
    // the dialog works on the open database, so it must not be swapped meanwhile
    m_pollTimer->stop();
    OptionsDialog dialog(*m_db, m_folder, m_readOnly, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (m_closed)
        return;
    m_pollTimer->start();
    if (accepted) {
        afterChange();
        if (dialog.folderChanged()) {
            QMessageBox::information(
                this, QStringLiteral("Options"),
                QStringLiteral("Restart Lesson Planner to use the new folder:\n%1")
                    .arg(QDir::toNativeSeparators(config::dbFolder().value_or(QString()))));
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_closed) {
        for (;;) {
            QString failure;
            try {
                flushAll();
            } catch (const std::exception &error) {
                failure = QString::fromUtf8(error.what());
            }
            if (failure.isEmpty() && !hasUnsaved())
                break;
            const QString text = failure.isEmpty()
                ? QStringLiteral("The last edit was not saved. Quit anyway and lose it?")
                : QStringLiteral("The last edit could not be saved:\n%1\n\nQuit anyway and lose it?")
                      .arg(failure);
            QMessageBox box(QMessageBox::Warning, TITLE, text, QMessageBox::NoButton, this);
            auto *retry = box.addButton(QStringLiteral("Retry"), QMessageBox::AcceptRole);
            auto *quit = box.addButton(QStringLiteral("Quit anyway"), QMessageBox::DestructiveRole);
            auto *cancel = box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(retry);
            box.exec();
            if (box.clickedButton() == cancel) {
                event->ignore();
                return;
            }
            if (box.clickedButton() == quit)
                break;
        }
        for (QTimer *timer : {m_pollTimer, m_heartbeatTimer, m_backupTimer})
            timer->stop();
        hide();  // the tablet export below can take a few seconds
        if (m_tabletTimer->isActive()) {
            m_tabletTimer->stop();
            exportTablet();
        }
        m_closed = true;  // a timer that was already due can still fire once
        m_lock.release();
        m_db->close();
    }
    QMainWindow::closeEvent(event);
}
