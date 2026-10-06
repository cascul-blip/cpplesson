// Application entry point: choose folder -> lock -> open database -> window.

#include "core/config.h"
#include "core/database.h"
#include "core/lock.h"
#include "gui/main_window.h"
#include "gui/options_dialog.h"
#include "gui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <exception>

namespace {

const QString TITLE = QStringLiteral("Lesson Planner");
const int SMOKE_TEST_MS = 3000;

// An error escaping a menu action or timer (a full disk, a database another
// program has locked) is reported and the app carries on, instead of the
// exception unwinding out of the event loop and ending the program.
class Application : public QApplication
{
public:
    using QApplication::QApplication;

    bool quiet = false;   // report to stderr only
    bool failed = false;  // something was reported

    bool notify(QObject *receiver, QEvent *event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &error) {
            report(QString::fromUtf8(error.what()));
        } catch (...) {
            report(QStringLiteral("Unknown error"));
        }
        return false;
    }

private:
    void report(const QString &message)
    {
        std::fprintf(stderr, "Lesson Planner: %s\n", qPrintable(message));
        failed = true;
        if (quiet || m_reporting)  // one box at a time, even if a timer keeps failing
            return;
        m_reporting = true;
        QMessageBox::warning(activeWindow(), TITLE,
                             QStringLiteral("Something went wrong:\n%1").arg(message));
        m_reporting = false;
    }

    bool m_reporting = false;
};

}  // namespace

int main(int argc, char *argv[])
{
    Application app(argc, argv);
    app.setApplicationName(config::APP_NAME);
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setDesktopFileName(QStringLiteral("lessonplanner"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
    theme::followSystem(app);

    // --smoke-test: run against a throwaway folder for a few seconds, then
    // close and report whether the database was created. Used by the CI build.
    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));
    QTemporaryDir smokeDir;
    app.quiet = smokeTest;

    QString folder;
    if (smokeTest) {
        folder = smokeDir.path();
    } else {
        folder = config::dbFolder().value_or(QString());
        if (folder.isEmpty() || !QDir(folder).exists()) {
            const std::optional<QString> chosen = chooseInitialFolder();
            if (!chosen)
                return 0;
            folder = *chosen;
            config::setDbFolder(folder);
        }
    }

    lock::Lock fileLock(config::lockPath(folder));
    std::optional<lock::LockInfo> holder;
    std::unique_ptr<Database> db;
    try {
        holder = fileLock.acquire();
        db = std::make_unique<Database>(config::dbPath(folder));
    } catch (const std::exception &error) {
        fileLock.release();
        std::fprintf(stderr, "Lesson Planner: %s\n", error.what());
        if (!smokeTest) {
            QMessageBox::critical(nullptr, TITLE,
                                  QStringLiteral("Could not open the database:\n%1")
                                      .arg(QString::fromUtf8(error.what())));
        }
        return 1;
    }

    int result = 0;
    {
        MainWindow window(folder, std::move(db), fileLock, holder);
        window.show();
        if (smokeTest)
            QTimer::singleShot(SMOKE_TEST_MS, &window, &QWidget::close);
        result = app.exec();
    }
    fileLock.release();

    if (smokeTest) {
        if (app.failed || !QFile::exists(config::dbPath(folder))) {
            std::fprintf(stderr, "Lesson Planner: smoke test failed\n");
            return 1;
        }
        std::printf("OK: smoke test passed\n");
    }
    return result;
}
