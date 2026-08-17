// 多重起動防止と、既存インスタンスへの要求 (Phase 4)。常駐アプリなので必須。
//
// Qt Network (QLocalServer) は使わない。名前付き mutex + ブロードキャストした
// 登録済みウィンドウメッセージだけで足りるので、そのためだけに Qt のモジュールを
// 増やさない。Win32 はこのファイルへ閉じ込める。
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

namespace efs {

// このプロセスの立場。**`Error` を `Secondary` と同一視しない** — 「既に起動して
// いる」と「多重起動の判定自体ができなかった」は別の事象で、後者を黙って
// 成功終了させると、起動したはずのアプリが理由も無く消えたように見える。
enum class InstanceRole {
    Primary,   // 自分が最初のインスタンス
    Secondary, // 既に別のインスタンスが動いている
    Error,     // 判定・通信の土台が用意できなかった (理由は errorReason())
};

// 既存インスタンスへ送る要求。1 つの登録済みメッセージの wParam で区別する
// (メッセージを 2 つ登録しても失敗点が増えるだけ)。
enum class InstanceRequest {
    Show, // 前面に出せ
    Quit, // 設定を保存して終了しろ (install.ps1 の graceful exit)
};

class SingleInstance : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    [[nodiscard]] InstanceRole role() const { return m_role; }
    // role() == Error のときだけ中身がある。診断用 (UI には出さない)。
    [[nodiscard]] QString errorReason() const { return m_errorReason; }

    // 既存インスタンスへ要求を投げる。ブロードキャストなので、相手のウィンドウが
    // 隠れていても届く。**送れなければ false** — 送れていないのに成功として
    // 終了すると、install.ps1 が「graceful に終わった」と誤解する。
    static bool requestExistingInstance(InstanceRequest request, QString* reason);

signals:
    // 2 個目の起動が来た。
    void showRequested();
    // `efs.exe --quit` が来た。tray の Quit と同じ経路へ入れること。
    void quitRequested();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    InstanceRole m_role = InstanceRole::Error;
    QString m_errorReason;
    void* m_mutex = nullptr; // HANDLE。Win32 をヘッダへ持ち込まないため void*
    bool m_filterInstalled = false;
};

} // namespace efs
