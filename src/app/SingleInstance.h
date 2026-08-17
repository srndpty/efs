// 多重起動防止 (Phase 4)。常駐アプリなので必須。
//
// Qt Network (QLocalServer) は使わない。名前付き mutex + ブロードキャストした
// 登録済みウィンドウメッセージだけで足りるので、そのためだけに Qt のモジュールを
// 増やさない。Win32 はこのファイルへ閉じ込める。
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace efs {

class SingleInstance : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    // false なら既に別のインスタンスが動いている。呼び出し側は
    // requestExistingInstanceToShow() を呼んでから終了すること。
    [[nodiscard]] bool isPrimary() const { return m_primary; }

    // 既存インスタンスへ「前面に出ろ」と伝える。ブロードキャストなので、
    // 相手のウィンドウが隠れていても届く。
    static void requestExistingInstanceToShow();

signals:
    // 2 個目の起動が来た。
    void showRequested();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool m_primary = false;
    void* m_mutex = nullptr; // HANDLE。Win32 をヘッダへ持ち込まないため void*
    bool m_filterInstalled = false;
};

} // namespace efs
