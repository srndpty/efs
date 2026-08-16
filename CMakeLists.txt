cmake_minimum_required(VERSION 3.25)

project(efs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clang-tidy が参照する。Visual Studio ジェネレータでは生成されないため、
# lint は Ninja プリセット (ninja-x64-debug) で行う。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EFS_BUILD_TESTS "テストをビルドする" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

# テスト実行時に Qt の DLL を PATH へ通すために使う。
set(EFS_QT_BIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

set(EFS_EVERYTHING_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/everything-sdk")
set(EFS_EVERYTHING_DLL     "${EFS_EVERYTHING_SDK_DIR}/dll/Everything64.dll")

if(NOT EXISTS "${EFS_EVERYTHING_DLL}")
    message(FATAL_ERROR "Everything64.dll が見つからない: ${EFS_EVERYTHING_DLL}")
endif()

# Everything64.dll を <target>.exe の隣へ配置する。実行時の LoadLibraryW が
# PATH に依存せず DLL を見つけられるようにするため。
function(efs_copy_everything_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EFS_EVERYTHING_DLL}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Everything64.dll を ${target} の隣へコピー")
endfunction()

# --- efs_core -----------------------------------------------------------------
# Qt Widgets に依存しないものはすべてここに入れる。テスト (efs_tests) が
# リンクできる必要があるため、src/app 配下でも Widgets 非依存のクラス
# (SearchController / SearchWorker / ResultTableModel) はこちらに置く。
# efs 実行ファイル側に残すのは MainWindow と main() だけ。
add_library(efs_core STATIC
    src/core/SearchTypes.h
    src/core/ISearchBackend.h
    src/core/FileKinds.h
    src/core/FileKinds.cpp
    src/core/Formatting.h
    src/core/Formatting.cpp
    src/core/PathUtils.h
    src/core/PathUtils.cpp
    src/backend/everything/EverythingApi.h
    src/backend/everything/EverythingApi.cpp
    src/backend/everything/EverythingQueryBuilder.h
    src/backend/everything/EverythingQueryBuilder.cpp
    src/backend/everything/EverythingBackend.h
    src/backend/everything/EverythingBackend.cpp
    src/app/SearchWorker.h
    src/app/SearchWorker.cpp
    src/app/SearchController.h
    src/app/SearchController.cpp
    src/app/ResultTableModel.h
    src/app/ResultTableModel.cpp
)
# src は PUBLIC (利用側が EverythingApi.h を include する)。
# Everything SDK は PRIVATE。これを PUBLIC にすると efs_core にリンクするだけで
# Everything.h が include できてしまい、UI から SDK を封じる意味が無くなる。
target_include_directories(efs_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${EFS_EVERYTHING_SDK_DIR}"
)
target_link_libraries(efs_core PUBLIC Qt6::Core)
target_compile_definitions(efs_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# --- efs (GUI) ----------------------------------------------------------------
qt_add_executable(efs WIN32
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/Theme.h
    src/app/Theme.cpp
    src/app/ToolbarIcons.h
    src/app/ToolbarIcons.cpp
    src/app/FileActions.h
    src/app/FileActions.cpp
)
# dwmapi: タイトルバーのダーク化 (Theme.cpp)。
# shell32 / ole32: Explorer で選択 (FileActions.cpp)。どちらも Windows 固有
# 処理を閉じ込めた 2 ファイルだけが使う。
target_link_libraries(efs PRIVATE efs_core Qt6::Widgets dwmapi shell32 ole32)
efs_copy_everything_dll(efs)

if(MSVC)
    foreach(t efs_core efs)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    endforeach()
endif()

if(EFS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
