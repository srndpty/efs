// Everything64.dll (Everything 1.4.1) の動的ロード層。
//
// 暗黙リンクではなく LoadLibraryW + GetProcAddress を使う。暗黙リンクだと DLL
// が無いだけでプロセスが起動できず、理由をユーザーに提示できないため。
//
// Everything SDK を知っているのはこのヘッダと対応する .cpp だけ。Everything.h
// を include するのは EverythingApi.cpp のみ。
#pragma once

#include <QString>

#include <windows.h>

namespace efs {

class EverythingApi {
public:
    EverythingApi() = default;
    ~EverythingApi();

    EverythingApi(const EverythingApi&) = delete;
    EverythingApi& operator=(const EverythingApi&) = delete;

    // **exe と同階層の** Everything64.dll をロードし、以下の全エントリポイントを
    // 解決する。そこに DLL が無い、ロードできない、エクスポートが1つでも解決
    // できない場合は false を返し loadError() に理由を残す。冪等。
    //
    // **PATH / CWD へフォールバックしない。** 配置の authority はビルドと
    // package スクリプトであり、隣に無ければ配置の失敗として明確に落とす。
    bool load();

    [[nodiscard]] bool isLoaded() const { return m_module != nullptr; }
    [[nodiscard]] QString loadError() const { return m_loadError; }
    // ロードした DLL の絶対パス (= 常に exe と同階層)。診断表示用。
    [[nodiscard]] QString dllPath() const { return m_dllPath; }

    using SetSearchW_t = void(WINAPI*)(LPCWSTR);
    using SetRegex_t = void(WINAPI*)(BOOL);
    using SetMatchCase_t = void(WINAPI*)(BOOL);
    using SetMatchWholeWord_t = void(WINAPI*)(BOOL);
    using SetMatchPath_t = void(WINAPI*)(BOOL);
    using SetSort_t = void(WINAPI*)(DWORD);
    using SetRequestFlags_t = void(WINAPI*)(DWORD);
    using SetMax_t = void(WINAPI*)(DWORD);
    using SetOffset_t = void(WINAPI*)(DWORD);
    using QueryW_t = BOOL(WINAPI*)(BOOL);
    using GetNumResults_t = DWORD(WINAPI*)(void);
    using GetTotResults_t = DWORD(WINAPI*)(void);
    using GetResultFileNameW_t = LPCWSTR(WINAPI*)(DWORD);
    using GetResultPathW_t = LPCWSTR(WINAPI*)(DWORD);
    using GetResultSize_t = BOOL(WINAPI*)(DWORD, LARGE_INTEGER*);
    using GetResultDateModified_t = BOOL(WINAPI*)(DWORD, FILETIME*);
    using IsFolderResult_t = BOOL(WINAPI*)(DWORD);
    using GetLastError_t = DWORD(WINAPI*)(void);
    using CleanUp_t = void(WINAPI*)(void);
    using GetMajorVersion_t = DWORD(WINAPI*)(void);
    using GetMinorVersion_t = DWORD(WINAPI*)(void);
    using GetRevision_t = DWORD(WINAPI*)(void);
    using GetBuildNumber_t = DWORD(WINAPI*)(void);

    SetSearchW_t SetSearchW = nullptr;
    SetRegex_t SetRegex = nullptr;
    SetMatchCase_t SetMatchCase = nullptr;
    SetMatchWholeWord_t SetMatchWholeWord = nullptr;
    SetMatchPath_t SetMatchPath = nullptr;
    SetSort_t SetSort = nullptr;
    SetRequestFlags_t SetRequestFlags = nullptr;
    SetMax_t SetMax = nullptr;
    SetOffset_t SetOffset = nullptr;
    QueryW_t QueryW = nullptr;
    GetNumResults_t GetNumResults = nullptr;
    GetTotResults_t GetTotResults = nullptr;
    GetResultFileNameW_t GetResultFileNameW = nullptr;
    GetResultPathW_t GetResultPathW = nullptr;
    GetResultSize_t GetResultSize = nullptr;
    GetResultDateModified_t GetResultDateModified = nullptr;
    IsFolderResult_t IsFolderResult = nullptr;
    GetLastError_t GetLastError = nullptr;
    CleanUp_t CleanUp = nullptr;
    GetMajorVersion_t GetMajorVersion = nullptr;
    GetMinorVersion_t GetMinorVersion = nullptr;
    GetRevision_t GetRevision = nullptr;
    GetBuildNumber_t GetBuildNumber = nullptr;

private:
    HMODULE m_module = nullptr;
    QString m_loadError;
    QString m_dllPath;
};

// Everything_GetLastError() が返す EVERYTHING_ERROR_* を読める文字列にする。
QString everythingErrorText(DWORD code);

} // namespace efs
