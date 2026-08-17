#include "app/SingleInstance.h"

#include <QCoreApplication>

#include <windows.h>

namespace efs {

namespace {

// mutex 名。セッションごとに分けたいので Local\ 名前空間 (既定) を使う。
// Global\ にすると別ユーザーのセッションとも衝突する。
constexpr auto kMutexName = L"efs.single-instance";

// 既存インスタンスを起こすためのメッセージ。文字列から ID を得るので、
// 他アプリのメッセージ番号と衝突しない。
UINT showMessageId()
{
    static const UINT id = ::RegisterWindowMessageW(L"efs.show-window");
    return id;
}

} // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent)
{
    // 既に在れば GetLastError が ERROR_ALREADY_EXISTS を返す。
    // 失敗しても handle は有効なので、そのまま保持して破棄時に閉じる。
    HANDLE mutex = ::CreateMutexW(nullptr, FALSE, kMutexName);
    const DWORD error = ::GetLastError();
    m_mutex = mutex;
    m_primary = mutex != nullptr && error != ERROR_ALREADY_EXISTS;

    if (m_primary) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
}

SingleInstance::~SingleInstance()
{
    if (m_filterInstalled)
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_mutex != nullptr)
        ::CloseHandle(static_cast<HANDLE>(m_mutex));
}

void SingleInstance::requestExistingInstanceToShow()
{
    // HWND_BROADCAST は非表示のトップレベルウィンドウにも届く
    // (常駐中の efs はまさに隠れている)。
    ::PostMessageW(HWND_BROADCAST, showMessageId(), 0, 0);
}

bool SingleInstance::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result)
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != showMessageId())
        return false;

    emit showRequested();
    return true;
}

} // namespace efs
