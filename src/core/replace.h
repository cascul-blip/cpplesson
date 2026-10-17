// Move `from` onto `to`, replacing whatever is already there.
#pragma once

#include <QString>

#include <filesystem>
#include <stdexcept>

inline void replaceFile(const QString &from, const QString &to)
{
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(from.toStdU16String()),
                            std::filesystem::path(to.toStdU16String()), error);
    if (error) {
        throw std::runtime_error(QStringLiteral("Could not write %1: %2")
                                     .arg(to, QString::fromStdString(error.message()))
                                     .toStdString());
    }
}
