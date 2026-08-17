#include "EverythingApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <string>

#include "Everything.h"

namespace efs {

namespace {

// エクスポートを1つ解決する。失敗したら名前を missing に記録して false を返す。
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
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString s = n ? QString::fromWCharArray(buf, static_cast<int>(n)).trimmed()
                  : QStringLiteral("不明なエラー");
    if (buf)
        ::LocalFree(buf);
    return QStringLiteral("%1 (win32 エラー %2)").arg(s).arg(code);
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

    // **探索先は exe と同階層の絶対パス 1 つだけ。PATH へフォールバックしない。**
    // ビルド (CMake の efs_copy_everything_dll) と配布 (scripts/package.ps1) の
    // どちらも DLL を exe の隣へ必ず置く契約なので、そこに無いのは配置の失敗。
    // CWD / PATH 上の別の Everything64.dll を拾って「環境によって動いたり
    // 動かなかったりする」状態を作るより、明確に失敗して理由を出す方が良い
    // (AGENTS.md「フォールバック・互換性は最小」)。
    const QString path =
        QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath())
                                     .absoluteFilePath(QStringLiteral("Everything64.dll")));

    if (!QFileInfo::exists(path)) {
        m_loadError = QStringLiteral("Everything64.dll が exe と同階層に無い: %1").arg(path);
        return false;
    }

    // 絶対パスで指定した上で検索順序も限定する。依存 DLL の解決先を system32 と
    // DLL 自身のディレクトリに限り、PATH 上の同名 DLL を巻き込まない。
    const std::wstring wide = path.toStdWString();
    m_module = ::LoadLibraryExW(wide.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!m_module) {
        m_loadError =
            QStringLiteral("%1 をロードできない: %2").arg(path, formatWin32Error(::GetLastError()));
        return false;
    }
    m_dllPath = path;

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
        m_loadError = QStringLiteral("%1: 解決できないエクスポート: %2")
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
        return QStringLiteral("ERROR_MEMORY (メモリ不足)");
    case EVERYTHING_ERROR_IPC:
        return QStringLiteral("ERROR_IPC (Everything が起動していない)");
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
        return QStringLiteral("未知のエラーコード %1").arg(code);
    }
}

} // namespace efs
