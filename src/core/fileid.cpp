#include "core/fileid.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <QFile>

#include <sys/stat.h>
#endif

std::optional<FileId> fileIdentity(const QString &path)
{
#ifdef Q_OS_WIN
    const HANDLE file = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info;
    const bool ok = GetFileInformationByHandle(file, &info);
    CloseHandle(file);
    if (!ok)
        return std::nullopt;
    return FileId{info.dwVolumeSerialNumber,
                  (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
#else
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return std::nullopt;
    return FileId{quint64(st.st_dev), quint64(st.st_ino)};
#endif
}
