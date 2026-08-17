cmake_minimum_required(VERSION 3.25)

project(efs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clang-tidy が参照する。Visual Studio ジェネレータでは生成されないため、
# lint は Ninja プリセット (ninja-x64-debug) で行う。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EFS_BUILD_TESTS "テストをビルドする" ON)

# MSBuild は 1 ターゲット内のファイルを 1 つずつ順にコンパイルするため、コア数に
# 対して CPU がほとんど遊ぶ (実測: 32 論理コアで 3〜7%)。/MP でターゲット内の
# ファイルを並列にコンパイルする (ディレクトリスコープなので tests/ にも継承)。
# ターゲット間の並列は build プリセットの `jobs` (= MSBuild の /m) が担当する。
#
# **Ninja には付けない。** Ninja は既にファイル単位で並列に走らせるので二重に
# なるうえ、`/MP` が compile_commands.json へ入ると clang-tidy が余計な引数を
# 見ることになる。
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/MP)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
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
    src/app/Settings.h
    src/app/Settings.cpp
    src/app/RegexValidation.h
    src/app/RegexValidation.cpp
    src/app/HotkeySpec.h
    src/app/HotkeySpec.cpp
    src/app/IconCache.h
    src/app/IconCache.cpp
    src/app/Theme.h
)
# src は PUBLIC (利用側が EverythingApi.h を include する)。
# Everything SDK は PRIVATE。これを PUBLIC にすると efs_core にリンクするだけで
# Everything.h が include できてしまい、UI から SDK を封じる意味が無くなる。
target_include_directories(efs_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${EFS_EVERYTHING_SDK_DIR}"
)
# Qt6::Gui はアイコンキャッシュ (IconCache) が QImage を扱うため。QImage は
# thread-safe な値型で QGuiApplication を必要としないので、テストは
# QTEST_GUILESS_MAIN のまま書ける。**Widgets と Win32 はここへ入れない** —
# QPixmap/QIcon 化は IconDelegate、shell lookup は ShellIcon.cpp (どちらも efs 側)。
target_link_libraries(efs_core PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(efs_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# --- アプリアイコン (.ico) -----------------------------------------------------
# Explorer やタスクバーが出すアイコンは PE のリソースなので、実行時に QPainter で
# 描く appIcon() では出せず、ビルド時に .ico の実体が要る。**図形の定義を 2 箇所に
# 持たない**ため、.ico をリポジトリへコミットするのではなく、同じ paintAppIcon() を
# 呼ぶ小さなツールでビルド時に生成する。
add_executable(efs_icongen tools/make_app_icon.cpp src/app/ToolbarIcons.cpp)
target_link_libraries(efs_icongen PRIVATE Qt6::Gui)
target_include_directories(efs_icongen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(EFS_APP_ICON "${CMAKE_CURRENT_BINARY_DIR}/efs.ico")
# 生成ツールの実行には Qt の DLL が要る。テスト (tests/CMakeLists.txt) と同じく
# EFS_QT_BIN_DIR を PATH の先頭へ足す。
add_custom_command(OUTPUT "${EFS_APP_ICON}"
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${EFS_QT_BIN_DIR}\;$ENV{PATH}"
            "$<TARGET_FILE:efs_icongen>" "${EFS_APP_ICON}"
    DEPENDS efs_icongen
    COMMENT "アプリアイコンを生成: ${EFS_APP_ICON}")

configure_file(src/app/efs.rc.in "${CMAKE_CURRENT_BINARY_DIR}/efs.rc" @ONLY)
# .rc のコンパイルより先に .ico を作らせる (順序が逆だとリソースが空になる)。
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/efs.rc"
    PROPERTIES OBJECT_DEPENDS "${EFS_APP_ICON}")

# --- efs (GUI) ----------------------------------------------------------------
qt_add_executable(efs WIN32
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/Theme.cpp
    src/app/ToolbarIcons.h
    src/app/ToolbarIcons.cpp
    src/app/FileActions.h
    src/app/FileActions.cpp
    src/app/ShellIcon.h
    src/app/ShellIcon.cpp
    src/app/IconDelegate.h
    src/app/IconDelegate.cpp
    src/app/GlobalHotkey.h
    src/app/GlobalHotkey.cpp
    src/app/SingleInstance.h
    src/app/SingleInstance.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/efs.rc"
)
# dwmapi: タイトルバーの明暗 (Theme.cpp)。
# shell32 / ole32: Explorer で選択 (FileActions.cpp) と種別アイコンの
# SHGetFileInfoW (ShellIcon.cpp)。いずれも Windows 固有処理を閉じ込めた
# ファイルだけが使う。
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
