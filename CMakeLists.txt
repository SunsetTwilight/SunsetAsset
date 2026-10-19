# ============================================================
#  SunsetAsset: アセット (モデル、テクスチャ、音、独自のデータ …) の取り込みと管理のライブラリ
#
#  エンジン (SunsetGraphicsEngine) にも GPU にも依存しない。SunsetStudio などのアプリが使う。
#  設計: docs/design/asset-library.md
#
#  ターゲット:
#    SunsetAsset::core   GUID、.meta、取り込みの登録、キャッシュ (静的ライブラリ)
#    (予定) SunsetAsset::model / SunsetAsset::texture  種類ごとの取り込み
# ============================================================
cmake_minimum_required(VERSION 3.22)

project(SunsetAsset
    VERSION      0.1.0
    DESCRIPTION  "Asset import and management library (models, textures, audio, custom data)"
    LANGUAGES    CXX
)

option(SUNSET_ASSET_BUILD_TESTS "doctest のテストをビルドする" OFF)
option(SUNSET_ASSET_INSTALL     "install のルールを作る"       ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 警告とソースの文字コード (このライブラリのターゲットだけに付ける)
add_library(sasset_options INTERFACE)
if(MSVC)
    target_compile_options(sasset_options INTERFACE /W4 /WX /utf-8 /permissive- /Zc:__cplusplus)
    target_compile_definitions(sasset_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(sasset_options INTERFACE -Wall -Wextra -Werror)
endif()

# ============================================================
#  core
# ============================================================
add_library(sasset_core STATIC
    include/sunset/asset/Guid.h
    include/sunset/asset/Version.h
    src/Guid.cpp
)
add_library(SunsetAsset::core ALIAS sasset_core)
set_target_properties(sasset_core PROPERTIES EXPORT_NAME core OUTPUT_NAME SunsetAssetCore)
target_include_directories(sasset_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(sasset_core PUBLIC cxx_std_20)
target_compile_definitions(sasset_core PUBLIC
    SUNSET_ASSET_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SUNSET_ASSET_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SUNSET_ASSET_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_link_libraries(sasset_core PRIVATE $<BUILD_INTERFACE:sasset_options>)

# ============================================================
#  テスト
# ============================================================
if(SUNSET_ASSET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
#  install (find_package(SunsetAsset))
# ============================================================
if(SUNSET_ASSET_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(DIRECTORY include/sunset DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS sasset_core EXPORT SunsetAssetTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT SunsetAssetTargets
        FILE        SunsetAssetTargets.cmake
        NAMESPACE   SunsetAsset::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SunsetAsset)
    configure_package_config_file(cmake/SunsetAssetConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/SunsetAssetConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SunsetAsset)
    # 0.x のあいだは、マイナーバージョンが同じものだけを互換とする
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SunsetAssetConfigVersion.cmake
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SunsetAssetConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SunsetAssetConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SunsetAsset)
endif()
