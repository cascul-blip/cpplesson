// Identity of the file behind a path, to notice when a sync client swaps in a
// new file under the same name.
#pragma once

#include <QString>

#include <optional>
#include <utility>

// (volume, file index); nothing if the file cannot be examined.
using FileId = std::pair<quint64, quint64>;

std::optional<FileId> fileIdentity(const QString &path);
