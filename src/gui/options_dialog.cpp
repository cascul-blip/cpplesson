#include "gui/options_dialog.h"

#include "core/config.h"
#include "core/database.h"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

const int ID_ROLE = Qt::UserRole;  // class id; unset for a class not saved yet

QListWidgetItem *classItem(const QString &name, const QVariant &id, bool active)
{
    auto *item = new QListWidgetItem(name);
    item->setData(ID_ROLE, id);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(active ? Qt::Checked : Qt::Unchecked);
    return item;
}

}  // namespace

OptionsDialog::OptionsDialog(Database &db, const QString &folder, bool readOnly, QWidget *parent)
    : QDialog(parent), m_db(db), m_readOnly(readOnly)
{
    setWindowTitle(QStringLiteral("Options"));
    resize(520, 560);
    auto *layout = new QVBoxLayout(this);

    // --- storage (per computer) ---
    auto *storage = new QGroupBox(QStringLiteral("Database folder (this computer)"));
    auto *row = new QHBoxLayout(storage);
    m_folderEdit = new QLineEdit(QDir::toNativeSeparators(folder));
    auto *browseButton = new QPushButton(QStringLiteral("Browse…"));
    connect(browseButton, &QPushButton::clicked, this, &OptionsDialog::browse);
    row->addWidget(m_folderEdit);
    row->addWidget(browseButton);
    layout->addWidget(storage);

    // --- shared settings (stored in the database) ---
    auto *shared = new QGroupBox(QStringLiteral("School year (shared)"));
    auto *form = new QFormLayout(shared);
    m_week1 = new QDateEdit;
    m_week1->setCalendarPopup(true);
    m_week1->setDisplayFormat(QStringLiteral("ddd MMM d, yyyy"));
    m_week1->setDate(db.week1Monday());
    form->addRow(QStringLiteral("Monday of Week 1:"), m_week1);
    m_retention = new QSpinBox;
    m_retention->setRange(1, 3650);
    m_retention->setSuffix(QStringLiteral(" days"));
    m_retention->setValue(db.backupRetentionDays());
    form->addRow(QStringLiteral("Keep daily backups for:"), m_retention);
    layout->addWidget(shared);

    auto *classes = new QGroupBox(QStringLiteral(
        "Classes (checked = active; unchecked classes are hidden, their lessons are kept)"));
    auto *box = new QHBoxLayout(classes);
    m_classList = new QListWidget;
    for (const ClassInfo &c : db.classes(true))
        m_classList->addItem(classItem(c.name, c.id, c.active));
    box->addWidget(m_classList);
    auto *buttons = new QVBoxLayout;
    const auto addButton = [&](const QString &text, auto slot) {
        auto *button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
    };
    addButton(QStringLiteral("Add…"), [this] { addClass(); });
    addButton(QStringLiteral("Rename…"), [this] { renameClass(); });
    addButton(QStringLiteral("Move up"), [this] { moveClass(-1); });
    addButton(QStringLiteral("Move down"), [this] { moveClass(1); });
    buttons->addStretch();
    box->addLayout(buttons);
    layout->addWidget(classes, 1);

    if (readOnly) {
        for (QWidget *w : {shared, classes}) {
            w->setEnabled(false);
            w->setToolTip(QStringLiteral("Read-only: another computer has the database open."));
        }
    }

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &OptionsDialog::save);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(bb);
}

void OptionsDialog::browse()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Database folder"), m_folderEdit->text());
    if (!folder.isEmpty())
        m_folderEdit->setText(QDir::toNativeSeparators(folder));
}

void OptionsDialog::addClass()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Add class"),
                                               QStringLiteral("Class name:"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (ok && !name.isEmpty())
        m_classList->addItem(classItem(name, QVariant(), true));
}

void OptionsDialog::renameClass()
{
    QListWidgetItem *item = m_classList->currentItem();
    if (!item)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename class"),
                                               QStringLiteral("Class name:"), QLineEdit::Normal,
                                               item->text(), &ok).trimmed();
    if (ok && !name.isEmpty())
        item->setText(name);
}

void OptionsDialog::moveClass(int delta)
{
    const int row = m_classList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_classList->count())
        return;
    QListWidgetItem *item = m_classList->takeItem(row);
    m_classList->insertItem(target, item);
    m_classList->setCurrentRow(target);
}

void OptionsDialog::save()
{
    const QString title = QStringLiteral("Options");
    QSet<QString> names;
    for (int i = 0; i < m_classList->count(); ++i)
        names.insert(m_classList->item(i)->text().trimmed().toLower());
    if (names.size() != m_classList->count()) {
        QMessageBox::warning(this, title, QStringLiteral("Two classes have the same name."));
        return;
    }
    const QDate week1 = m_week1->date();
    if (week1.dayOfWeek() != 1) {
        QMessageBox::warning(this, title, QStringLiteral("Week 1 must start on a Monday."));
        return;
    }

    const QString newFolder =
        QDir::cleanPath(config::expandUser(QDir::fromNativeSeparators(m_folderEdit->text())));
    if (newFolder != QDir::cleanPath(config::dbFolder().value_or(QString()))) {
        if (!QDir(newFolder).exists()) {
            QMessageBox::warning(this, title,
                                 QStringLiteral("Folder does not exist:\n%1")
                                     .arg(QDir::toNativeSeparators(newFolder)));
            return;
        }
        config::setDbFolder(newFolder);
        m_folderChanged = true;
    }

    if (!m_readOnly) {
        m_db.setSetting(QStringLiteral("week1_monday"), week1.toString(Qt::ISODate));
        m_db.setSetting(QStringLiteral("backup_retention_days"),
                        QString::number(m_retention->value()));
        for (int order = 0; order < m_classList->count(); ++order) {
            QListWidgetItem *item = m_classList->item(order);
            const QString name = item->text().trimmed();
            const QVariant id = item->data(ID_ROLE);
            const int classId = id.isValid() ? id.toInt() : m_db.addClass(name);
            item->setData(ID_ROLE, classId);  // so a failed save does not add it twice
            m_db.updateClass(classId, name, order, item->checkState() == Qt::Checked);
        }
    }
    accept();
}

std::optional<QString> chooseInitialFolder(QWidget *parent)
{
    QMessageBox::information(
        parent, QStringLiteral("Lesson Planner"),
        QStringLiteral("Choose the folder that holds (or will hold) the lesson database.\n\n"
                       "Pick a folder inside Nextcloud so it syncs between computers. "
                       "Daily backups are saved in the same folder."));
    const QString folder = QFileDialog::getExistingDirectory(
        parent, QStringLiteral("Database folder"), QDir::homePath());
    return folder.isEmpty() ? std::nullopt : std::optional<QString>(folder);
}

MissingDbChoice askAboutMissingDatabase(const QString &folder, QWidget *parent)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("Lesson Planner"),
                    QStringLiteral("No lesson database in %1.\n\n"
                                   "Create a new one here, or choose the folder that already has "
                                   "lessonplanner.db. If this folder syncs from another computer, "
                                   "wait until Nextcloud has finished.")
                        .arg(QDir::toNativeSeparators(folder)),
                    QMessageBox::NoButton, parent);
    auto *create = box.addButton(QStringLiteral("Create a new one"), QMessageBox::AcceptRole);
    auto *choose = box.addButton(QStringLiteral("Choose another folder"), QMessageBox::ActionRole);
    auto *quit = box.addButton(QStringLiteral("Quit"), QMessageBox::RejectRole);
    box.setDefaultButton(quit);
    box.setEscapeButton(quit);
    box.exec();
    if (box.clickedButton() == create)
        return MissingDbChoice::Create;
    if (box.clickedButton() == choose)
        return MissingDbChoice::ChooseFolder;
    return MissingDbChoice::Quit;
}
