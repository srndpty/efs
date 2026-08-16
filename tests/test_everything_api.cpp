// EverythingApi の単体 / 結合テスト。
//
// エラーコードの写像は Everything 非依存なので常に実行する。DLL ロードと IPC
// クエリは環境依存なので、利用できない場合は QSKIP して落とさない (CI には
// Everything が存在しないため)。
#include <QtTest>

#include "backend/everything/EverythingApi.h"

#include "Everything.h"

class TestEverythingApi : public QObject {
    Q_OBJECT

private slots:
    void errorTextMapsKnownCodes();
    void errorTextMapsUnknownCode();
    void loadResolvesAllEntryPoints();
    void queryReturnsResults();
};

void TestEverythingApi::errorTextMapsKnownCodes()
{
    QCOMPARE(efs::everythingErrorText(EVERYTHING_OK), QStringLiteral("OK"));
    QVERIFY(efs::everythingErrorText(EVERYTHING_ERROR_IPC).contains(QStringLiteral("ERROR_IPC")));
    QVERIFY(
        efs::everythingErrorText(EVERYTHING_ERROR_MEMORY).contains(QStringLiteral("ERROR_MEMORY")));
}

void TestEverythingApi::errorTextMapsUnknownCode()
{
    // 未知のコードでも空文字を返さず、コード値が読み取れること。
    const QString text = efs::everythingErrorText(9999);
    QVERIFY(!text.isEmpty());
    QVERIFY(text.contains(QStringLiteral("9999")));
}

void TestEverythingApi::loadResolvesAllEntryPoints()
{
    efs::EverythingApi api;
    if (!api.load())
        QSKIP(qPrintable(
            QStringLiteral("Everything64.dll をロードできない: %1").arg(api.loadError())));

    QVERIFY(api.isLoaded());
    QVERIFY(!api.dllPath().isEmpty());
    QVERIFY(api.loadError().isEmpty());

    // load() が true を返した以上、使用する全エントリポイントが解決済みである契約。
    QVERIFY(api.SetSearchW != nullptr);
    QVERIFY(api.QueryW != nullptr);
    QVERIFY(api.GetNumResults != nullptr);
    QVERIFY(api.GetResultFileNameW != nullptr);
    QVERIFY(api.CleanUp != nullptr);
}

void TestEverythingApi::queryReturnsResults()
{
    efs::EverythingApi api;
    if (!api.load())
        QSKIP(qPrintable(
            QStringLiteral("Everything64.dll をロードできない: %1").arg(api.loadError())));

    if (api.GetMajorVersion() == 0)
        QSKIP("Everything が起動していないため IPC クエリを実行できない");

    api.SetSearchW(L"ext:exe");
    api.SetRegex(FALSE);
    api.SetMatchCase(FALSE);
    api.SetMatchPath(FALSE);
    api.SetMatchWholeWord(FALSE);
    api.SetSort(EVERYTHING_SORT_NAME_ASCENDING);
    api.SetRequestFlags(EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH);
    api.SetOffset(0);
    api.SetMax(10);

    QVERIFY(api.QueryW(TRUE));
    QCOMPARE(api.GetLastError(), static_cast<DWORD>(EVERYTHING_OK));

    // Windows 機に .exe が1つも無いことはない。
    QVERIFY(api.GetTotResults() > 0);

    const DWORD num = api.GetNumResults();
    QVERIFY(num > 0);
    QVERIFY(num <= 10); // SetMax による打ち切りが効いていること
    QVERIFY(!QString::fromWCharArray(api.GetResultFileNameW(0)).isEmpty());
}

QTEST_GUILESS_MAIN(TestEverythingApi)
#include "test_everything_api.moc"
