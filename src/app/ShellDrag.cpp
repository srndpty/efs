#include "app/ShellDrag.h"

#include <QDir>
#include <QWidget>

#include <string>
#include <vector>

#include <windows.h>

#include <shlobj.h>
#include <wrl/client.h>

namespace efs {

namespace {

using Microsoft::WRL::ComPtr;

// ILCreateFromPathW で作った PIDL の束。途中で失敗しても確実に解放する。
class PidlList {
public:
    PidlList() = default;
    ~PidlList()
    {
        for (PIDLIST_ABSOLUTE pidl : m_pidls)
            ::ILFree(pidl);
    }

    PidlList(const PidlList&) = delete;
    PidlList& operator=(const PidlList&) = delete;
    PidlList(PidlList&&) = delete;
    PidlList& operator=(PidlList&&) = delete;

    bool add(const QString& fullPath)
    {
        const std::wstring native = QDir::toNativeSeparators(fullPath).toStdWString();
        PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW(native.c_str());
        if (!pidl)
            return false;
        m_pidls.push_back(pidl);
        return true;
    }

    [[nodiscard]] UINT size() const { return static_cast<UINT>(m_pidls.size()); }

    // SHCreateShellItemArrayFromIDLists は要素が const の配列を取る。
    // 内側の const が増えるだけの変換なので const_cast で足りる。
    [[nodiscard]] PCIDLIST_ABSOLUTE* data()
    {
        return const_cast<PCIDLIST_ABSOLUTE*>(m_pidls.data());
    }

private:
    std::vector<PIDLIST_ABSOLUTE> m_pidls;
};

} // namespace

bool startShellDrag(QWidget* source, const QStringList& fullPaths)
{
    if (source == nullptr || fullPaths.isEmpty())
        return false;

    PidlList pidls;
    for (const QString& path : fullPaths) {
        // **一部だけ載せない。** 受け側は落ちてきた集合をそのまま扱うので、
        // 黙って間引くと「掴んだものと違うものが移動する」ことになる。
        if (path.isEmpty() || !pidls.add(path))
            return false;
    }

    ComPtr<IShellItemArray> items;
    if (FAILED(::SHCreateShellItemArrayFromIDLists(pidls.size(), pidls.data(), &items)))
        return false;

    // シェル自身のデータオブジェクト。CF_HDROP も CFSTR_SHELLIDLIST も
    // ドラッグ画像も、Explorer がドラッグ元のときと同じものが載る。
    // 移動でドロップされたときに元を消すのもこのオブジェクトの仕事なので、
    // **こちらでファイルを消しに行かない** (消し漏れてもコピーになるだけで、
    // 二重に消す事故は起こさない)。
    ComPtr<IDataObject> data;
    if (FAILED(items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))))
        return false;

    // DoDragDrop はスレッドが OleInitialize 済みであることを要求する。GUI
    // スレッドは Qt の Windows プラグインが起動時に初期化しているので、ここで
    // 初期化し直さない (FileActions.cpp の ComScope は CoInitializeEx が要る
    // SHOpenFolderAndSelectItems 用であり、こちらとは別の話)。
    //
    // 3 つとも許可する。無印 = 移動 / Ctrl = コピー / Alt = ショートカット の
    // 対応と、同一ドライブなら移動・別ドライブならコピーという既定の選択は、
    // 受け側 (ドロップ先の IDropTarget) がキー状態から決める。ここで既定を
    // 決め打つと Explorer と挙動がずれる。
    // winId() は WId (quintptr) なので、HWND へ戻すには整数からポインタへの
    // キャストしか無い (Theme.cpp と同じ Qt / Win32 の境界)。
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const hwnd = reinterpret_cast<HWND>(source->window()->winId());
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = ::SHDoDragDrop(hwnd, data.Get(), nullptr,
                                      DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}

} // namespace efs
