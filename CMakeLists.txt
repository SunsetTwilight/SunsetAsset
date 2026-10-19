# ============================================================
#  SunsetAsset: アセット (モデル、テクスチャ、音、独自のデータ …) の取り込みと管理のライブラリ
#
#  エンジン (SunsetGraphicsEngine) にも GPU にも依存しない。SunsetStudio などのアプリが使う。
#  設計: docs/design/asset-library.md
#
#  ターゲット:
#    SunsetAsset::core     GUID、.meta、取り込みの登録、キャッシュ (静的ライブラリ)
#    SunsetAsset::model    モデルの取り込み (FBX / OBJ: ufbx、glTF / GLB: cgltf)
#    SunsetAsset::texture  テクスチャの取り込み (PNG / JPEG / TGA / BMP: stb_image)
# ============================================================
cmake_minimum_required(VERSION 3.22)

project(SunsetAsset
    VERSION      0.1.0
    DESCRIPTION  "Asset import and management library (models, textures, audio, custom data)"
    LANGUAGES    C CXX
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
#  外部ライブラリ (アーカイブの URL + SHA256 で版を固定する)
#    nlohmann/json (MIT)  .meta と取り込みの設定。SunsetStudio と同じ名前と版にして、一緒に使うときは 1 つにまとめる
# ============================================================
include(FetchContent)
set(SASSET_JSON_VERSION "v3.12.0")
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/${SASSET_JSON_VERSION}/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# install するときは nlohmann_json の CMake のパッケージも一緒に入れる (find_dependency で見つけるため)
if(SUNSET_ASSET_INSTALL AND PROJECT_IS_TOP_LEVEL)
    set(JSON_Install ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(nlohmann_json)

# 取り込みに使うライブラリ (ヘッダー 1 つ + ソース 1 つ。同梱の CMake は使わない)
#   cgltf v1.15 (MIT)、ufbx v0.23.1 (MIT / 公開領域。下で個別に取得)、stb (MIT / 公開領域。stb_image の最後の変更のコミット)
FetchContent_Declare(sasset_cgltf
    URL      https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.15.tar.gz
    URL_HASH SHA256=84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _sasset_no_cmake_
)
FetchContent_Declare(sasset_stb
    URL      https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.tar.gz
    URL_HASH SHA256=b01aa93e1a968aed55f43e072c98ee401d2f20e897aabdb1a166c7166886ed11
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _sasset_no_cmake_
)
FetchContent_MakeAvailable(sasset_cgltf sasset_stb)

# ufbx はアーカイブ (テストデータ入りで、CMake の展開では開けない) ではなく、タグの ufbx.h / ufbx.c だけを取得する
set(sasset_ufbx_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/sasset_ufbx-v0.23.1")
foreach(item "ufbx.h|f787be529af577efc04f3e3e735844a08556718dbb613d1211c2b313d72895dc"
             "ufbx.c|4a956c26a708e40d82ecb0aeaee54da709d5ba7c144c9642174970059bda7e1d")
    string(REPLACE "|" ";" parts "${item}")
    list(GET parts 0 name)
    list(GET parts 1 hash)
    if(NOT EXISTS "${sasset_ufbx_SOURCE_DIR}/${name}")
        file(DOWNLOAD "https://raw.githubusercontent.com/ufbx/ufbx/v0.23.1/${name}" "${sasset_ufbx_SOURCE_DIR}/${name}"
             EXPECTED_HASH SHA256=${hash} STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${sasset_ufbx_SOURCE_DIR}/${name}")
            message(FATAL_ERROR "ufbx の ${name} を取得できません: ${status}")
        endif()
    endif()
endforeach()

# 外部のライブラリの実装 (C。警告を厳しくしない)
add_library(sasset_thirdparty STATIC
    src/thirdparty/impl.c
    ${sasset_ufbx_SOURCE_DIR}/ufbx.c
)
target_include_directories(sasset_thirdparty SYSTEM PUBLIC
    $<BUILD_INTERFACE:${sasset_cgltf_SOURCE_DIR}>
    $<BUILD_INTERFACE:${sasset_ufbx_SOURCE_DIR}>
    $<BUILD_INTERFACE:${sasset_stb_SOURCE_DIR}>
)
set_target_properties(sasset_thirdparty PROPERTIES OUTPUT_NAME SunsetAssetThirdParty POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(sasset_thirdparty PRIVATE /W0 /utf-8)
endif()

# ============================================================
#  core
# ============================================================
add_library(sasset_core STATIC
    include/sunset/asset/AssetDatabase.h
    include/sunset/asset/Artifact.h
    include/sunset/asset/Common.h
    include/sunset/asset/Guid.h
    include/sunset/asset/Importer.h
    include/sunset/asset/Meta.h
    include/sunset/asset/Version.h
    src/AssetDatabase.cpp
    src/Artifact.cpp
    src/Common.cpp
    src/Guid.cpp
    src/Importer.cpp
    src/Meta.cpp
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
target_link_libraries(sasset_core
    PUBLIC  nlohmann_json::nlohmann_json
    PRIVATE $<BUILD_INTERFACE:sasset_options>)

# ============================================================
#  model / texture
# ============================================================
add_library(sasset_model STATIC
    include/sunset/asset/model/Model.h
    include/sunset/asset/model/ModelImporter.h
    src/model/GltfImport.cpp
    src/model/MeshBuilder.h
    src/model/MeshBuilder.cpp
    src/model/ModelFormats.h
    src/model/ModelImporter.cpp
    src/model/ModelSerialize.cpp
    src/model/UfbxImport.cpp
)
add_library(SunsetAsset::model ALIAS sasset_model)
set_target_properties(sasset_model PROPERTIES EXPORT_NAME model OUTPUT_NAME SunsetAssetModel)
target_link_libraries(sasset_model
    PUBLIC  sasset_core
    PRIVATE sasset_thirdparty $<BUILD_INTERFACE:sasset_options>)

add_library(sasset_texture STATIC
    include/sunset/asset/texture/Texture.h
    src/texture/TextureImporter.cpp
)
add_library(SunsetAsset::texture ALIAS sasset_texture)
set_target_properties(sasset_texture PROPERTIES EXPORT_NAME texture OUTPUT_NAME SunsetAssetTexture)
target_link_libraries(sasset_texture
    PUBLIC  sasset_core
    PRIVATE sasset_thirdparty $<BUILD_INTERFACE:sasset_options>)

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
    install(TARGETS sasset_core sasset_model sasset_texture sasset_thirdparty EXPORT SunsetAssetTargets
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
