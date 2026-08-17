#include "app/ShellIcon.h"

#include <QImage>

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

namespace efs {

namespace {

// SHGetFileInfoW は shell の COM オブジェクトを内部で使う。アイコン worker の
// スレッドでも呼ぶので、そのスレッドで一度だけ COM を初期化する。
//
// 成功した CoInitializeEx は必ず同数の CoUninitialize と対にする必要がある
// (S_FALSE = 既に同じモードで初期化済み、も「1 回数えた」ので対にする)。
// thread_local な RAII にしておくと、そのスレッドの終了時にちょうど 1 回呼ばれる。
// 汎用の COM フレームワークは作らない — 必要なのはこれだけ。
class ComScope {
public:
    ComScope()
    {
        const HRESULT hr =
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        // RPC_E_CHANGED_MODE 等の失敗では **CoUninitialize を呼んではならない**
        // (他所が張った初期化を 1 つ剥がしてしまう)。その場合も続行はする —
        // 既に別のモデルで初期化済みなら SHGetFileInfoW は動く。
        m_owned = SUCCEEDED(hr);
    }
    ~ComScope()
    {
        if (m_owned)
            ::CoUninitialize();
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;

private:
    bool m_owned = false;
};

void ensureComInitialized()
{
    // 初回呼び出しでスレッドごとに構築され、スレッド終了時に破棄される。
    static thread_local ComScope scope;
    Q_UNUSED(scope)
}

} // namespace

QImage shellIconImage(const IconKey& key)
{
    ensureComInitialized();

    // SHGFI_USEFILEATTRIBUTES を付けると、この「パス」は実在しなくてよく、
    // shell は拡張子と属性だけを見る。ディスクへは行かない。
    const QString pseudoPath =
        key.isDirectory
            ? QStringLiteral("folder")
            : QStringLiteral("file") + (key.extension.isEmpty() ? QString() : u'.' + key.extension);
    const DWORD attributes = key.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    SHFILEINFOW info{};
    const DWORD_PTR ok =
        ::SHGetFileInfoW(reinterpret_cast<const wchar_t*>(pseudoPath.utf16()), attributes, &info,
                         sizeof(info), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    if (ok == 0 || info.hIcon == nullptr)
        return {};

    // HICON の所有権はこちらに移る。QImage へコピーしたら必ず破棄する。
    QImage image = QImage::fromHICON(info.hIcon);
    ::DestroyIcon(info.hIcon);
    return image;
}

} // namespace efs
