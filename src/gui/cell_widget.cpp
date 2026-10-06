#include "gui/cell_widget.h"

#include "gui/theme.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const int AUTOSAVE_MS = 1000;
const char *const FIELD_NAMES[] = {"lesson", "special", "homework"};
const char *const FIELD_LABELS[] = {"Lesson", "Special", "Homework"};
const int MIN_LINES[] = {1, 1, 2};

QString cellStyle()
{
    return QStringLiteral(R"(
CellWidget { background: palette(base); border: 1px solid palette(mid); border-radius: 4px; }
CellWidget[special="true"] { border: 2px solid %1; }
QPlainTextEdit { border: none; background: transparent; }
QPlainTextEdit[field="special"][filled="true"] {
    background: rgba(224, 161, 0, 0.22); border-radius: 3px;
}
QLabel { color: palette(placeholder-text); font-size: 8pt; }
QLabel[field="special"][filled="true"] { color: %1; font-weight: bold; }
)").arg(QLatin1String(theme::SPECIAL_ACCENT));
}

// Python's str.rstrip(): drop trailing whitespace only.
QString rstrip(const QString &text)
{
    qsizetype end = text.size();
    while (end > 0 && text.at(end - 1).isSpace())
        --end;
    return text.left(end);
}

}  // namespace

// --- FieldEdit ---------------------------------------------------------------

FieldEdit::FieldEdit(int minLines, QWidget *parent) : QPlainTextEdit(parent), m_minLines(minLines)
{
    setTabChangesFocus(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &FieldEdit::fit);
    fit();
}

void FieldEdit::fit()
{
    // a plain-text document reports its height in lines
    const int lines = qMax(int(document()->documentLayout()->documentSize().height()), m_minLines);
    const QFontMetrics fm(font());
    const QMargins margins = contentsMargins();
    setFixedHeight(lines * fm.lineSpacing() + int(2 * document()->documentMargin())
                   + margins.top() + margins.bottom() + 2);
}

void FieldEdit::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    emit focusLost();
}

// --- CellWidget --------------------------------------------------------------

CellWidget::CellWidget(int classId, QDate day, QWidget *parent)
    : QFrame(parent), m_classId(classId), m_date(day)
{
    setStyleSheet(cellStyle());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 4);
    layout->setSpacing(0);
    for (int field = 0; field < FieldCount; ++field) {
        auto *label = new QLabel(QLatin1String(FIELD_LABELS[field]));
        label->setProperty("field", QLatin1String(FIELD_NAMES[field]));
        auto *edit = new FieldEdit(MIN_LINES[field]);
        edit->setProperty("field", QLatin1String(FIELD_NAMES[field]));
        if (field == Lesson) {
            QFont bold(edit->font());
            bold.setBold(true);
            edit->setFont(bold);
        }
        connect(edit, &QPlainTextEdit::textChanged, this, &CellWidget::onTextChanged);
        connect(edit, &FieldEdit::focusLost, this, &CellWidget::flush);
        edit->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(edit, &QWidget::customContextMenuRequested, this,
                [this, edit](const QPoint &pos) { showContextMenu(edit, pos); });
        layout->addWidget(label);
        layout->addWidget(edit);
        m_labels[field] = label;
        m_edits[field] = edit;
    }
    layout->addStretch();

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(AUTOSAVE_MS);
    connect(m_timer, &QTimer::timeout, this, &CellWidget::flush);
    updateSpecialStyle();
}

Entry CellWidget::entry() const
{
    return Entry{m_classId, m_date, rstrip(m_edits[Lesson]->toPlainText()),
                 rstrip(m_edits[Special]->toPlainText()), rstrip(m_edits[Homework]->toPlainText())};
}

void CellWidget::setEntry(const Entry &entry, bool force)
{
    if (!force && (m_dirty || hasEditFocus()))
        return;
    const QString values[] = {entry.lesson, entry.special, entry.homework};
    for (int field = 0; field < FieldCount; ++field) {
        FieldEdit *edit = m_edits[field];
        if (edit->toPlainText() != values[field]) {
            const QSignalBlocker blocker(edit);
            edit->setPlainText(values[field]);
            edit->fit();
        }
    }
    m_timer->stop();
    m_dirty = false;
    updateSpecialStyle();
}

bool CellWidget::hasEditFocus() const
{
    for (const FieldEdit *edit : m_edits) {
        if (edit->hasFocus())
            return true;
    }
    return false;
}

void CellWidget::setReadOnly(bool readOnly)
{
    for (FieldEdit *edit : m_edits)
        edit->setReadOnly(readOnly);
}

void CellWidget::flush()
{
    m_timer->stop();
    if (m_dirty) {
        m_dirty = false;
        emit changed(entry());
    }
}

void CellWidget::focusLesson()
{
    m_edits[Lesson]->setFocus();
}

void CellWidget::onTextChanged()
{
    m_dirty = true;
    m_timer->start();
    updateSpecialStyle();
}

void CellWidget::updateSpecialStyle()
{
    const bool filled = !m_edits[Special]->toPlainText().trimmed().isEmpty();
    const QVariant current = property("special");
    if (current.isValid() && current.toBool() == filled)
        return;
    setProperty("special", filled);
    m_edits[Special]->setProperty("filled", filled);
    m_labels[Special]->setProperty("filled", filled);
    QFont font(m_edits[Special]->font());
    font.setBold(filled);
    m_edits[Special]->setFont(font);
    // re-polish so the property-based style rules apply
    for (QWidget *w : {static_cast<QWidget *>(this), static_cast<QWidget *>(m_edits[Special]),
                       static_cast<QWidget *>(m_labels[Special])}) {
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
}

void CellWidget::showContextMenu(FieldEdit *edit, QPoint pos)
{
    flush();
    emit menuRequested(m_classId, m_date, edit->mapToGlobal(pos), edit->createStandardContextMenu());
}
