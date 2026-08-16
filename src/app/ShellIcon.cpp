#include "app/ShellIcon.h"

#include <QImage>

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

namespace efs {

namespace {

// SHGetFileInfoW は shell の COM オブジェクトを内部で使う。アイコン worker の
// スレッドでも呼ぶので、そのスレッドで一度だけ COM を初期化する。
// thread_local なので「1 回だけ」がスレッドごとに成立する。
void ensureComInitialized()
{
    static thread_local bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    // 失敗しても続行する (別のモデルで初期化済みなら SHGetFileInfoW は動く)。
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
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
