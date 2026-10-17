// Drives the real window: typing, autosave, the right-click menus that rebuild
// the grid underneath themselves, and closing. Run with QT_QPA_PLATFORM=offscreen.

#include "core/config.h"
#include "core/database.h"
#include "core/lock.h"
#include "core/schoolcal.h"
#include "gui/cell_widget.h"
#include "gui/main_window.h"
#include "gui/options_dialog.h"
#include "gui/week_view.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

// Picks the entry starting with `prefix` from the popup menu that is about to open.
void chooseFromNextMenu(const QString &prefix, bool *found)
{
    QTimer::singleShot(300, qApp, [prefix, found] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        for (QAction *action : menu->actions()) {
            if (action->text().startsWith(prefix) && action->isEnabled()) {
                *found = true;
                action->trigger();
                break;
            }
        }
        menu->close();
    });
}

// Answers the modal dialog that is about to open: types `text` into an input
// dialog, or presses Yes on a question.
void answerNextDialog(const QString &text = {})
{
    QTimer::singleShot(900, qApp, [text] {
        QWidget *dialog = QApplication::activeModalWidget();
        if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
            input->setTextValue(text);
            input->accept();
        } else if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
            box->button(QMessageBox::Yes)->click();
        }
    });
}

// Clicks a button on the modal message box that is about to open.
void clickNextMessageButton(const QString &text)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton *button : box->buttons()) {
            if (button->text() == text) {
                timer->stop();
                timer->deleteLater();
                button->click();
                return;
            }
        }
    });
    timer->start();
}

void rightClick(QWidget *widget)
{
    const QPoint pos(5, 5);
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, widget->mapToGlobal(pos));
    QApplication::sendEvent(widget, &event);
}

}  // namespace

class TestGui : public QObject
{
    Q_OBJECT

    const QDate monday = schoolcal::currentMonday();
    const QDate tuesday = monday.addDays(1);

    WeekView *thisWeek(MainWindow &window)
    {
        for (WeekView *view : window.findChildren<WeekView *>()) {
            if (view->monday() == monday)
                return view;
        }
        return nullptr;
    }

private slots:
    void editShiftAndClose()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        lock::Lock fileLock(config::lockPath(folder));
        QVERIFY(!fileLock.acquire().has_value());
        MainWindow window(folder,
                          std::make_unique<Database>(config::dbPath(folder), DbOpen::Create),
                          fileLock,
                          std::nullopt);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Database check(config::dbPath(folder));  // a second connection, like another program

        // typing autosaves
        WeekView *view = thisWeek(window);
        QVERIFY(view && view->loaded());
        CellWidget *cell = view->cell(1, monday);
        QVERIFY(cell);
        auto *lessonEdit = cell->findChildren<FieldEdit *>().first();
        lessonEdit->setFocus();
        QTest::keyClicks(lessonEdit, QStringLiteral("Lesson A"));
        QTRY_COMPARE_WITH_TIMEOUT(check.entry(1, monday).lesson, QStringLiteral("Lesson A"), 3000);

        // Insert lesson here: Monday's lesson moves to Tuesday
        bool found = false;
        chooseFromNextMenu(QStringLiteral("Insert lesson here"), &found);
        rightClick(lessonEdit->viewport());
        QVERIFY(found);
        QTRY_COMPARE_WITH_TIMEOUT(check.entry(1, tuesday).lesson, QStringLiteral("Lesson A"), 3000);
        QVERIFY(check.entry(1, monday).isEmpty());
        QTRY_COMPARE(view->cell(1, tuesday)->entry().lesson, QStringLiteral("Lesson A"));
        QCOMPARE(view->cell(1, monday)->entry().lesson, QString());

        // Mark Monday as No School from a cell that the rebuild then destroys
        found = false;
        chooseFromNextMenu(QStringLiteral("Mark Monday as No School"), &found);
        answerNextDialog(QStringLiteral("Snow day"));
        rightClick(view->cell(1, monday)->findChildren<FieldEdit *>().first()->viewport());
        QVERIFY(found);
        QTRY_COMPARE_WITH_TIMEOUT(check.noSchoolDays().value(monday), QStringLiteral("Snow day"),
                                  5000);
        QTRY_VERIFY(view->findChild<NoSchoolBanner *>());
        QCOMPARE(view->cell(1, monday), nullptr);
        // everything from Monday on moved a day later
        QCOMPARE(check.entry(1, tuesday.addDays(1)).lesson, QStringLiteral("Lesson A"));
        QTest::qWait(200);  // let the old grid's deferred deletion run

        // Remove No School from the banner, which the rebuild then destroys
        found = false;
        chooseFromNextMenu(QStringLiteral("Remove No School"), &found);
        answerNextDialog();
        rightClick(view->findChild<NoSchoolBanner *>());
        QVERIFY(found);
        QTRY_VERIFY_WITH_TIMEOUT(check.noSchoolDays().isEmpty(), 5000);
        QTRY_VERIFY(view->cell(1, monday));
        QTRY_COMPARE(view->cell(1, tuesday)->entry().lesson, QStringLiteral("Lesson A"));
        QCOMPARE(check.entry(1, tuesday).lesson, QStringLiteral("Lesson A"));
        QTest::qWait(200);

        // a change from outside shows up without a reload
        check.saveEntry({2, monday, QStringLiteral("From elsewhere"), {}, {}});
        QTRY_COMPARE_WITH_TIMEOUT(view->cell(2, monday)->entry().lesson,
                                  QStringLiteral("From elsewhere"), 5000);

        // closing writes the tablet PDF and gives up the lock
        QVERIFY(QFile::exists(config::lockPath(folder)));
        window.close();
        QVERIFY(!QFile::exists(config::lockPath(folder)));
        const QStringList pdfs =
            QDir(QDir(folder).filePath(QStringLiteral("Tablet"))).entryList({"*.pdf"});
        QCOMPARE(pdfs.size(), 1);
        QVERIFY(QFileInfo(QDir(folder).filePath("Tablet/" + pdfs.first())).size() > 1000);
        QCOMPARE(QDir(folder).entryList({"lessons_*.db"}).size(), 1);  // today's backup
    }

    void opensReadOnlyWhenAnotherComputerHoldsTheLock()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        const lock::LockInfo other{QStringLiteral("other-pc"), QStringLiteral("x"), 1, {}, {}};
        lock::Lock fileLock(config::lockPath(folder));
        MainWindow window(folder,
                          std::make_unique<Database>(config::dbPath(folder), DbOpen::Create),
                          fileLock,
                          other);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Database check(config::dbPath(folder));

        WeekView *view = thisWeek(window);
        QVERIFY(view && view->cell(1, monday));
        auto *lessonEdit = view->cell(1, monday)->findChildren<FieldEdit *>().first();
        QVERIFY(lessonEdit->isReadOnly());
        lessonEdit->setFocus();
        QTest::keyClicks(lessonEdit, QStringLiteral("nope"));
        QTest::qWait(1500);
        QVERIFY(check.entry(1, monday).isEmpty());

        bool found = false;
        chooseFromNextMenu(QStringLiteral("Insert lesson here"), &found);
        rightClick(lessonEdit->viewport());
        QVERIFY(!found);  // the shifting actions are disabled

        window.close();
        QVERIFY(!QDir(folder).exists(QStringLiteral("Tablet")));  // read-only writes nothing
        QVERIFY(QDir(folder).entryList({"lessons_*.db"}).isEmpty());
    }

    void failedSaveStaysDirtyUntilItSucceeds()
    {
        CellWidget cell(1, monday);
        enum class Mode { Throw, Ignore, Save } mode = Mode::Throw;
        int saves = 0;
        QString seen;
        connect(&cell, &CellWidget::changed, &cell, [&](const Entry &entry, bool *saved) {
            seen = entry.lesson;
            if (mode == Mode::Throw)
                throw DbError("disk full");
            if (mode == Mode::Ignore)
                return;
            ++saves;
            if (saved)
                *saved = true;
        });
        auto *lesson = cell.findChildren<FieldEdit *>().first();
        lesson->setPlainText(QStringLiteral("keep me"));
        QVERIFY(cell.isDirty());
        QVERIFY_THROWS_EXCEPTION(DbError, cell.flush());
        QCOMPARE(seen, QStringLiteral("keep me"));
        QVERIFY(cell.isDirty());
        cell.setEntry(Entry{1, monday, QStringLiteral("from the database"), {}, {}});
        QCOMPARE(cell.entry().lesson, QStringLiteral("keep me"));

        // A receiver that does not store the entry must not clear the edit either.
        mode = Mode::Ignore;
        cell.flush();
        QVERIFY(cell.isDirty());

        mode = Mode::Save;
        cell.flush();
        QVERIFY(!cell.isDirty());
        QCOMPARE(saves, 1);
        cell.setEntry(Entry{1, monday, QStringLiteral("from the database"), {}, {}});
        QCOMPARE(cell.entry().lesson, QStringLiteral("from the database"));
    }

    void reloadSavesTheLastKeystrokes()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        lock::Lock fileLock(config::lockPath(folder));
        QVERIFY(!fileLock.acquire().has_value());
        MainWindow window(folder,
                          std::make_unique<Database>(config::dbPath(folder), DbOpen::Create),
                          fileLock,
                          std::nullopt);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Database check(config::dbPath(folder));

        WeekView *view = thisWeek(window);
        CellWidget *cell = view->cell(1, monday);
        auto *lessonEdit = cell->findChildren<FieldEdit *>().first();
        lessonEdit->setFocus();
        QTest::keyClicks(lessonEdit, QStringLiteral("just typed"));
        QVERIFY(cell->isDirty());
        QVERIFY(check.entry(1, monday).isEmpty());

        QAction *reload = nullptr;
        for (QAction *action : window.findChildren<QAction *>()) {
            if (action->shortcut() == QKeySequence(QKeySequence::Refresh))
                reload = action;
        }
        QVERIFY(reload);
        reload->trigger();
        QTRY_COMPARE(check.entry(1, monday).lesson, QStringLiteral("just typed"));
        QVERIFY(!cell->isDirty());
        window.close();
    }

    void missingDatabaseDialog()
    {
        const QString folder = QStringLiteral("/tmp/lessonplanner-missing");
        clickNextMessageButton(QStringLiteral("Quit"));
        QCOMPARE(askAboutMissingDatabase(folder), MissingDbChoice::Quit);
        clickNextMessageButton(QStringLiteral("Create a new one"));
        QCOMPARE(askAboutMissingDatabase(folder), MissingDbChoice::Create);
        clickNextMessageButton(QStringLiteral("Choose another folder"));
        QCOMPARE(askAboutMissingDatabase(folder), MissingDbChoice::ChooseFolder);

        QString defaultText;
        auto *timer = new QTimer(this);
        timer->setInterval(10);
        connect(timer, &QTimer::timeout, this, [timer, &defaultText] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            timer->stop();
            timer->deleteLater();
            if (box->defaultButton())
                defaultText = box->defaultButton()->text();
            QTest::keyClick(box, Qt::Key_Escape);
        });
        timer->start();
        QCOMPARE(askAboutMissingDatabase(folder), MissingDbChoice::Quit);
        QCOMPARE(defaultText, QStringLiteral("Quit"));
    }
};

QTEST_MAIN(TestGui)
#include "test_gui.moc"
