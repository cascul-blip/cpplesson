#include "gui/week_view.h"

#include "core/schoolcal.h"
#include "gui/cell_widget.h"

#include <QContextMenuEvent>
#include <QGridLayout>

namespace {

const int MIN_COLUMN_WIDTH = 170;

}  // namespace

NoSchoolBanner::NoSchoolBanner(QDate day, const QString &reason, QWidget *parent)
    : QLabel(QStringLiteral("No School — %1")
                 .arg(reason.isEmpty() ? QStringLiteral("no reason given") : reason),
             parent),
      m_date(day)
{
    setTextFormat(Qt::PlainText);
    setAlignment(Qt::AlignCenter);
    setMinimumHeight(48);
    setStyleSheet(QStringLiteral("QLabel { background: palette(alternate-base); font-style: italic;"
                                 " border: 1px dashed palette(mid); border-radius: 4px; }"));
}

void NoSchoolBanner::contextMenuEvent(QContextMenuEvent *event)
{
    emit menuRequested(m_date, event->globalPos());
}

WeekView::WeekView(Database *db, QDate monday, QWidget *parent)
    : QScrollArea(parent), m_db(db), m_monday(monday)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
}

CellWidget *WeekView::cell(int classId, QDate day) const
{
    return m_cells.value({classId, day}, nullptr);
}

void WeekView::showEvent(QShowEvent *event)
{
    // weeks are built the first time their tab is shown
    if (!loaded())
        reload();
    QScrollArea::showEvent(event);
}

WeekView::Structure WeekView::currentStructure()
{
    Structure structure;
    for (const ClassInfo &c : m_db->classes())
        structure.classes.append({c.id, c.name});
    const schoolcal::NoSchool noSchool = m_db->noSchoolDays();
    for (QDate day : schoolcal::weekDays(m_monday)) {
        if (noSchool.contains(day))
            structure.noSchool.append({day, noSchool.value(day)});
    }
    return structure;
}

void WeekView::reload(bool force)
{
    const Structure structure = currentStructure();
    if (structure != m_structure)
        build(structure);
    const QList<QDate> days = schoolcal::weekDays(m_monday);
    QMap<std::pair<int, QDate>, Entry> entries;
    for (const Entry &e : m_db->entriesBetween(days.first(), days.last()))
        entries.insert({e.classId, e.date}, e);
    for (auto it = m_cells.cbegin(); it != m_cells.cend(); ++it) {
        const Entry blank{it.key().first, it.key().second, {}, {}, {}};
        it.value()->setEntry(entries.value(it.key(), blank), force);
    }
}

void WeekView::flush()
{
    for (CellWidget *cell : std::as_const(m_cells))
        cell->flush();
}

bool WeekView::hasUnsaved() const
{
    for (CellWidget *cell : std::as_const(m_cells)) {
        if (cell->isDirty())
            return true;
    }
    return false;
}

void WeekView::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (CellWidget *cell : std::as_const(m_cells))
        cell->setReadOnly(readOnly);
}

void WeekView::build(const Structure &structure)
{
    flush();
    m_structure = structure;
    m_cells.clear();

    auto *gridHost = new QWidget;
    auto *grid = new QGridLayout(gridHost);
    grid->setSpacing(6);
    grid->setContentsMargins(8, 8, 8, 8);

    QFont headerFont(font());
    headerFont.setBold(true);
    headerFont.setPointSizeF(headerFont.pointSizeF() * 1.1);
    int col = 1;
    for (const auto &[classId, name] : structure.classes) {
        auto *label = new QLabel(name);
        label->setTextFormat(Qt::PlainText);
        label->setFont(headerFont);
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 0, col);
        grid->setColumnMinimumWidth(col, MIN_COLUMN_WIDTH);
        grid->setColumnStretch(col, 1);
        ++col;
    }

    QMap<QDate, QString> noSchool;
    for (const auto &[day, reason] : structure.noSchool)
        noSchool.insert(day, reason);
    const QDate today = QDate::currentDate();
    int row = 1;
    for (QDate day : schoolcal::weekDays(m_monday)) {
        auto *dayLabel = new QLabel(QStringLiteral("<b>%1</b><br>%2")
                                        .arg(schoolcal::format(day, QStringLiteral("dddd")),
                                             schoolcal::format(day, QStringLiteral("MMM d"))));
        dayLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        if (day == today)
            dayLabel->setStyleSheet(QStringLiteral("QLabel { color: palette(highlight); }"));
        grid->addWidget(dayLabel, row, 0);
        if (noSchool.contains(day)) {
            auto *banner = new NoSchoolBanner(day, noSchool.value(day));
            connect(banner, &NoSchoolBanner::menuRequested, this, &WeekView::noSchoolMenuRequested);
            grid->addWidget(banner, row, 1, 1, int(qMax(structure.classes.size(), qsizetype(1))));
        } else {
            col = 1;
            for (const auto &[classId, name] : structure.classes) {
                auto *cell = new CellWidget(classId, day);
                cell->setReadOnly(m_readOnly);
                connect(cell, &CellWidget::changed, this, &WeekView::entryChanged);
                connect(cell, &CellWidget::menuRequested, this, &WeekView::cellMenuRequested);
                grid->addWidget(cell, row, col++);
                m_cells.insert({classId, day}, cell);
            }
        }
        ++row;
    }
    grid->setRowStretch(6, 1);

    // The old grid may be the one whose right-click menu is open right now, so
    // it is deleted once control is back in the event loop, not underneath it.
    if (QWidget *old = takeWidget()) {
        old->hide();
        old->deleteLater();
    }
    setWidget(gridHost);
}
