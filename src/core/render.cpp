#include "core/render.h"

#include "core/database.h"
#include "core/schoolcal.h"

#include <QStringList>

#include <utility>

namespace render {

namespace {

QString lines(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList parts = text.split(QLatin1Char('\n'));
    if (!parts.isEmpty() && parts.last().isEmpty())
        parts.removeLast();
    for (QString &part : parts)
        part = part.toHtmlEscaped();
    return parts.join(QLatin1String("<br>"));
}

QString monthDay(QDate day)
{
    return schoolcal::format(day, QStringLiteral("MMM d"));
}

}  // namespace

QString weekHtml(Database &db, QDate monday)
{
    const QList<ClassInfo> classes = db.classes();
    const QList<QDate> days = schoolcal::weekDays(monday);
    QMap<std::pair<int, QDate>, Entry> entries;
    for (const Entry &e : db.entriesBetween(days.first(), days.last()))
        entries.insert({e.classId, e.date}, e);
    const schoolcal::NoSchool noSchool = db.noSchoolDays();
    const int week = schoolcal::weekNumber(monday, db.week1Monday());
    const QDate friday = days.last();

    const int colWidth = 92 / int(qMax(classes.size(), qsizetype(1)));
    QStringList out;
    out << QStringLiteral(
               "<html><body style='font-family: sans-serif; font-size: 9pt; color: #000;'>")
        << QStringLiteral("<h3 style='margin: 0 0 4px 0;'>Week %1 &ndash; %2 to %3, %4</h3>")
               .arg(week).arg(monthDay(monday), monthDay(friday)).arg(friday.year())
        << QStringLiteral("<table width='100%' border='1' cellspacing='0' cellpadding='3' "
                          "style='border-collapse: collapse; border-color: #555;'>")
        << QStringLiteral("<tr><th width='8%' style='background: #ddd;'></th>");
    for (const ClassInfo &c : classes) {
        out << QStringLiteral("<th width='%1%' style='background: #ddd;'>%2</th>")
                   .arg(colWidth).arg(c.name.toHtmlEscaped());
    }
    out << QStringLiteral("</tr>");

    for (QDate day : days) {
        out << QStringLiteral("<tr><td valign='top' style='background: #eee;'><b>%1</b><br>%2</td>")
                   .arg(schoolcal::format(day, QStringLiteral("ddd")), monthDay(day));
        if (noSchool.contains(day)) {
            QString reason = noSchool.value(day);
            if (reason.isEmpty())
                reason = QStringLiteral("No school");
            out << QStringLiteral("<td colspan='%1' align='center' valign='middle' "
                                  "style='background: #eee;'><i>No School &ndash; %2</i></td></tr>")
                       .arg(classes.size()).arg(reason.toHtmlEscaped());
            continue;
        }
        for (const ClassInfo &c : classes) {
            QStringList parts;
            const auto it = entries.constFind({c.id, day});
            if (it != entries.constEnd()) {
                if (!it->lesson.trimmed().isEmpty())
                    parts << QStringLiteral("<b>%1</b>").arg(lines(it->lesson));
                if (!it->special.trimmed().isEmpty()) {
                    parts << QStringLiteral(
                                 "<table width='100%' border='1' cellspacing='0' cellpadding='2' "
                                 "style='border-collapse: collapse; border-color: #000; "
                                 "margin: 2px 0;'><tr><td style='background: #ffe9a8;'>"
                                 "<b>&#9733; %1</b></td></tr></table>")
                                 .arg(lines(it->special));
                }
                if (!it->homework.trimmed().isEmpty())
                    parts << QStringLiteral("<i>HW:</i> %1").arg(lines(it->homework));
            }
            out << QStringLiteral("<td valign='top'>%1</td>").arg(parts.join(QLatin1String("<br>")));
        }
        out << QStringLiteral("</tr>");
    }
    out << QStringLiteral("</table></body></html>");
    return out.join(QLatin1Char('\n'));
}

}  // namespace render
