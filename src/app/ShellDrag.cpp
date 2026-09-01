#include "app/ShellDrag.h"

#include <QDebug>
#include <QDir>
#include <QWidget>

#include <string>

#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace efs {

namespace {

using Microsoft::WRL::ComPtr;

// 失敗を無音にしない。popup は出さず、後から追える情報だけ残す。
void warnFailure(const char* stage, const QString& fullPath, HRESULT hr)
{
    qWarning("ドラッグを開始できない (%s, hr=0x%08lX): %s", stage, static_cast<unsigned long>(hr),
             qUtf8Printable(fullPath));
}

// DoDragDrop は OLE の初期化を要求する。GUI スレッドは Qt の Windows プラグイン
// が初期化しているはずだが、**その内部実装だけを前提にしない**。既に初期化済み
// なら S_FALSE が返って参照数が増えるだけなので、ここで釣り合いを取ればよい。
// RPC_E_CHANGED_MODE 等で失敗したときに CoUninitialize 相当を呼ぶと他所が張った
// 初期化を剥がしてしまうので、成功したときだけ解放する (ShellIcon.cpp と同じ規則)。
class OleScope {
public:
    OleScope() : m_hr(::OleInitialize(nullptr)) {}
    ~OleScope()
    {
        if (SUCCEEDED(m_hr))
            ::OleUninitialize();
    }

    OleScope(const OleScope&) = delete;
    OleScope& operator=(const OleScope&) = delete;
    OleScope(OleScope&&) = delete;
    OleScope& operator=(OleScope&&) = delete;

    [[nodiscard]] HRESULT hr() const { return m_hr; }

private:
    HRESULT m_hr;
};

// SHParseDisplayName で作った絶対 PIDL。
// **ILCreateFromPathW は使わない** — docs 上 MAX_PATH までしか契約されておらず、
// Everything はそれを超えるパスを結果に返しうる。SHParseDisplayName にはその
// 制約が無く、321 文字の実在パスで PIDL / IDataObject の取得まで実測で通った
// (「長さ無制限」を保証したわけではない)。
//
// **`\\?\` 前置のパスを渡さない。** その形は
// SHParseDisplayName に E_INVALIDARG で弾かれる (これも実測)。渡すのは
// Everything が返す素のパスのままでよい。
class AbsolutePidl {
public:
    AbsolutePidl() = default;
    // PIDL はタスクアロケータから来るので CoTaskMemFree で返す
    // (Windows 2000 以降はこちらが Microsoft の推奨。ILFree は等価な旧 API)。
    ~AbsolutePidl() { ::CoTaskMemFree(m_pidl); }

    AbsolutePidl(const AbsolutePidl&) = delete;
    AbsolutePidl& operator=(const AbsolutePidl&) = delete;
    AbsolutePidl(AbsolutePidl&&) = delete;
    AbsolutePidl& operator=(AbsolutePidl&&) = delete;

    HRESULT parse(const QString& fullPath)
    {
        const std::wstring native = QDir::toNativeSeparators(fullPath).toStdWString();
        return ::SHParseDisplayName(native.c_str(), nullptr, &m_pidl, 0, nullptr);
    }

    [[nodiscard]] PCIDLIST_ABSOLUTE get() const { return m_pidl; }

private:
    PIDLIST_ABSOLUTE m_pidl = nullptr;
};

} // namespace

bool startShellDrag(QWidget* source, const QString& fullPath)
{
    if (source == nullptr || fullPath.isEmpty())
        return false;

    const OleScope ole;
    if (FAILED(ole.hr())) {
        warnFailure("OleInitialize", fullPath, ole.hr());
        return false;
    }

    AbsolutePidl pidl;
    HRESULT hr = pidl.parse(fullPath);
    if (FAILED(hr)) {
        warnFailure("SHParseDisplayName", fullPath, hr);
        return false;
    }

    // 親フォルダの IShellFolder と、その中での子 PIDL を得る。
    // ppidlLast は絶対 PIDL の内部を指すので解放しない (pidl の寿命に従う)。
    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = nullptr;
    hr = ::SHBindToParent(pidl.get(), IID_PPV_ARGS(&parent), &child);
    if (FAILED(hr)) {
        warnFailure("SHBindToParent", fullPath, hr);
        return false;
    }

    // **Explorer が item のデータオブジェクトを取るのと同じ経路。**
    // CFSTR_SHELLIDLIST と CF_HDROP が載ることは実測で確認済み。ドラッグ画像は
    // 自前で用意せず SHDoDragDrop とシェルの既定処理に任せる。
    // 移動でドロップされたときに元を消すのもこのオブジェクトの仕事なので、
    // **こちらでファイルを消しに行かない**
    // (消し漏れてもコピーになるだけで、二重に消す事故は起こさない)。
    //
    // winId() は WId (quintptr) なので、HWND へ戻すには整数からポインタへの
    // キャストしか無い (Theme.cpp と同じ Qt / Win32 の境界)。
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const hwnd = reinterpret_cast<HWND>(source->window()->winId());
    ComPtr<IDataObject> data;
    hr = parent->GetUIObjectOf(hwnd, 1, &child, IID_IDataObject, nullptr,
                               reinterpret_cast<void**>(data.GetAddressOf()));
    if (FAILED(hr) || !data) {
        warnFailure("GetUIObjectOf", fullPath, hr);
        return false;
    }

    // 3 つとも許可する。無印 = 移動 / Ctrl = コピー / Alt = ショートカット の
    // 対応と、同一ドライブなら移動・別ドライブならコピーという既定の選択は、
    // 受け側 (ドロップ先の IDropTarget) がキー状態から決める。ここで既定を
    // 決め打つと Explorer と挙動がずれる。
    DWORD effect = DROPEFFECT_NONE;
    hr = ::SHDoDragDrop(hwnd, data.Get(), nullptr,
                        DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    // Escape / 右ボタンでのキャンセルは DRAGDROP_S_CANCEL。失敗ではないので
    // 警告は出さない (受け側がドロップを受け付けなかった場合も同じ扱い)。
    if (FAILED(hr)) {
        warnFailure("SHDoDragDrop", fullPath, hr);
        return false;
    }
    return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}

} // namespace efs
