// Week view as print-oriented HTML (the subset QTextDocument understands).
#pragma once

#include <QDate>
#include <QString>

class Database;

namespace render {

QString weekHtml(Database &db, QDate monday);

}  // namespace render
