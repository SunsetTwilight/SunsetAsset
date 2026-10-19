#pragma once
// ============================================================
//  sunset/asset/Version.h
//
//  ライブラリのバージョン (CMake の project(VERSION) から)。
// ============================================================

#include <cstdint>

namespace sunset::asset
{
struct LibraryVersion
{
	uint32_t major, minor, patch;
};

constexpr LibraryVersion libraryVersion() { return { SUNSET_ASSET_VERSION_MAJOR, SUNSET_ASSET_VERSION_MINOR, SUNSET_ASSET_VERSION_PATCH }; }
} // namespace sunset::asset
