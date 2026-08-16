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
# Everything SDK との境界。Phase 1 以降で core/ と backend/ に育つ。
add_library(efs_core STATIC
    src/backend/everything/EverythingApi.h
    src/backend/everything/EverythingApi.cpp
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
qt_add_executable(efs WIN32 src/app/main.cpp)
target_link_libraries(efs PRIVATE Qt6::Widgets)
efs_copy_everything_dll(efs)

# --- efs_spike (コンソール / Phase 0 限定) --------------------------------------
# Everything SDK と ext: / regex: 併用 (計画 6.2) を実機確認するための調査用
# プログラム。Phase 1 で本物の backend が入った時点で削除する。
qt_add_executable(efs_spike src/spike/everything_spike.cpp)
target_link_libraries(efs_spike PRIVATE efs_core)
# SDK の定数 (EVERYTHING_REQUEST_* 等) を直接使うため、この調査用ターゲットだけ
# 明示的に SDK の include パスを持つ。定数を包み直す層は作らない (Phase 1 で
# このターゲットごと消えるため)。
target_include_directories(efs_spike PRIVATE "${EFS_EVERYTHING_SDK_DIR}")
# コンソールアプリであることを明示する (既定値に依存しない)。
set_target_properties(efs_spike PROPERTIES WIN32_EXECUTABLE FALSE)
efs_copy_everything_dll(efs_spike)

if(MSVC)
    foreach(t efs_core efs efs_spike)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    endforeach()
endif()

if(EFS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
