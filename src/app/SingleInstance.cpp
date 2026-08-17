#include "app/SingleInstance.h"

#include <QCoreApplication>

#include <windows.h>

namespace efs {

namespace {

// mutex 名。セッションごとに分けたいので Local\ 名前空間 (既定) を使う。
// Global\ にすると別ユーザーのセッションとも衝突する。
constexpr auto kMutexName = L"efs.single-instance";

// 既存インスタンスへの要求メッセージ。文字列から ID を得るので他アプリの
// メッセージ番号と衝突しない。**0 は「登録に失敗した」を意味する。**
UINT requestMessageId()
{
    static const UINT id = ::RegisterWindowMessageW(L"efs.instance-request");
    return id;
}

// wParam に載せる要求コード。列挙子の値そのものを流すと、並べ替えたときに
// 別プロセス間で意味がずれるので、明示した定数を使う。
constexpr WPARAM kRequestShow = 1;
constexpr WPARAM kRequestQuit = 2;

WPARAM toWParam(InstanceRequest request)
{
    return request == InstanceRequest::Quit ? kRequestQuit : kRequestShow;
}

QString win32ErrorText(const char* what, DWORD code)
{
    return QStringLiteral("%1 failed (Win32 error %2)").arg(QLatin1String(what)).arg(code);
}

} // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent)
{
    // 3 通りを区別する:
    //   非 NULL + !ERROR_ALREADY_EXISTS → Primary
    //   非 NULL +  ERROR_ALREADY_EXISTS → Secondary
    //   NULL                            → Error (handle は無い。閉じてはならない)
    HANDLE mutex = ::CreateMutexW(nullptr, FALSE, kMutexName);
    const DWORD createError = ::GetLastError();
    if (mutex == nullptr) {
        m_role = InstanceRole::Error;
        m_errorReason = win32ErrorText("CreateMutexW", createError);
        return;
    }

    m_mutex = mutex;
    m_role = createError == ERROR_ALREADY_EXISTS ? InstanceRole::Secondary : InstanceRole::Primary;

    if (m_role != InstanceRole::Primary)
        return;

    // 自分が Primary でも、要求を受け取れなければ「2 個目の起動で前面に出る」も
    // `--quit` も成立しない。mutex は保持したまま Error として扱う
    // (mutex を離すと、今度は多重起動まで許してしまう)。
    if (requestMessageId() == 0) {
        m_role = InstanceRole::Error;
        m_errorReason = win32ErrorText("RegisterWindowMessageW", ::GetLastError());
        return;
    }

    QCoreApplication::instance()->installNativeEventFilter(this);
    m_filterInstalled = true;
}

SingleInstance::~SingleInstance()
{
    if (m_filterInstalled)
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_mutex != nullptr)
        ::CloseHandle(static_cast<HANDLE>(m_mutex));
}

bool SingleInstance::requestExistingInstance(InstanceRequest request, QString* reason)
{
    const UINT message = requestMessageId();
    if (message == 0) {
        if (reason != nullptr)
            *reason = win32ErrorText("RegisterWindowMessageW", ::GetLastError());
        return false;
    }

    // HWND_BROADCAST は非表示のトップレベルウィンドウにも届く
    // (常駐中の efs はまさに隠れている)。
    if (::PostMessageW(HWND_BROADCAST, message, toWParam(request), 0) == 0) {
        if (reason != nullptr)
            *reason = win32ErrorText("PostMessageW", ::GetLastError());
        return false;
    }
    return true;
}

bool SingleInstance::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result)
    // ブロードキャストはウィンドウ宛だが、Qt の版によってどちらの eventType で
    // 渡されるかは内部実装次第なので両方受ける (GlobalHotkey.cpp と同じ理由)。
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;

    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != requestMessageId())
        return false;

    switch (msg->wParam) {
    case kRequestShow:
        emit showRequested();
        return true;
    case kRequestQuit:
        emit quitRequested();
        return true;
    default:
        // 知らない要求コード。将来の版から届いた可能性があるので、消費だけして
        // 何もしない (勝手に Show や Quit へ倒さない)。
        return true;
    }
}

} // namespace efs
