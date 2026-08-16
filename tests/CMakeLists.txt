find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

# テストは1ターゲットにまとめる。層ごとに分割しても現時点で得がない。
qt_add_executable(efs_tests test_everything_api.cpp)
target_link_libraries(efs_tests PRIVATE efs_core Qt6::Test)
# SDK のエラーコード定数を期待値として使うため、テストだけ明示的に SDK の
# include パスを持つ (efs_core 側は PRIVATE にしてあり伝播しない)。
target_include_directories(efs_tests PRIVATE "${EFS_EVERYTHING_SDK_DIR}")
# コンソールアプリであることを明示する (既定値に依存しない)。
set_target_properties(efs_tests PROPERTIES WIN32_EXECUTABLE FALSE)
efs_copy_everything_dll(efs_tests)

if(MSVC)
    target_compile_options(efs_tests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

add_test(NAME efs_tests COMMAND efs_tests)

# QT_ASSUME_STDERR_HAS_CONSOLE:
#   Windows では QtTest は標準出力がコンソールでない (= ctest にパイプされている)
#   と判断すると、結果を OutputDebugString へ送り標準出力には何も書かない。
#   そのままだと ctest --output-on-failure が失敗時に何も表示できないため、
#   コンソールがあるものとして扱わせる。
#
# PATH:
#   Qt の DLL が PATH に無いとテスト実行ファイルが起動できない。
set_tests_properties(efs_tests PROPERTIES
    ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${EFS_QT_BIN_DIR}")
