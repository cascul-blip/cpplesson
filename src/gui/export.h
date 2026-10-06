// Printing and PDF export of a week, scaled to fit one landscape page.
#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

class Database;
class QPrinter;

namespace exporter {

inline const QString TABLET_FOLDER = QStringLiteral("Tablet");

// Letter landscape; prints to `pdfPath` instead of a printer when given.
void setupPrinter(QPrinter &printer, const QString &pdfPath = {});
void paintHtml(const QString &html, QPrinter &printer);
void exportWeekPdf(Database &db, QDate monday, const QString &path);
QString tabletPdfName(QDate monday, QDate week1Monday);

// Keep <folder>/Tablet in sync: one read-only PDF per week that has lessons.
// Only weeks whose content changed are rewritten. Returns the files written.
QStringList exportTablet(Database &db, const QString &folder);

}  // namespace exporter
