find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

# QtTest は 1 実行ファイルにつき 1 つの QTEST_MAIN しか置けないため、テストは
# ファイル単位でターゲットを分ける。共通の設定はこの関数に寄せる。
#
# QT_ASSUME_STDERR_HAS_CONSOLE:
#   Windows では QtTest は標準出力がコンソールでない (= ctest にパイプされている)
#   と判断すると、結果を OutputDebugString へ送り標準出力には何も書かない。
#   そのままだと ctest --output-on-failure が失敗時に何も表示できないため、
#   コンソールがあるものとして扱わせる。
#
# PATH:
#   Qt の DLL が PATH に無いとテスト実行ファイルが起動できない。
function(efs_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE efs_core Qt6::Test)
    # コンソールアプリであることを明示する (既定値に依存しない)。
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    efs_copy_everything_dll(${name})

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    endif()

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${EFS_QT_BIN_DIR}")
endfunction()

efs_add_test(test_everything_api)
efs_add_test(test_query_builder)
efs_add_test(test_formatting)
efs_add_test(test_result_model)
efs_add_test(test_search_controller)
efs_add_test(test_everything_backend)

# SDK のエラーコード定数を期待値として使うテストだけ、明示的に SDK の include
# パスを持つ (efs_core 側は PRIVATE にしてあり伝播しない)。
target_include_directories(test_everything_api PRIVATE "${EFS_EVERYTHING_SDK_DIR}")
