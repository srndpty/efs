#include "EverythingApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "Everything.h"

namespace efs {

namespace {

// Resolves one export; on failure records the name and returns false.
template <typename Fn>
bool resolve(HMODULE mod, const char* name, Fn& out, QStringList& missing)
{
    auto* p = ::GetProcAddress(mod, name);
    if (!p) {
        missing << QString::fromLatin1(name);
        return false;
    }
    out = reinterpret_cast<Fn>(p);
    return true;
}

QString formatWin32Error(DWORD code)
{
    LPWSTR buf = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString s = n ? QString::fromWCharArray(buf, static_cast<int>(n)).trimmed()
                  : QStringLiteral("unknown error");
    if (buf)
        ::LocalFree(buf);
    return QStringLiteral("%1 (win32 error %2)").arg(s).arg(code);
}

} // namespace

EverythingApi::~EverythingApi()
{
    if (m_module) {
        if (CleanUp)
            CleanUp();
        ::FreeLibrary(m_module);
        m_module = nullptr;
    }
}

bool EverythingApi::load()
{
    if (m_module)
        return true;

    m_loadError.clear();
    m_searchedPaths.clear();

    // (1) next to the executable, (2) the default DLL search order (PATH).
    const QString beside =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("Everything64.dll"));
    m_searchedPaths << QDir::toNativeSeparators(beside)
                    << QStringLiteral("<default DLL search order / PATH>");

    QString attemptedError;
    if (QFileInfo::exists(beside)) {
        const std::wstring w = QDir::toNativeSeparators(beside).toStdWString();
        m_module = ::LoadLibraryW(w.c_str());
        if (m_module) {
            m_dllPath = QDir::toNativeSeparators(beside);
        } else {
            attemptedError = QStringLiteral("%1: %2")
                                 .arg(QDir::toNativeSeparators(beside),
                                      formatWin32Error(::GetLastError()));
        }
    } else {
        attemptedError = QStringLiteral("%1: file not found")
                             .arg(QDir::toNativeSeparators(beside));
    }

    if (!m_module) {
        m_module = ::LoadLibraryW(L"Everything64.dll");
        if (m_module) {
            wchar_t path[MAX_PATH] = {};
            if (::GetModuleFileNameW(m_module, path, MAX_PATH))
                m_dllPath = QString::fromWCharArray(path);
            else
                m_dllPath = QStringLiteral("Everything64.dll");
        }
    }

    if (!m_module) {
        m_loadError = QStringLiteral(
                          "Everything64.dll could not be loaded. %1; "
                          "not on PATH either: %2")
                          .arg(attemptedError,
                               formatWin32Error(::GetLastError()));
        return false;
    }

    QStringList missing;
    resolve(m_module, "Everything_SetSearchW", SetSearchW, missing);
    resolve(m_module, "Everything_SetRegex", SetRegex, missing);
    resolve(m_module, "Everything_SetMatchCase", SetMatchCase, missing);
    resolve(m_module, "Everything_SetMatchWholeWord", SetMatchWholeWord, missing);
    resolve(m_module, "Everything_SetMatchPath", SetMatchPath, missing);
    resolve(m_module, "Everything_SetSort", SetSort, missing);
    resolve(m_module, "Everything_SetRequestFlags", SetRequestFlags, missing);
    resolve(m_module, "Everything_SetMax", SetMax, missing);
    resolve(m_module, "Everything_SetOffset", SetOffset, missing);
    resolve(m_module, "Everything_QueryW", QueryW, missing);
    resolve(m_module, "Everything_GetNumResults", GetNumResults, missing);
    resolve(m_module, "Everything_GetTotResults", GetTotResults, missing);
    resolve(m_module, "Everything_GetResultFileNameW", GetResultFileNameW, missing);
    resolve(m_module, "Everything_GetResultPathW", GetResultPathW, missing);
    resolve(m_module, "Everything_GetResultSize", GetResultSize, missing);
    resolve(m_module, "Everything_GetResultDateModified", GetResultDateModified, missing);
    resolve(m_module, "Everything_IsFolderResult", IsFolderResult, missing);
    resolve(m_module, "Everything_GetLastError", GetLastError, missing);
    resolve(m_module, "Everything_CleanUp", CleanUp, missing);
    resolve(m_module, "Everything_GetMajorVersion", GetMajorVersion, missing);
    resolve(m_module, "Everything_GetMinorVersion", GetMinorVersion, missing);
    resolve(m_module, "Everything_GetRevision", GetRevision, missing);
    resolve(m_module, "Everything_GetBuildNumber", GetBuildNumber, missing);

    if (!missing.isEmpty()) {
        m_loadError = QStringLiteral("%1: unresolved export(s): %2")
                          .arg(m_dllPath, missing.join(QStringLiteral(", ")));
        ::FreeLibrary(m_module);
        m_module = nullptr;
        return false;
    }

    return true;
}

QString everythingErrorText(DWORD code)
{
    switch (code) {
    case EVERYTHING_OK:
        return QStringLiteral("OK");
    case EVERYTHING_ERROR_MEMORY:
        return QStringLiteral("ERROR_MEMORY (out of memory)");
    case EVERYTHING_ERROR_IPC:
        return QStringLiteral("ERROR_IPC (Everything is not running)");
    case EVERYTHING_ERROR_REGISTERCLASSEX:
        return QStringLiteral("ERROR_REGISTERCLASSEX");
    case EVERYTHING_ERROR_CREATEWINDOW:
        return QStringLiteral("ERROR_CREATEWINDOW");
    case EVERYTHING_ERROR_CREATETHREAD:
        return QStringLiteral("ERROR_CREATETHREAD");
    case EVERYTHING_ERROR_INVALIDINDEX:
        return QStringLiteral("ERROR_INVALIDINDEX");
    case EVERYTHING_ERROR_INVALIDCALL:
        return QStringLiteral("ERROR_INVALIDCALL");
    case EVERYTHING_ERROR_INVALIDREQUEST:
        return QStringLiteral("ERROR_INVALIDREQUEST");
    case EVERYTHING_ERROR_INVALIDPARAMETER:
        return QStringLiteral("ERROR_INVALIDPARAMETER");
    default:
        return QStringLiteral("unknown error code %1").arg(code);
    }
}

} // namespace efs
