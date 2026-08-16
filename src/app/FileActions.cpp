#include "app/FileActions.h"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>

#include <string>

#include <windows.h>

#include <shlobj.h>

namespace efs {

namespace {

// SHOpenFolderAndSelectItems は COM の初期化を要求する。GUI スレッドは Qt が
// 既に OleInitialize しているので通常は S_FALSE が返るが、それに依存せず
// 呼ぶ側で釣り合いを取る。
class ComScope {
public:
    ComScope() : m_hr(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(m_hr))
            ::CoUninitialize();
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;

private:
    HRESULT m_hr;
};

} // namespace

bool openPath(const QString& fullPath)
{
    if (fullPath.isEmpty())
        return false;
    // ShellExecute 相当をコマンド文字列を組まずに呼ぶ。
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
}

bool revealInExplorer(const QString& fullPath)
{
    if (fullPath.isEmpty())
        return false;

    const ComScope com;

    const std::wstring native = QDir::toNativeSeparators(fullPath).toStdWString();
    PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(native.c_str());
    if (!pidl)
        return false;

    // explorer /select,"..." のようなコマンド行を組み立てない。パスは PIDL
    // として渡るので、空白・`,`・`&`・括弧・日本語のいずれも解釈されない。
    const HRESULT hr = ::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ::ILFree(pidl);
    return SUCCEEDED(hr);
}

} // namespace efs
