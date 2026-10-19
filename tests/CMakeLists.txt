# ============================================================
#  自動テスト (doctest + CTest)
#    sasset_tests  GPU も外部のファイルも要らない単体テスト
# ============================================================
include(FetchContent)

# doctest はヘッダー 1 つだけを使う (エンジンと同じ版。同梱の CMake はテストやサンプルもビルドするため使わない)
set(SASSET_DOCTEST_VERSION "2.5.2")
FetchContent_Declare(sasset_doctest
    URL      https://github.com/doctest/doctest/archive/refs/tags/v${SASSET_DOCTEST_VERSION}.tar.gz
    URL_HASH SHA256=9189960c2bbbc4f3382ce0773b2bb5f13e3afd8fed47f55f193e11e85a4f9854
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  _sasset_no_cmake_
)
FetchContent_MakeAvailable(sasset_doctest)

add_library(sasset_doctest INTERFACE)
target_include_directories(sasset_doctest SYSTEM INTERFACE "${sasset_doctest_SOURCE_DIR}")   # #include <doctest/doctest.h>

add_executable(sasset_tests
    doctest_main.cpp
    test_guid.cpp
)
target_link_libraries(sasset_tests PRIVATE SunsetAsset::core sasset_doctest sasset_options)
add_test(NAME unit COMMAND sasset_tests)
