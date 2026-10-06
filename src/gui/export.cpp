#include "gui/export.h"

#include "core/database.h"
#include "core/render.h"
#include "core/schoolcal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPainter>
#include <QPrinter>
#include <QSet>
#include <QTextDocument>

#include <stdexcept>

namespace exporter {

namespace {

const int LOGICAL_WIDTH = 950;  // layout width in document pixels before scaling to the page
const QString HASH_FILE = QStringLiteral(".contents.json");

}  // namespace

void setupPrinter(QPrinter &printer, const QString &pdfPath)
{
    printer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Landscape,
                                      QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter));
    if (!pdfPath.isEmpty()) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(pdfPath);
    }
}

void paintHtml(const QString &html, QPrinter &printer)
{
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.setTextWidth(LOGICAL_WIDTH);
    const QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
    const qreal height = qMax(doc.size().height(), 1.0);
    const qreal scale = qMin(page.width() / qreal(LOGICAL_WIDTH), page.height() / height);
    QPainter painter;
    if (!painter.begin(&printer)) {
        const QString target = printer.outputFileName().isEmpty() ? printer.printerName()
                                                                  : printer.outputFileName();
        throw std::runtime_error(QStringLiteral("Could not write to %1").arg(target).toStdString());
    }
    painter.scale(scale, scale);
    doc.drawContents(&painter);
    painter.end();
}

void exportWeekPdf(Database &db, QDate monday, const QString &path)
{
    QPrinter printer(QPrinter::HighResolution);
    setupPrinter(printer, path);
    paintHtml(render::weekHtml(db, monday), printer);
}

QString tabletPdfName(QDate monday, QDate week1Monday)
{
    return QStringLiteral("Week %1 (%2).pdf")
        .arg(schoolcal::weekNumber(monday, week1Monday), 2, 10, QLatin1Char('0'))
        .arg(schoolcal::format(monday, QStringLiteral("MMM dd")));
}

QStringList exportTablet(Database &db, const QString &folder)
{
    const QDir outDir(QDir(folder).filePath(TABLET_FOLDER));
    if (!outDir.mkpath(QStringLiteral(".")))
        throw std::runtime_error(
            QStringLiteral("Could not create %1").arg(outDir.path()).toStdString());
    QFile hashFile(outDir.filePath(HASH_FILE));
    QJsonObject oldHashes;
    if (hashFile.open(QIODevice::ReadOnly)) {
        oldHashes = QJsonDocument::fromJson(hashFile.readAll()).object();
        hashFile.close();
    }

    const QDate week1 = db.week1Monday();
    QSet<QDate> seen;
    QList<QDate> mondays;  // ascending, like the dates they come from
    for (QDate day : db.datesWithEntries()) {
        const QDate monday = schoolcal::mondayOf(day);
        if (!seen.contains(monday)) {
            seen.insert(monday);
            mondays.append(monday);
        }
    }

    QJsonObject hashes;
    QStringList written;
    for (QDate monday : std::as_const(mondays)) {
        const QString name = tabletPdfName(monday, week1);
        const QString html = render::weekHtml(db, monday);
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(html.toUtf8(), QCryptographicHash::Sha1).toHex());
        hashes[name] = digest;
        const QString path = outDir.filePath(name);
        if (oldHashes.value(name).toString() != digest || !QFile::exists(path)) {
            QPrinter printer(QPrinter::HighResolution);
            setupPrinter(printer, path);
            paintHtml(html, printer);
            written.append(path);
        }
    }
    for (const QString &name : oldHashes.keys()) {
        if (!hashes.contains(name))
            QFile::remove(outDir.filePath(name));
    }
    if (hashes != oldHashes && hashFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        hashFile.write(QJsonDocument(hashes).toJson());
    return written;
}

}  // namespace exporter
