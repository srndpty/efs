// Dynamic loader for Everything64.dll (Everything 1.4.1).
//
// The DLL is loaded with LoadLibraryW + GetProcAddress rather than import-linked,
// so a missing SDK degrades to a diagnosable error instead of preventing the
// process from starting at all.
//
// This header and its .cpp are the only place that knows about the Everything
// SDK. Everything.h itself is included only by EverythingApi.cpp.
#pragma once

#include <QString>
#include <QStringList>

#include <windows.h>

namespace efs {

class EverythingApi {
public:
    EverythingApi() = default;
    ~EverythingApi();

    EverythingApi(const EverythingApi&) = delete;
    EverythingApi& operator=(const EverythingApi&) = delete;

    // Loads the DLL and resolves every entry point below. Returns false and
    // leaves loadError() set if the DLL is missing or an export is unresolved.
    // Idempotent.
    bool load();

    bool isLoaded() const { return m_module != nullptr; }
    QString loadError() const { return m_loadError; }
    QString dllPath() const { return m_dllPath; }
    // Directories probed by the last load() attempt, for diagnostics.
    QStringList searchedPaths() const { return m_searchedPaths; }

    using SetSearchW_t        = void  (WINAPI*)(LPCWSTR);
    using SetRegex_t          = void  (WINAPI*)(BOOL);
    using SetMatchCase_t      = void  (WINAPI*)(BOOL);
    using SetMatchWholeWord_t = void  (WINAPI*)(BOOL);
    using SetMatchPath_t      = void  (WINAPI*)(BOOL);
    using SetSort_t           = void  (WINAPI*)(DWORD);
    using SetRequestFlags_t   = void  (WINAPI*)(DWORD);
    using SetMax_t            = void  (WINAPI*)(DWORD);
    using SetOffset_t         = void  (WINAPI*)(DWORD);
    using QueryW_t            = BOOL  (WINAPI*)(BOOL);
    using GetNumResults_t     = DWORD (WINAPI*)(void);
    using GetTotResults_t     = DWORD (WINAPI*)(void);
    using GetResultFileNameW_t = LPCWSTR (WINAPI*)(DWORD);
    using GetResultPathW_t    = LPCWSTR (WINAPI*)(DWORD);
    using GetResultSize_t     = BOOL  (WINAPI*)(DWORD, LARGE_INTEGER*);
    using GetResultDateModified_t = BOOL (WINAPI*)(DWORD, FILETIME*);
    using IsFolderResult_t    = BOOL  (WINAPI*)(DWORD);
    using GetLastError_t      = DWORD (WINAPI*)(void);
    using CleanUp_t           = void  (WINAPI*)(void);
    using GetMajorVersion_t   = DWORD (WINAPI*)(void);
    using GetMinorVersion_t   = DWORD (WINAPI*)(void);
    using GetRevision_t       = DWORD (WINAPI*)(void);
    using GetBuildNumber_t    = DWORD (WINAPI*)(void);

    SetSearchW_t        SetSearchW        = nullptr;
    SetRegex_t          SetRegex          = nullptr;
    SetMatchCase_t      SetMatchCase      = nullptr;
    SetMatchWholeWord_t SetMatchWholeWord = nullptr;
    SetMatchPath_t      SetMatchPath      = nullptr;
    SetSort_t           SetSort           = nullptr;
    SetRequestFlags_t   SetRequestFlags   = nullptr;
    SetMax_t            SetMax            = nullptr;
    SetOffset_t         SetOffset         = nullptr;
    QueryW_t            QueryW            = nullptr;
    GetNumResults_t     GetNumResults     = nullptr;
    GetTotResults_t     GetTotResults     = nullptr;
    GetResultFileNameW_t GetResultFileNameW = nullptr;
    GetResultPathW_t    GetResultPathW    = nullptr;
    GetResultSize_t     GetResultSize     = nullptr;
    GetResultDateModified_t GetResultDateModified = nullptr;
    IsFolderResult_t    IsFolderResult    = nullptr;
    GetLastError_t      GetLastError      = nullptr;
    CleanUp_t           CleanUp           = nullptr;
    GetMajorVersion_t   GetMajorVersion   = nullptr;
    GetMinorVersion_t   GetMinorVersion   = nullptr;
    GetRevision_t       GetRevision       = nullptr;
    GetBuildNumber_t    GetBuildNumber    = nullptr;

private:
    HMODULE     m_module = nullptr;
    QString     m_loadError;
    QString     m_dllPath;
    QStringList m_searchedPaths;
};

// Human-readable form of an EVERYTHING_ERROR_* code from Everything_GetLastError().
QString everythingErrorText(DWORD code);

} // namespace efs
